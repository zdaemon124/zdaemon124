#include "EditorApp.h"

#include "EditorUI.h"

#include <algorithm>
#include <cctype>

namespace ze {

namespace {

UI::Icon EntityIcon(const Entity& e)
{
    if (e.camera) return UI::Icon::Camera;
    if (e.IsUI()) return UI::Icon::Image;
    if (e.light) return UI::Icon::Light;
    if (e.meshRenderer) return UI::Icon::Cube;
    return UI::Icon::Empty;
}

bool ContainsNoCase(const std::string& text, const std::string& pattern)
{
    auto it = std::search(text.begin(), text.end(), pattern.begin(), pattern.end(),
                          [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
    return it != text.end();
}

} // namespace

void EditorApp::DrawCreateMenuItems()
{
    if (ImGui::MenuItem("Create Empty"))
        CreateObject("GameObject").transform.position =
            m_EditorCamera.transform.position + m_EditorCamera.transform.Forward() * 8.0f;
    if (ImGui::BeginMenu("3D Object")) {
        for (int i = 0; i < int(PrimitiveType::Count); ++i) {
            auto type = PrimitiveType(i);
            if (ImGui::MenuItem(PrimitiveName(type)))
                CreatePrimitiveObject(type);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Light")) {
        if (ImGui::MenuItem("Directional Light")) {
            Entity& e = CreateObject("Directional Light");
            e.light = LightComponent{};
            e.transform.position = {0.0f, 3.0f, 0.0f};
            e.transform.SetEulerAngles({50.0f, -30.0f, 0.0f});
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("UI")) {
        if (ImGui::MenuItem("Image"))
            CreateUIImage("", {0.0f, 0.0f}, SelectedUIParent());
        if (ImGui::MenuItem("Text"))
            CreateUIText("New Text", SelectedUIParent());
        if (ImGui::MenuItem("Group (empty container)"))
            CreateUIGroup("Group", SelectedUIParent());
        ImGui::Separator();
        if (ImGui::MenuItem("Sample HUD (orbs + action bar)"))
            CreateSampleHUD();
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Camera")) {
        Entity& e = CreateObject("Camera");
        e.camera = CameraComponent{};
        e.transform = m_EditorCamera.transform;
        e.transform.scale = glm::vec3(1.0f);
    }
}

void EditorApp::DrawHierarchy()
{
    bool open = ImGui::Begin("Hierarchy");
    m_HierarchyFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!open) {
        ImGui::End();
        return;
    }

    static std::string filter;
    if (ImGui::Button("+"))
        ImGui::OpenPopup("CreateMenu");
    UI::Tooltip("Create object");
    if (ImGui::BeginPopup("CreateMenu")) {
        DrawCreateMenuItems();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Search...", &filter);
    ImGui::Separator();

    ImGui::BeginChild("entities");
    // Scene root node.
    std::string sceneName = m_ScenePath.empty() ? "Untitled" : m_ScenePath.stem().string();
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    bool rootOpen = ImGui::TreeNodeEx("##scene-root", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen,
                                      "%s%s", sceneName.c_str(), m_Dirty ? "*" : "");
    if (rootOpen) {
        EntityID toDelete = 0, toDuplicate = 0, dragged = 0;
        int dropIndex = -1;
        auto& entities = m_Scene.Entities();
        for (size_t i = 0; i < entities.size(); ++i) {
            Entity& e = *entities[i];
            if (e.IsUIOnly())
                continue; // screen UI lives in the UI panel, not in the scene list
            if (!filter.empty() && !ContainsNoCase(e.name, filter))
                continue;
            ImGui::PushID(int(e.id));

            float iconSize = ImGui::GetTextLineHeight();
            ImVec2 start = ImGui::GetCursorScreenPos();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                       ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
            if (e.id == m_Selected)
                flags |= ImGuiTreeNodeFlags_Selected;

            if (m_RenamingEntity == e.id) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText("##rename", &m_RenameBuffer,
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                    if (!m_RenameBuffer.empty()) {
                        e.name = m_RenameBuffer;
                        MarkDirty();
                    }
                    m_RenamingEntity = 0;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0)))
                    m_RenamingEntity = 0;
                ImGui::PopID();
                continue;
            }

            if (!e.active)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TreeNodeEx("##node", flags, "      %s", e.name.c_str());
            if (!e.active)
                ImGui::PopStyleColor();
            float framePad = ImGui::GetStyle().FramePadding.y;
            UI::DrawIcon(ImGui::GetWindowDrawList(), EntityIcon(e),
                         {start.x + ImGui::GetTreeNodeToLabelSpacing() - 2.0f, start.y + framePad},
                         {start.x + ImGui::GetTreeNodeToLabelSpacing() - 2.0f + iconSize, start.y + framePad + iconSize},
                         ImGui::GetColorU32(ImGuiCol_Text));

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
                Select(e.id);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                FocusSelected();

            // Drag to reorder.
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("ZE_ENTITY", &e.id, sizeof(EntityID));
                ImGui::TextUnformatted(e.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ZE_ENTITY")) {
                    dragged = *static_cast<const EntityID*>(payload->Data);
                    dropIndex = int(i);
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopupContextItem("EntityContext")) {
                if (ImGui::MenuItem("Rename", "F2")) {
                    m_RenamingEntity = e.id;
                    m_RenameBuffer = e.name;
                }
                if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                    toDuplicate = e.id;
                if (ImGui::MenuItem("Delete", "Del"))
                    toDelete = e.id;
                ImGui::Separator();
                if (ImGui::MenuItem(e.active ? "Deactivate" : "Activate")) {
                    e.active = !e.active;
                    MarkDirty();
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        size_t uiCount = std::count_if(entities.begin(), entities.end(), [](const auto& e) { return e->IsUIOnly(); });
        if (uiCount > 0) {
            ImGui::Spacing();
            ImVec2 start = ImGui::GetCursorScreenPos();
            float iconSize = ImGui::GetTextLineHeight();
            Entity* sel = Selected();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                       ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
            if (sel && sel->IsUIOnly())
                flags |= ImGuiTreeNodeFlags_Selected;
            ImGui::TreeNodeEx("##screen-ui", flags, "      Screen UI  (%zu)", uiCount);
            UI::DrawIcon(ImGui::GetWindowDrawList(), UI::Icon::Image,
                         {start.x + ImGui::GetTreeNodeToLabelSpacing() - 2.0f, start.y + ImGui::GetStyle().FramePadding.y},
                         {start.x + ImGui::GetTreeNodeToLabelSpacing() - 2.0f + iconSize,
                          start.y + ImGui::GetStyle().FramePadding.y + iconSize},
                         ImGui::GetColorU32(ImGuiCol_Text));
            if (ImGui::IsItemClicked())
                m_FocusUIPanel = true;
            UI::Tooltip("Screen-space UI is edited in the UI panel");
        }

        if (dragged && dropIndex >= 0) {
            m_Scene.Move(dragged, dropIndex);
            MarkDirty();
        }
        if (toDuplicate) {
            Select(toDuplicate);
            DuplicateSelected();
        }
        if (toDelete) {
            Select(toDelete);
            DeleteSelected();
        }
        ImGui::TreePop();
    }

    // Empty space: deselect / create.
    ImGui::Dummy(ImGui::GetContentRegionAvail());
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        Select(0);
    if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        DrawCreateMenuItems();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace ze
