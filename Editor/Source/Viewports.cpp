#include "EditorApp.h"

#include "EditorUI.h"

#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <IndeetsEngine/Core/Platform.h>
#include <IndeetsEngine/Scene/UILayout.h>

namespace ie {

namespace {

VkExtent2D ToExtent(ImVec2 size)
{
    return {uint32_t(std::max(1.0f, size.x)), uint32_t(std::max(1.0f, size.y))};
}

// World position -> screen position inside the viewport; false if behind the camera.
bool WorldToScreen(const glm::mat4& viewProj, const glm::vec3& p, ImVec2 origin, ImVec2 size, ImVec2& out)
{
    glm::vec4 clip = viewProj * glm::vec4(p, 1.0f);
    if (clip.w <= 0.01f)
        return false;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    out = {origin.x + (ndc.x * 0.5f + 0.5f) * size.x, origin.y + (ndc.y * 0.5f + 0.5f) * size.y};
    return true;
}

} // namespace

void EditorApp::DrawSceneView()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (m_FocusSceneView) {
        ImGui::SetNextWindowFocus();
        m_FocusSceneView = false;
    }
    bool open = ImGui::Begin("Scene");
    ImGui::PopStyleVar();
    m_SceneViewVisible = open;
    m_SceneViewHovered = false;
    m_SceneViewFocused = ImGui::IsWindowFocused();
    if (!open) {
        ImGui::End();
        return;
    }

    DrawSceneToolbar();

    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    m_SceneViewSize = ToExtent(size);
    ImGui::Image(ImTextureRef(m_ImGui->Texture(*m_SceneTarget)), size);
    m_SceneViewHovered = ImGui::IsItemHovered();

    // Drop a model or prefab from the Project panel: place it where the cursor hits the ground plane.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("IE_ASSET")) {
            std::string asset(static_cast<const char*>(payload->Data));
            ImVec2 mouse = ImGui::GetMousePos();
            float aspectDrop = size.x / std::max(size.y, 1.0f);
            CameraData camDrop = m_EditorCamera.Data(aspectDrop);
            glm::mat4 inv = glm::inverse(camDrop.projection * camDrop.view);
            glm::vec2 ndc{(mouse.x - origin.x) / size.x * 2.0f - 1.0f, (mouse.y - origin.y) / size.y * 2.0f - 1.0f};
            glm::vec4 n = inv * glm::vec4(ndc, 0.0f, 1.0f), f = inv * glm::vec4(ndc, 1.0f, 1.0f);
            glm::vec3 ro = glm::vec3(n) / n.w, rd = glm::normalize(glm::vec3(f) / f.w - ro);
            glm::vec3 point = ro + rd * 8.0f;
            if (rd.y < -1e-4f) {
                float t = -ro.y / rd.y;
                if (t > 0.0f && t < 500.0f)
                    point = ro + rd * t;
            }
            InstantiateAsset(asset, 0, &point);
        }
        ImGui::EndDragDropTarget();
    }
    // Right/middle clicks in the view should focus it (for W/E/R, F, Delete shortcuts).
    if (m_SceneViewHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
        ImGui::SetWindowFocus();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    float aspect = size.x / std::max(size.y, 1.0f);
    CameraData cam = m_EditorCamera.Data(aspect);
    glm::mat4 viewProj = cam.projection * cam.view;

    // Icons for objects without a visible mesh (lights, cameras, empties).
    for (const auto& e : m_Scene.Entities()) {
        if (!e->active || e->meshRenderer || e->IsUIOnly())
            continue;
        ImVec2 p;
        glm::vec3 worldPos(e->world[3]);
        if (!WorldToScreen(viewProj, worldPos, origin, size, p))
            continue;
        UI::Icon icon = e->camera ? UI::Icon::Camera : e->light ? UI::Icon::Light : UI::Icon::Empty;
        float r = 14.0f;
        if (e->id == m_Selected)
            draw->AddCircleFilled(p, r + 3.0f, IM_COL32(255, 140, 25, 120));
        UI::DrawIcon(draw, icon, {p.x - r, p.y - r}, {p.x + r, p.y + r}, IM_COL32(230, 230, 230, 255));
        if (e->light && e->id == m_Selected) {
            ImVec2 tip;
            glm::vec3 forward = glm::normalize(glm::vec3(e->world[2]));
            if (WorldToScreen(viewProj, worldPos + forward * 2.0f, origin, size, tip))
                draw->AddLine(p, tip, IM_COL32(250, 210, 80, 255), 2.0f);
        }
    }

    // Transform gizmo.
    bool gizmoOver = false;
    Entity* selected = Selected();
    if (selected && !selected->IsUIOnly() && !m_EditorCamera.IsControlling()) {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
        glm::mat4 projection = m_EditorCamera.lens.ProjectionYUp(aspect);
        glm::mat4 model = selected->world;

        bool snap = m_Snap || ImGui::GetIO().KeyCtrl;
        float snapValue = m_GizmoOperation == ImGuizmo::ROTATE ? 15.0f : m_GizmoOperation == ImGuizmo::SCALE ? 0.1f : 0.5f;
        float snapValues[3] = {snapValue, snapValue, snapValue};
        ImGuizmo::MODE mode = m_GizmoOperation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : m_GizmoMode;
        if (ImGuizmo::Manipulate(glm::value_ptr(cam.view), glm::value_ptr(projection), m_GizmoOperation, mode,
                                 glm::value_ptr(model), nullptr, snap ? snapValues : nullptr)) {
            m_Scene.SetWorldMatrix(*selected, model);
            m_Scene.UpdateWorldTransforms();
            MarkDirty();
        }
        gizmoOver = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
    }

    // Click to select.
    if (m_SceneViewHovered && !gizmoOver && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ImVec2 mouse = ImGui::GetMousePos();
        glm::vec2 ndc{(mouse.x - origin.x) / size.x * 2.0f - 1.0f, (mouse.y - origin.y) / size.y * 2.0f - 1.0f};
        glm::mat4 inv = glm::inverse(viewProj);
        glm::vec4 nearPoint = inv * glm::vec4(ndc, 0.0f, 1.0f);
        glm::vec4 farPoint = inv * glm::vec4(ndc, 1.0f, 1.0f);
        glm::vec3 rayOrigin = glm::vec3(nearPoint) / nearPoint.w;
        glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint) / farPoint.w - rayOrigin);
        Select(PickEntity(rayOrigin, rayDir));
    }

    // Overlay: stats + camera hint.
    if (m_ShowStats) {
        ImGui::SetCursorScreenPos({origin.x + 8.0f, origin.y + 6.0f});
        DrawStats();
    }
    ImGui::End();
}

