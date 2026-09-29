#include "EditorApp.h"

#include "EditorUI.h"

#include <IndeetsEngine/Core/Platform.h>
#include <IndeetsEngine/Scripting/ProjectAssets.h>
#include <IndeetsEngine/Scripting/ScriptEngine.h>

#include <imgui_internal.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <map>
#include <limits>

namespace ie {

namespace {
constexpr const char* kSceneExtension = ".zscene";
constexpr float kToolbarHeight = 34.0f;
} // namespace

EditorApp::EditorApp(const ApplicationDesc& desc, int argc, char** argv) : Application(desc, argc, argv)
{
    m_IniPath = (Platform::ExecutableDir() / "editor_layout.ini").string();
    m_ImGui = std::make_unique<ImGuiLayer>(GetWindow(), GetRenderer(), m_IniPath);
    ImGuizmo::AllowAxisFlip(false); // keep axes pointing along +X/+Y/+Z like Unity
    m_SceneTarget = GetRenderer().CreateRenderTarget(m_SceneViewSize);
    m_GameTarget = GetRenderer().CreateRenderTarget(m_GameViewSize);
    m_UITarget = GetRenderer().CreateRenderTarget(m_UITargetSize);
    m_PreviewTarget = GetRenderer().CreateRenderTarget({512, 360});
}

EditorApp::~EditorApp()
{
    GetRenderer().WaitIdle();
    m_ImGui->ReleaseTexture(*m_SceneTarget);
    m_ImGui->ReleaseTexture(*m_GameTarget);
    m_ImGui->ReleaseTexture(*m_UITarget);
    m_ImGui->ReleaseTexture(*m_PreviewTarget);
    GetRenderer().DestroyRenderTarget(*m_PreviewTarget);
    GetRenderer().DestroyRenderTarget(*m_SceneTarget);
    GetRenderer().DestroyRenderTarget(*m_GameTarget);
    GetRenderer().DestroyRenderTarget(*m_UITarget);
    m_ImGui.reset();
}

// ------------------------------------------------------------------ lifecycle

void EditorApp::OnStart()
{
    std::string projectArg = ArgValue("--project");
    m_ProjectDir = projectArg.empty() ? Platform::ExecutableDir() / "Project" : std::filesystem::path(projectArg);
    std::error_code ec;
    std::filesystem::create_directories(AssetsDir() / "Scenes", ec);
    m_ProjectCurrentDir = AssetsDir();
    GetRenderer().SetAssetRoot(AssetsDir());
    CreateSampleSprites();
    GetWindow().onFileDrop = [this](const std::vector<std::filesystem::path>& files) { ImportFiles(files); };
    Log::Info("Project: {}", m_ProjectDir.string());

    // --scene <path>: a scene to open (relative to Assets or absolute), also a Unity .unity scene.
    std::string sceneArg = ArgValue("--scene");
    std::filesystem::path startScene = Platform::Utf8ToPath(sceneArg);
    if (!sceneArg.empty() && startScene.is_relative())
        startScene = AssetsDir() / startScene;
    if (sceneArg.empty() || !OpenScene(startScene)) {
        std::filesystem::path mainScene = AssetsDir() / "Scenes" / (std::string("Main") + kSceneExtension);
        if (!std::filesystem::exists(mainScene) || !OpenScene(mainScene)) {
            CreateDefaultScene();
            SaveSceneAs(mainScene);
        }
    }

    ResetUndo();
    InitScripting();
    if (std::find(Args().begin(), Args().end(), "--play") != Args().end())
        Play();
}

void EditorApp::OnShutdown()
{
    if (IsPlaying())
        Stop();
}

bool EditorApp::OnCloseRequested()
{
    if (IsPlaying())
        Stop();
    if (!m_Dirty)
        return true;
    RequestSceneChange([this] { Quit(); });
    return false;
}

void EditorApp::OnUpdate(float deltaTime)
{
    m_EditorCamera.Update(SmoothDeltaTime(), m_SceneViewHovered);
    UpdateScripting();

    // Unity's frame: FixedUpdate + physics steps, then Update, coroutines and LateUpdate.
    ScriptEngine& scripts = ScriptEngine::Get();
    if (m_PlayState == PlayState::Playing) {
        m_Physics.Update(
            m_Scene, deltaTime * scripts.TimeScale(), [&] { scripts.FixedUpdate(PhysicsWorld::kFixedTimeStep); },
            [&](const std::vector<ContactEvent>& contacts) { scripts.DispatchContacts(contacts); });
        scripts.Update(deltaTime);
        scripts.LateUpdate();
    } else if (m_PlayState == PlayState::Paused && m_StepRequested) {
        scripts.FixedUpdate(PhysicsWorld::kFixedTimeStep);
        m_Scene.UpdateWorldTransforms();
        m_Physics.StepOnce(m_Scene, [&](const std::vector<ContactEvent>& contacts) { scripts.DispatchContacts(contacts); });
        scripts.Update(PhysicsWorld::kFixedTimeStep);
        scripts.LateUpdate();
    }
    m_StepRequested = false;
    if (IsPlaying()) {
        // Esc gives the mouse back to the editor when a script locked the cursor, like Unity.
        if (Input::GetKeyDown(Key::Escape))
            Input::SetCursorLocked(false);
        if (scripts.ConsumeQuitRequest()) {
            Log::Info("Application.Quit() called: leaving Play Mode");
            Stop();
        }
    }
    UpdateTitle();
}

void EditorApp::OnRender()
{
    Renderer& renderer = GetRenderer();
    // Resize viewports to the sizes their panels had last frame (must happen outside a frame).
    renderer.ResizeRenderTarget(*m_SceneTarget, m_SceneViewSize);
    renderer.ResizeRenderTarget(*m_GameTarget, m_GameViewSize);
    renderer.ResizeRenderTarget(*m_UITarget, m_UITargetSize);

    if (!renderer.BeginFrame())
        return;

    m_Scene.UpdateWorldTransforms();
    m_PreviewVisible = false;
    m_ImGui->BeginFrame();
    bool tint = IsPlaying();
    if (tint) {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.2f, 0.23f, 0.29f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.16f, 0.19f, 0.25f, 1.0f));
    }
    DrawDockspace();
    DrawHierarchy();
    DrawInspector();
    DrawProject();
    DrawConsole();
    DrawGameView();
    DrawSceneView();
    DrawUIPanel();
    DrawModals();
    if (tint)
        ImGui::PopStyleColor(2);
    HandleShortcuts();
    CommitUndoIfIdle();
    m_Scene.UpdateWorldTransforms();

    if (m_SceneViewVisible) {
        SceneRenderOptions options;
        options.drawGrid = m_ShowGrid;
        options.selected = m_Selected;
        options.drawAllColliders = m_ShowColliders;
        options.drawUI = false; // screen-space UI is shown in the Game view
        float aspect = float(m_SceneTarget->extent.width) / float(m_SceneTarget->extent.height);
        renderer.DrawScene(*m_SceneTarget, m_Scene, m_EditorCamera.Data(aspect), options);
    }
    if (m_GameViewVisible) {
        if (const Entity* cam = m_Scene.MainCamera()) {
            PerspectiveLens lens{cam->camera->fieldOfView, cam->camera->nearClip, cam->camera->farClip};
            float aspect = float(m_GameTarget->extent.width) / float(m_GameTarget->extent.height);
            renderer.DrawScene(*m_GameTarget, m_Scene, MakeCameraData(cam->transform, lens, aspect));
        }
    }

    if (m_PreviewVisible) {
        // Orbit camera around the previewed model / prefab.
        Transform cam;
        cam.SetEulerAngles({m_PreviewPitch, m_PreviewYaw, 0.0f});
        cam.position = m_PreviewCenter - cam.Forward() * m_PreviewDistance;
        PerspectiveLens lens{40.0f, 0.01f, 5000.0f};
        float aspect = float(m_PreviewTarget->extent.width) / float(m_PreviewTarget->extent.height);
        m_PreviewScene.UpdateWorldTransforms();
        SceneRenderOptions options;
        options.drawUI = false;
        renderer.DrawScene(*m_PreviewTarget, m_PreviewScene, MakeCameraData(cam, lens, aspect), options);
    }

    if (m_UIPanelVisible) {
        SceneRenderOptions options;
        const Entity* cam = m_UIBackground == 0 ? m_Scene.MainCamera() : nullptr;
        options.drawWorld = cam != nullptr;
        options.background = m_UIBackground == 2 ? glm::vec4(0.78f, 0.78f, 0.8f, 1.0f) : glm::vec4(0.13f, 0.13f, 0.15f, 1.0f);
        float aspect = float(m_UITarget->extent.width) / float(m_UITarget->extent.height);
        CameraData data = m_EditorCamera.Data(aspect);
        if (cam) {
            PerspectiveLens lens{cam->camera->fieldOfView, cam->camera->nearClip, cam->camera->farClip};
            data = MakeCameraData(cam->transform, lens, aspect);
        }
        renderer.DrawScene(*m_UITarget, m_Scene, data, options);
    }

    renderer.BeginScreenPass(true);
    m_ImGui->Render(renderer.CommandBuffer());
    renderer.EndScreenPass();
    renderer.EndFrame();
}

