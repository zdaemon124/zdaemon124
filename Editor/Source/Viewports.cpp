#include "EditorApp.h"

#include "EditorUI.h"

#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <ZEngine/Core/Platform.h>
#include <ZEngine/Scene/UILayout.h>

namespace ze {

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
        if (!WorldToScreen(viewProj, e->transform.position, origin, size, p))
            continue;
        UI::Icon icon = e->camera ? UI::Icon::Camera : e->light ? UI::Icon::Light : UI::Icon::Empty;
        float r = 14.0f;
        if (e->id == m_Selected)
            draw->AddCircleFilled(p, r + 3.0f, IM_COL32(255, 140, 25, 120));
        UI::DrawIcon(draw, icon, {p.x - r, p.y - r}, {p.x + r, p.y + r}, IM_COL32(230, 230, 230, 255));
        if (e->light && e->id == m_Selected) {
            ImVec2 tip;
            if (WorldToScreen(viewProj, e->transform.position + e->transform.Forward() * 2.0f, origin, size, tip))
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
        glm::mat4 model = selected->transform.Matrix();

        bool snap = m_Snap || ImGui::GetIO().KeyCtrl;
        float snapValue = m_GizmoOperation == ImGuizmo::ROTATE ? 15.0f : m_GizmoOperation == ImGuizmo::SCALE ? 0.1f : 0.5f;
        float snapValues[3] = {snapValue, snapValue, snapValue};
        ImGuizmo::MODE mode = m_GizmoOperation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : m_GizmoMode;
        if (ImGuizmo::Manipulate(glm::value_ptr(cam.view), glm::value_ptr(projection), m_GizmoOperation, mode,
                                 glm::value_ptr(model), nullptr, snap ? snapValues : nullptr)) {
            glm::vec3 scale, translation, skew;
            glm::vec4 perspective;
            glm::quat rotation;
            if (glm::decompose(model, scale, rotation, translation, skew, perspective)) {
                selected->transform.position = translation;
                selected->transform.rotation = glm::normalize(rotation);
                selected->transform.scale = scale;
                MarkDirty();
            }
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
        bool hovered = ImGui::IsItemHovered();

        // Drop an image from the Project panel to create a UI Image at that spot.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ZE_ASSET")) {
                std::string asset(static_cast<const char*>(payload->Data));
                if (ImageIO::IsImageFile(Platform::Utf8ToPath(asset))) {
                    glm::vec2 screen(size.x, size.y);
                    float scale = UILayout::Scale(m_Scene.settings, screen);
                    ImVec2 mouse = ImGui::GetMousePos();
                    glm::vec2 local(mouse.x - origin.x, size.y - (mouse.y - origin.y));
                    CreateUIImage(asset, (local - screen * 0.5f) / scale);
                }
            }
            ImGui::EndDragDropTarget();
        }
        DrawGameViewUIOverlay(origin, size, hovered);
    } else {
        const char* text = "No cameras rendering. Add a Camera component to an object.";
        ImVec2 textSize = ImGui::CalcTextSize(text);
        ImGui::SetCursorPos({(size.x - textSize.x) * 0.5f, (size.y - textSize.y) * 0.5f});
        ImGui::TextDisabled("%s", text);
    }
    ImGui::End();
}

void EditorApp::DrawGameViewUIOverlay(ImVec2 origin, ImVec2 size, bool hovered)
{
    glm::vec2 screen(size.x, size.y);
    float scale = UILayout::Scale(m_Scene.settings, screen);
    auto rectOf = [&](Entity& e) -> UIRect* {
        if (e.uiImage) return &e.uiImage->rect;
        if (e.uiText) return &e.uiText->rect;
        return nullptr;
    };

    ImVec2 mouse = ImGui::GetMousePos();
    glm::vec2 local(mouse.x - origin.x, mouse.y - origin.y);

    // Click selects the top-most UI element under the cursor.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Entity* hit = nullptr;
        int bestOrder = std::numeric_limits<int>::min();
        for (auto& e : m_Scene.Entities()) {
            UIRect* rect = e->active ? rectOf(*e) : nullptr;
            if (rect && rect->order >= bestOrder && UILayout::Compute(*rect, screen, scale).Contains(local)) {
                hit = e.get();
                bestOrder = rect->order;
            }
        }
        if (hit) {
            Select(hit->id);
            m_DraggingUI = true;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        m_DraggingUI = false;

    Entity* selected = Selected();
    UIRect* rect = selected ? rectOf(*selected) : nullptr;
    if (!rect)
        return;

    // Drag to move (screen pixels -> reference pixels, y up).
    if (m_DraggingUI) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        if (delta.x != 0.0f || delta.y != 0.0f) {
            rect->position += glm::vec2(delta.x, -delta.y) / scale;
            MarkDirty();
        }
    }

    ScreenRect r = UILayout::Compute(*rect, screen, scale);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 a(origin.x + r.min.x, origin.y + r.min.y), b(origin.x + r.max.x, origin.y + r.max.y);
    draw->AddRect(a, b, IM_COL32(255, 140, 25, 255), 0.0f, 1.5f);
    ImVec2 pivot(a.x + (b.x - a.x) * rect->pivot.x, b.y - (b.y - a.y) * rect->pivot.y);
    draw->AddCircle(pivot, 5.0f, IM_COL32(80, 160, 255, 255), 12, 2.0f);
    ImVec2 anchor(origin.x + size.x * rect->anchor.x, origin.y + size.y * (1.0f - rect->anchor.y));
    draw->AddTriangleFilled({anchor.x, anchor.y - 7.0f}, {anchor.x - 6.0f, anchor.y + 4.0f},
                            {anchor.x + 6.0f, anchor.y + 4.0f}, IM_COL32(230, 230, 230, 200));
}

} // namespace ze