void EditorApp::DrawSceneToolbar()
{
    // Tool strip at the top of the Scene view (right of the Hierarchy panel).
    ImGui::SetCursorPos({6.0f, ImGui::GetCursorPosY() + 4.0f});
    ImVec2 button(28.0f, 24.0f);
    if (UI::IconButton("move", UI::Icon::Move, m_GizmoOperation == ImGuizmo::TRANSLATE, "Move (W)", button))
        m_GizmoOperation = ImGuizmo::TRANSLATE;
    ImGui::SameLine(0.0f, 2.0f);
    if (UI::IconButton("rotate", UI::Icon::Rotate, m_GizmoOperation == ImGuizmo::ROTATE, "Rotate (E)", button))
        m_GizmoOperation = ImGuizmo::ROTATE;
    ImGui::SameLine(0.0f, 2.0f);
    if (UI::IconButton("scale", UI::Icon::Scale, m_GizmoOperation == ImGuizmo::SCALE, "Scale (R)", button))
        m_GizmoOperation = ImGuizmo::SCALE;
    ImGui::SameLine(0.0f, 10.0f);
    if (ImGui::Button(m_GizmoMode == ImGuizmo::LOCAL ? "Local" : "Global", ImVec2(60.0f, 24.0f)))
        m_GizmoMode = m_GizmoMode == ImGuizmo::LOCAL ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    UI::Tooltip("Gizmo space");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Checkbox("Snap", &m_Snap);
    UI::Tooltip("Snap: 0.5 m / 15 deg / 0.1 scale (hold Ctrl for temporary snap)");

    float right = ImGui::GetWindowWidth() - 250.0f;
    if (right > ImGui::GetCursorPosX() + 300.0f) {
        ImGui::SameLine(right);
    } else {
        ImGui::SameLine();
    }
    ImGui::Checkbox("Grid", &m_ShowGrid);
    ImGui::SameLine();
    ImGui::Checkbox("Colliders", &m_ShowColliders);
    ImGui::SameLine();
    ImGui::Checkbox("Stats", &m_ShowStats);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
}

void EditorApp::DrawStats()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.45f));
    ImGui::BeginChild("stats", ImVec2(230.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
    ImGui::Text("%.0f FPS  (%.2f ms)", Fps(), Fps() > 0.0f ? 1000.0f / Fps() : 0.0f);
    ImGui::Text("Objects: %zu", m_Scene.Entities().size());
    if (IsPlaying())
        ImGui::Text("Physics bodies: %zu", m_Physics.BodyCount());
    ImGui::TextDisabled("Camera speed: %.1f", m_EditorCamera.moveSpeed);
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void EditorApp::DrawGameView()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (m_FocusGameView) {
        ImGui::SetNextWindowFocus();
        m_FocusGameView = false;
    }
    bool open = ImGui::Begin("Game");
    ImGui::PopStyleVar();
    m_GameViewVisible = open;
    if (!open) {
        ImGui::End();
        return;
    }
    ImVec2 size = ImGui::GetContentRegionAvail();
    m_GameViewSize = ToExtent(size);
    if (m_Scene.MainCamera()) {
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Image(ImTextureRef(m_ImGui->Texture(*m_GameTarget)), size);

        // The Game view only shows the game, as in Unity: UI is edited in the UI
        // panel. Dropping an image here outside Play Mode still creates a UI Image.
        if (!IsPlaying() && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("IE_ASSET")) {
                std::string asset(static_cast<const char*>(payload->Data));
                if (ImageIO::IsImageFile(Platform::Utf8ToPath(asset))) {
                    glm::vec2 screen(size.x, size.y);
                    float scale = UILayout::Scale(m_Scene.settings, screen);
                    ImVec2 mouse = ImGui::GetMousePos();
                    glm::vec2 local(mouse.x - origin.x, size.y - (mouse.y - origin.y));
                    CreateUIImage(asset, (local - screen * 0.5f) / scale, 0);
                }
            }
            ImGui::EndDragDropTarget();
        }
    } else {
        const char* text = "No cameras rendering. Add a Camera component to an object.";
        ImVec2 textSize = ImGui::CalcTextSize(text);
        ImGui::SetCursorPos({(size.x - textSize.x) * 0.5f, (size.y - textSize.y) * 0.5f});
        ImGui::TextDisabled("%s", text);
    }
    ImGui::End();
}

} // namespace ie