void EditorApp::UpdateTitle()
{
    std::string sceneName = m_ScenePath.empty() ? "Untitled" : RelativeToAssets(m_ScenePath);
    std::string title = std::format("IndeetsEngine Editor - {}{}{}  |  {:.0f} FPS", sceneName, m_Dirty ? "*" : "",
                                    IsPlaying() ? "  [PLAY MODE]" : "", Fps());
    static std::string last;
    if (title != last) {
        GetWindow().SetTitle(title);
        last = title;
    }
}

// ------------------------------------------------------------------ layout

void EditorApp::DrawDockspace()
{
    DrawMenuBar();
    DrawToolbar();

    ImGuiID dockspace = ImGui::GetID("MainDockspace");
    if (!ImGui::DockBuilderGetNode(dockspace) || m_LayoutReset) {
        BuildDefaultLayout(dockspace);
        m_LayoutReset = false;
    }
    ImGui::DockSpaceOverViewport(dockspace, ImGui::GetMainViewport());
}

void EditorApp::BuildDefaultLayout(ImGuiID dockspace)
{
    // Unity-like: Hierarchy left, Scene/Game center, Inspector right, Project/Console bottom.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);

    ImGuiID center = dockspace;
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.3f, nullptr, &center);
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);

    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderDockWindow("Game", center);
    ImGui::DockBuilderDockWindow("UI", center);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Project", bottom);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderFinish(dockspace);
    m_FocusSceneView = true;
}

