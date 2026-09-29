#pragma once

#include <ZEngine/ZEngine.h>
#include <ZEngine/Scene/UILayout.h>
#include <ZEngine/UI/ImGuiLayer.h>

#include <ImGuizmo.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ze {

enum class PlayState { Edit, Playing, Paused };

class EditorApp final : public Application {
public:
    EditorApp(const ApplicationDesc& desc, int argc, char** argv);
    ~EditorApp() override;

protected:
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnRender() override;
    void OnShutdown() override;
    bool OnCloseRequested() override;

private:
    // ---- Frame / layout (EditorApp.cpp)
    void DrawDockspace();
    void BuildDefaultLayout(ImGuiID dockspace);
    void DrawMenuBar();
    void DrawToolbar();
    void DrawModals();
    void HandleShortcuts();
    void UpdateTitle();

    // ---- Panels
    void DrawHierarchy();          // HierarchyPanel.cpp
    void DrawCreateMenuItems();    // HierarchyPanel.cpp
    void DrawInspector();          // InspectorPanel.cpp
    void DrawSceneSettings();      // InspectorPanel.cpp
    void DrawProject();            // ProjectPanel.cpp
    void DrawConsole();            // ConsolePanel.cpp
    bool m_HierarchyFocused = false;
    void DrawSceneView();          // Viewports.cpp
    void DrawGameView();           // Viewports.cpp
    void DrawStats();              // Viewports.cpp
    void DrawSceneToolbar();       // Viewports.cpp
    void DrawGameViewUIOverlay(ImVec2 origin, ImVec2 size, bool hovered); // Viewports.cpp
    void DrawRectTransform(Entity& entity, bool& changed);                  // InspectorPanel.cpp
    bool DrawSpriteField(std::string& sprite);                              // InspectorPanel.cpp
    void DrawUIPanel();                                                     // UIPanel.cpp
    void DrawUILayers();                                                    // UIPanel.cpp
    void DrawUICanvas();                                                    // UIPanel.cpp
    void ReparentUI(EntityID child, EntityID newParent);                    // UIPanel.cpp
    // UI layout at the reference resolution (used for editing in canvas units).
    UILayoutResult ReferenceLayout() const;                                 // UIPanel.cpp
    void CreateSampleSprites();    // UIObjects.cpp
    Entity& CreateUIImage(const std::string& sprite, glm::vec2 position, EntityID parent = 0); // UIObjects.cpp
    Entity& CreateUIText(const std::string& text, EntityID parent = 0);                        // UIObjects.cpp
    Entity& CreateUIGroup(const std::string& name, EntityID parent = 0);                       // UIObjects.cpp
    EntityID SelectedUIParent();   // selected UI element, to create new elements inside it
    void CreateSampleHUD();        // UIObjects.cpp
    void ImportFiles(const std::vector<std::filesystem::path>& files);      // ProjectPanel.cpp
    const std::vector<std::string>& ListImageAssets();                      // ProjectPanel.cpp (cached)
    struct ProjectEntry {
        std::filesystem::path path;
        bool directory = false;
    };
    const std::vector<ProjectEntry>& ListDirectoryCached(const std::filesystem::path& dir); // ProjectPanel.cpp
    void InvalidateProjectCache();

    // ---- Scene management
    void NewScene();
    void CreateDefaultScene();
    bool OpenScene(const std::filesystem::path& path);
    bool SaveScene();
    bool SaveSceneAs(const std::filesystem::path& path);
    void MarkDirty();
    // Runs `action` after asking to save unsaved changes (if any).
    void RequestSceneChange(std::function<void()> action);

    // ---- Play mode
    void Play();
    void Stop();
    void TogglePause();
    bool IsPlaying() const { return m_PlayState != PlayState::Edit; }

    // ---- Selection / editing helpers
    Entity* Selected();
    void Select(EntityID id);
    void DeleteSelected();
    void DuplicateSelected();
    void FocusSelected();
    Entity& CreateObject(const std::string& name);
    Entity& CreatePrimitiveObject(PrimitiveType type);
    EntityID PickEntity(const glm::vec3& origin, const glm::vec3& direction);