void EditorApp::DrawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene", "Ctrl+N", false, !IsPlaying()))
            RequestSceneChange([this] { NewScene(); });
        if (ImGui::BeginMenu("Open Scene", !IsPlaying())) {
            bool any = false;
            std::error_code ec;
            for (auto it = std::filesystem::recursive_directory_iterator(AssetsDir(), ec);
                 it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (ec || it->path().extension() != kSceneExtension)
                    continue;
                any = true;
                std::filesystem::path path = it->path();
                if (ImGui::MenuItem(RelativeToAssets(path).c_str(), nullptr, path == m_ScenePath))
                    RequestSceneChange([this, path] { OpenScene(path); });
            }
            if (!any)
                ImGui::TextDisabled("No scenes in Assets");
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, !IsPlaying()))
            SaveScene();
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, !IsPlaying()))
            m_OpenSaveAsModal = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Show Project Folder"))
            Platform::OpenInFileBrowser(m_ProjectDir);
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4"))
            if (OnCloseRequested())
                Quit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_UndoStack.empty() && !IsPlaying()))
            Undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_RedoStack.empty() && !IsPlaying()))
            Redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, Selected() != nullptr))
            DuplicateSelected();
        if (ImGui::MenuItem("Delete", "Del", false, Selected() != nullptr))
            DeleteSelected();
        if (ImGui::MenuItem("Frame Selected", "F", false, Selected() != nullptr))
            FocusSelected();
        ImGui::Separator();
        if (ImGui::MenuItem("Play", "Ctrl+P", IsPlaying()))
            IsPlaying() ? Stop() : Play();
        if (ImGui::MenuItem("Pause", "Ctrl+Shift+P", m_PlayState == PlayState::Paused, IsPlaying()))
            TogglePause();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("GameObject")) {
        DrawCreateMenuItems();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Scene Grid", nullptr, &m_ShowGrid);
        ImGui::MenuItem("Show All Colliders", nullptr, &m_ShowColliders);
        ImGui::MenuItem("Scene Stats", nullptr, &m_ShowStats);
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout"))
            m_LayoutReset = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About IndeetsEngine"))
            m_OpenAboutModal = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void EditorApp::DrawToolbar()
{
    // Top bar: play controls only. Transform tools live in the Scene view's own toolbar.
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
    if (ImGui::BeginViewportSideBar("##Toolbar", ImGui::GetMainViewport(), ImGuiDir_Up, kToolbarHeight, flags)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", m_ScenePath.empty() ? "Untitled" : RelativeToAssets(m_ScenePath).c_str());

        float center = ImGui::GetWindowWidth() * 0.5f;
        ImGui::SameLine(center - 50.0f);
        if (UI::IconButton("play", UI::Icon::Play, IsPlaying(), IsPlaying() ? "Stop (Ctrl+P)" : "Play (Ctrl+P)"))
            IsPlaying() ? Stop() : Play();
        ImGui::SameLine();
        if (UI::IconButton("pause", UI::Icon::Pause, m_PlayState == PlayState::Paused, "Pause (Ctrl+Shift+P)"))
            TogglePause();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_PlayState != PlayState::Paused);
        if (UI::IconButton("step", UI::Icon::Step, false, "Step one frame"))
            m_StepRequested = true;
        ImGui::EndDisabled();

        ScriptEngine& scripts = ScriptEngine::Get();
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::AlignTextToFramePadding();
        if (scripts.IsCompiling() || m_ScriptsDirty)
            ImGui::TextDisabled(m_PlayAfterCompile ? "Compiling scripts, Play Mode will start..." : "Compiling scripts...");
        else if (scripts.HasCompileErrors())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "Script compile errors (see Console)");
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorApp::DrawModals()
{
    if (m_OpenUnsavedModal) {
        ImGui::OpenPopup("Unsaved Changes");
        m_OpenUnsavedModal = false;
    }
    if (m_OpenSaveAsModal) {
        ImGui::OpenPopup("Save Scene As");
        m_SaveAsBuffer = m_ScenePath.empty() ? "Scenes/NewScene.zscene" : RelativeToAssets(m_ScenePath);
        m_OpenSaveAsModal = false;
    }
    if (m_OpenAboutModal) {
        ImGui::OpenPopup("About IndeetsEngine");
        m_OpenAboutModal = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string name = m_ScenePath.empty() ? "Untitled" : m_ScenePath.filename().string();
        ImGui::Text("Do you want to save the changes you made in %s?", name.c_str());
        ImGui::TextDisabled("Your changes will be lost if you don't save them.");
        ImGui::Spacing();
        auto finish = [this] {
            ImGui::CloseCurrentPopup();
            if (m_PendingSceneAction) {
                auto action = std::move(m_PendingSceneAction);
                m_PendingSceneAction = nullptr;
                action();
            }
        };
        if (ImGui::Button("Save", ImVec2(110, 0))) {
            if (m_ScenePath.empty()) {
                ImGui::CloseCurrentPopup();
                m_OpenSaveAsModal = true;
            } else if (SaveScene()) {
                finish();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(110, 0))) {
            m_Dirty = false;
            finish();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_PendingSceneAction = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Path inside the Assets folder:");
        ImGui::SetNextItemWidth(380.0f);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("##path", &m_SaveAsBuffer, ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Save", ImVec2(120, 0)) || enter) {
            std::filesystem::path path = AssetsDir() / m_SaveAsBuffer;
            if (path.extension() != kSceneExtension)
                path += kSceneExtension;
            if (SaveSceneAs(path)) {
                ImGui::CloseCurrentPopup();
                if (m_PendingSceneAction) {
                    auto action = std::move(m_PendingSceneAction);
                    m_PendingSceneAction = nullptr;
                    action();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_PendingSceneAction = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About IndeetsEngine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("IndeetsEngine Editor 0.6");
        ImGui::Separator();
        ImGui::TextUnformatted("Vulkan 1.3 renderer, Jolt Physics, Dear ImGui.");
        ImGui::TextDisabled("Scene view: RMB + WASD/QE fly, MMB pan, wheel zoom, F frame selected.");
        ImGui::TextDisabled("Tools: W move, E rotate, R scale. Ctrl+P play.");
        if (ImGui::Button("Close", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void EditorApp::HandleShortcuts()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        return;
    bool ctrl = io.KeyCtrl, shift = io.KeyShift;

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (shift || m_ScenePath.empty())
            m_OpenSaveAsModal = true;
        else
            SaveScene();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false) && !IsPlaying())
        RequestSceneChange([this] { NewScene(); });
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) {
        if (shift)
            TogglePause();
        else
            IsPlaying() ? Stop() : Play();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
        DuplicateSelected();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        shift ? Redo() : Undo();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
        Redo();

    bool sceneFocus = m_SceneViewHovered || m_SceneViewFocused ||
                      ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow) == false;
    bool hierarchyFocus = m_HierarchyFocused || m_UIPanelFocused;
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && (sceneFocus || hierarchyFocus))
        DeleteSelected();
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) && (sceneFocus || hierarchyFocus))
        FocusSelected();
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && Selected()) {
        m_RenamingEntity = m_Selected;
        m_RenameBuffer = Selected()->name;
    }

    if (!ctrl && (sceneFocus || m_UIPanelFocused) && !m_EditorCamera.IsControlling()) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_GizmoOperation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_GizmoOperation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_GizmoOperation = ImGuizmo::SCALE;
    }
}

// ------------------------------------------------------------------ scenes

void EditorApp::NewScene()
{
    if (IsPlaying())
        Stop();
    m_Scene.Clear();
    Entity& camera = CreateObject("Main Camera");
    camera.camera = CameraComponent{};
    camera.transform.position = {0.0f, 1.0f, -10.0f};
    Entity& light = CreateObject("Directional Light");
    light.light = LightComponent{};
    light.transform.position = {0.0f, 3.0f, 0.0f};
    light.transform.SetEulerAngles({50.0f, -30.0f, 0.0f});
    m_ScenePath.clear();
    m_Selected = 0;
    m_Dirty = false;
    ResetUndo();
    Log::Info("New scene");
}

void EditorApp::CreateDefaultScene()
{
    NewScene();
    Entity* camera = m_Scene.Find("Main Camera");
    camera->transform.position = {0.0f, 4.0f, -12.0f};
    camera->transform.SetEulerAngles({15.0f, 0.0f, 0.0f});

    Entity& ground = m_Scene.CreatePrimitive(PrimitiveType::Plane, "Ground");
    ground.transform.scale = glm::vec3(2.0f);
    ground.meshRenderer->color = {0.72f, 0.72f, 0.72f, 1.0f};
    ground.meshRenderer->checkerScale = 1.0f;

    const glm::vec4 colors[] = {{0.9f, 0.3f, 0.25f, 1}, {0.3f, 0.55f, 0.95f, 1}, {0.35f, 0.8f, 0.4f, 1},
                                {0.98f, 0.75f, 0.2f, 1}};
    for (int i = 0; i < 4; ++i) {
        Entity& box = m_Scene.CreatePrimitive(PrimitiveType::Cube, std::format("Box {}", i + 1));
        box.transform.position = {-2.0f + 0.15f * float(i), 0.5f + float(i) * 1.05f, 0.0f};
        box.transform.SetEulerAngles({0.0f, float(i) * 20.0f, 0.0f});
        box.meshRenderer->color = colors[i];
        box.rigidbody = RigidbodyComponent{};
    }

    Entity& ball = m_Scene.CreatePrimitive(PrimitiveType::Sphere, "Bouncy Ball");
    ball.transform.position = {1.5f, 6.0f, 0.0f};
    ball.meshRenderer->color = {0.95f, 0.95f, 0.95f, 1.0f};
    ball.collider->bounciness = 0.8f;
    ball.rigidbody = RigidbodyComponent{};

    Entity& capsule = m_Scene.CreatePrimitive(PrimitiveType::Capsule);
    capsule.transform.position = {3.5f, 3.0f, 1.0f};
    capsule.transform.SetEulerAngles({0.0f, 0.0f, 40.0f});
    capsule.meshRenderer->color = {0.7f, 0.4f, 0.9f, 1.0f};
    capsule.rigidbody = RigidbodyComponent{};

    Entity& ramp = m_Scene.CreatePrimitive(PrimitiveType::Cube, "Ramp");
    ramp.transform.position = {-5.0f, 1.0f, 2.0f};
    ramp.transform.scale = {4.0f, 0.3f, 3.0f};
    ramp.transform.SetEulerAngles({0.0f, 0.0f, -20.0f});
    ramp.meshRenderer->color = {0.55f, 0.5f, 0.45f, 1.0f};
}