    std::filesystem::path AssetsDir() const { return m_ProjectDir / "Assets"; }
    std::string RelativeToAssets(const std::filesystem::path& path) const;

    std::unique_ptr<ImGuiLayer> m_ImGui;
    std::string m_IniPath;

    Scene m_Scene;
    PhysicsWorld m_Physics;
    EditorCamera m_EditorCamera;
    std::unique_ptr<RenderTarget> m_SceneTarget;
    std::unique_ptr<RenderTarget> m_GameTarget;
    VkExtent2D m_SceneViewSize{1280, 720};
    VkExtent2D m_GameViewSize{1280, 720};
    bool m_SceneViewVisible = false;
    bool m_GameViewVisible = false;
    bool m_SceneViewHovered = false;
    bool m_SceneViewFocused = false;

    // Selection & gizmo
    EntityID m_Selected = 0;
    ImGuizmo::OPERATION m_GizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE m_GizmoMode = ImGuizmo::LOCAL;
    bool m_Snap = false;
    bool m_ShowGrid = true;
    bool m_ShowColliders = false;
    bool m_ShowStats = true;
    glm::vec3 m_EulerCache{0.0f};
    EntityID m_EulerCacheEntity = 0;

    // Scene file
    std::filesystem::path m_ProjectDir;
    std::filesystem::path m_ScenePath;
    bool m_Dirty = false;

    // Play mode
    PlayState m_PlayState = PlayState::Edit;
    std::string m_EditSnapshot;
    bool m_FocusGameView = false;
    bool m_FocusSceneView = false;
    bool m_StepRequested = false;

    // Hierarchy rename
    EntityID m_RenamingEntity = 0;
    std::string m_RenameBuffer;

    // Project panel
    std::filesystem::path m_ProjectCurrentDir;
    float m_ProjectIconSize = 72.0f;
    std::filesystem::path m_ProjectRenaming;
    std::string m_ProjectRenameBuffer;
    std::filesystem::path m_ProjectPendingDelete;

    // Game view UI editing
    bool m_DraggingUI = false;

    // UI panel
    std::unique_ptr<RenderTarget> m_UITarget;
    VkExtent2D m_UITargetSize{1920, 1080};
    bool m_UIPanelVisible = false;
    bool m_UIPanelFocused = false;
    int m_UIResolution = 1; // 1920 x 1080
    int m_UIBackground = 0; // 0 = game camera, 1 = dark, 2 = light
    float m_UIZoom = 0.0f;  // 0 = fit
    glm::vec2 m_UIPan{0.0f};
    bool m_UISnap = true;
    bool m_UIShowAnchors = true;
    int m_UIDragHandle = -1; // -1 none, 0 move, 1..8 resize handles
    EntityID m_UIDragEntity = 0;
    CanvasRect m_UIDragStartRect;
    glm::vec2 m_UIDragStartMouse{0.0f};
    bool m_FocusUIPanel = false;

    // Caches (avoid touching the disk every frame)
    std::unordered_map<std::string, std::vector<ProjectEntry>> m_DirCache;
    double m_DirCacheTime = -1.0;
    std::vector<std::string> m_ImageAssetsCache;
    double m_ImageAssetsTime = -1.0;
    std::vector<LogEntry> m_ConsoleCache;
    int m_ConsoleCounts[3] = {0, 0, 0};

    // Console
    bool m_ConsoleShowInfo = true;
    bool m_ConsoleShowWarnings = true;
    bool m_ConsoleShowErrors = true;
    bool m_ConsoleAutoScroll = true;
    uint64_t m_ConsoleSeenVersion = 0;

    // Modals
    std::function<void()> m_PendingSceneAction;
    bool m_OpenUnsavedModal = false;
    bool m_OpenSaveAsModal = false;
    std::string m_SaveAsBuffer;
    bool m_OpenAboutModal = false;
    bool m_QuitAfterSave = false;
    bool m_LayoutReset = false;
};

} // namespace ze