UnityAssetDatabase& EditorApp::UnityAssets()
{
    if (!m_UnityAssets)
        m_UnityAssets = std::make_shared<UnityAssetDatabase>(AssetsDir());
    return *m_UnityAssets;
}

UnityImportOptions EditorApp::UnityOptions()
{
    UnityImportOptions options;
    options.loadModel = [this](const std::string& assetPath) { return GetRenderer().LoadModel(assetPath); };
    return options;
}

bool EditorApp::OpenScene(const std::filesystem::path& path)
{
    if (IsPlaying())
        Stop();
    Scene loaded;
    bool unity = UnityImporter::IsUnityScene(path);
    if (unity) {
        // A Unity scene is converted: saving writes an engine scene next to it.
        UnityAssets().Refresh();
        UnityImportReport report;
        auto start = std::chrono::steady_clock::now();
        if (!UnityImporter::ImportScene(UnityAssets(), RelativeToAssets(path), loaded, UnityOptions(), &report)) {
            Log::Error("Could not import Unity scene {}", RelativeToAssets(path));
            return false;
        }
        float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
        Log::Info("Imported Unity scene {} in {:.1f} s: {}", RelativeToAssets(path), seconds, report.Summary());
        std::map<std::string, int> warnings;
        for (const std::string& w : report.warnings)
            ++warnings[w];
        for (const auto& [w, n] : warnings)
            n > 1 ? Log::Warn("{} (x{})", w, n) : Log::Warn("{}", w);
    } else if (!SceneSerializer::Load(loaded, path)) {
        return false;
    }
    SceneSerializer::FromString(m_Scene, SceneSerializer::ToString(loaded));
    m_ScenePath = path;
    if (unity)
        m_ScenePath.replace_extension(kSceneExtension);
    m_Selected = 0;
    m_Dirty = unity;
    ResetUndo();
    if (!unity)
        Log::Info("Opened scene {}", RelativeToAssets(path));
    return true;
}

bool EditorApp::SaveScene()
{
    if (IsPlaying()) {
        Log::Warn("Exit play mode before saving the scene");
        return false;
    }
    if (m_ScenePath.empty()) {
        m_OpenSaveAsModal = true;
        return false;
    }
    return SaveSceneAs(m_ScenePath);
}

bool EditorApp::SaveSceneAs(const std::filesystem::path& path)
{
    if (!SceneSerializer::Save(m_Scene, path))
        return false;
    m_ScenePath = path;
    m_Dirty = false;
    InvalidateProjectCache();
    Log::Info("Saved scene {}", RelativeToAssets(path));
    return true;
}

void EditorApp::MarkDirty()
{
    if (!IsPlaying()) {
        m_Dirty = true;
        m_UndoPending = true;
    }
}

void EditorApp::RequestSceneChange(std::function<void()> action)
{
    if (!m_Dirty || IsPlaying()) {
        action();
        return;
    }
    m_PendingSceneAction = std::move(action);
    m_OpenUnsavedModal = true;
}

std::string EditorApp::RelativeToAssets(const std::filesystem::path& path) const
{
    std::error_code ec;
    auto rel = std::filesystem::relative(path, AssetsDir(), ec);
    return (ec || rel.empty() ? path : rel).generic_string();
}

// ------------------------------------------------------------------ play mode

void EditorApp::Play()
{
    if (IsPlaying())
        return;
    ScriptEngine& scripts = ScriptEngine::Get();
    if (scripts.IsAvailable()) {
        if (scripts.IsCompiling() || m_ScriptsDirty) {
            m_PlayAfterCompile = true; // Play Mode starts as soon as the scripts are compiled
            return;
        }
        if (scripts.HasCompileErrors()) {
            Log::Error("All compiler errors have to be fixed before you can enter Play Mode!");
            m_FocusConsole = true;
            return;
        }
    }
    if (m_UnityAssets)
        m_UnityAssets->Refresh(); // prefabs and assets may have changed since the last import
    m_EditSnapshot = SceneSerializer::ToString(m_Scene);
    m_Physics.Start(m_Scene);
    m_PlayState = PlayState::Playing;
    m_FocusGameView = true;
    Log::Info("Entered play mode ({} physics bodies)", m_Physics.BodyCount());
    scripts.BeginPlay(m_Scene, &m_Physics);
}

void EditorApp::Stop()
{
    if (!IsPlaying())
        return;
    ScriptEngine::Get().EndPlay();
    m_Physics.Stop();
    SceneSerializer::FromString(m_Scene, m_EditSnapshot);
    m_EditSnapshot.clear();
    m_PlayState = PlayState::Edit;
    m_FocusSceneView = true;
    if (!m_Scene.Get(m_Selected))
        m_Selected = 0;
    Log::Info("Exited play mode");
}

void EditorApp::TogglePause()
{
    if (m_PlayState == PlayState::Playing)
        m_PlayState = PlayState::Paused;
    else if (m_PlayState == PlayState::Paused)
        m_PlayState = PlayState::Playing;
}

// ------------------------------------------------------------------ selection

Entity* EditorApp::Selected() { return m_Selected ? m_Scene.Get(m_Selected) : nullptr; }

void EditorApp::Select(EntityID id)
{
    m_Selected = id;
    if (id)
        m_SelectedAsset.clear();
}

void EditorApp::DeleteSelected()
{
    if (!Selected())
        return;
    if (IsPlaying() && ScriptEngine::Get().IsPlaying()) {
        ScriptEngine::Get().DestroyEntity(m_Selected); // runs OnDisable / OnDestroy, removes bodies
    } else {
        if (IsPlaying())
            for (EntityID id : m_Scene.Subtree(m_Selected))
                m_Physics.RemoveEntity(id);
        m_Scene.DestroyEntity(m_Selected);
    }
    m_Selected = 0;
    MarkDirty();
}

void EditorApp::DuplicateSelected()
{
    if (!Selected())
        return;
    EntityID copy = m_Scene.Duplicate(m_Selected).id;
    if (IsPlaying()) {
        m_Scene.UpdateWorldTransforms();
        for (EntityID id : m_Scene.Subtree(copy))
            m_Physics.AddEntity(*m_Scene.Get(id));
        StartScriptsOf(copy);
    }
    m_Selected = copy;
    MarkDirty();
}

void EditorApp::FocusSelected()
{
    Entity* e = Selected();
    if (!e)
        return;
    if (e->IsUIOnly()) {
        m_FocusUIPanel = true;
        return;
    }
    m_Scene.UpdateWorldTransforms();
    // Bounds of the whole subtree (models are usually a hierarchy of meshes).
    glm::vec3 bmin(std::numeric_limits<float>::max()), bmax(-std::numeric_limits<float>::max());
    for (EntityID id : m_Scene.Subtree(e->id)) {
        const Entity* s = m_Scene.Get(id);
        if (!s || !s->meshRenderer)
            continue;
        const Mesh* mesh = GetRenderer().FindMesh(s->meshRenderer->mesh);
        if (!mesh)
            continue;
        for (int c = 0; c < 8; ++c) {
            glm::vec3 corner((c & 1) ? mesh->boundsMax.x : mesh->boundsMin.x, (c & 2) ? mesh->boundsMax.y : mesh->boundsMin.y,
                             (c & 4) ? mesh->boundsMax.z : mesh->boundsMin.z);
            glm::vec3 w = s->world * glm::vec4(corner, 1.0f);
            bmin = glm::min(bmin, w);
            bmax = glm::max(bmax, w);
        }
    }
    if (bmin.x > bmax.x)
        m_EditorCamera.Focus(glm::vec3(e->world[3]), 0.5f);
    else
        m_EditorCamera.Focus((bmin + bmax) * 0.5f, glm::length(bmax - bmin) * 0.5f);
}

Entity& EditorApp::CreateObject(const std::string& name)
{
    Entity& e = m_Scene.CreateEntity(name);
    m_Selected = e.id;
    MarkDirty();
    return e;
}

Entity& EditorApp::CreatePrimitiveObject(PrimitiveType type)
{
    Entity& e = m_Scene.CreatePrimitive(type);
    // Spawn in front of the scene camera, like Unity.
    e.transform.position = m_EditorCamera.transform.position + m_EditorCamera.transform.Forward() * 8.0f;
    if (IsPlaying())
        m_Physics.AddEntity(e);
    m_Selected = e.id;
    MarkDirty();
    return e;
}

EntityID EditorApp::PickEntity(const glm::vec3& origin, const glm::vec3& direction)
{
    EntityID best = 0;
    float bestDistance = std::numeric_limits<float>::max();
    for (const auto& e : m_Scene.Entities()) {
        if (e->IsUIOnly() || !m_Scene.IsActiveInHierarchy(e->id))
            continue;
        glm::vec3 bmin(-0.35f), bmax(0.35f); // handle for objects without a mesh (lights, cameras)
        glm::mat4 model = e->world;
        if (e->meshRenderer) {
            const Mesh* mesh = GetRenderer().FindMesh(e->meshRenderer->mesh);
            if (!mesh)
                continue;
            bmin = mesh->boundsMin - 0.001f;
            bmax = mesh->boundsMax + 0.001f;
        } else {
            model = glm::translate(glm::mat4(1.0f), glm::vec3(e->world[3]));
        }
        glm::mat4 inv = glm::inverse(model);
        glm::vec3 o = inv * glm::vec4(origin, 1.0f);
        glm::vec3 d = inv * glm::vec4(direction, 0.0f);

        // Slab test in local space.
        float tmin = 0.0f, tmax = std::numeric_limits<float>::max();
        bool hit = true;
        for (int axis = 0; axis < 3 && hit; ++axis) {
            if (std::abs(d[axis]) < 1e-8f) {
                hit = o[axis] >= bmin[axis] && o[axis] <= bmax[axis];
                continue;
            }
            float t1 = (bmin[axis] - o[axis]) / d[axis];
            float t2 = (bmax[axis] - o[axis]) / d[axis];
            tmin = std::max(tmin, std::min(t1, t2));
            tmax = std::min(tmax, std::max(t1, t2));
            hit = tmin <= tmax;
        }
        if (!hit)
            continue;
        glm::vec3 worldHit = model * glm::vec4(o + d * tmin, 1.0f);
        float distance = glm::length(worldHit - origin);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = e->id;
        }
    }
    return best;
}

} // namespace ie
