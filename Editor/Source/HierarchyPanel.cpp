#include "EditorApp.h"

#include "EditorUI.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_map>

namespace ie {

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
    auto& entities = m_Scene.Entities();

    // Children lists (scene order) for the 3D tree; screen UI lives in the UI panel.
    std::unordered_map<EntityID, std::vector<Entity*>> children;
    for (auto& e : entities) {
        if (e->IsUIOnly())
            continue;
        EntityID parent = e->parent && m_Scene.Get(e->parent) ? e->parent : 0;
        children[parent].push_back(e.get());
    }

    // Deferred actions (never modify the scene while iterating it).
    EntityID toDelete = 0, toDuplicate = 0, toPrefab = 0, moveUp = 0, moveDown = 0, createChildOf = 0;
    std::pair<EntityID, EntityID> reparent{0, 0};
    bool reparentRequested = false;
    std::pair<std::string, EntityID> instantiate;

    auto acceptDrops = [&](EntityID target) {
        if (!ImGui::BeginDragDropTarget())
            return;
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("IE_ENTITY")) {
            reparent = {*static_cast<const EntityID*>(payload->Data), target};
            reparentRequested = true;
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("IE_ASSET"))
            instantiate = {static_cast<const char*>(payload->Data), target};
        ImGui::EndDragDropTarget();
    };

    auto drawRow = [&](Entity& e, bool hasChildren, bool flat) -> bool {
        float iconSize = ImGui::GetTextLineHeight();
        ImVec2 start = ImGui::GetCursorScreenPos();
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding |
                                   ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (!hasChildren || flat)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // leaves need no TreePop
        if (e.id == m_Selected)
            flags |= ImGuiTreeNodeFlags_Selected;

        if (m_RenamingEntity == e.id) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##rename", &m_RenameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                if (!m_RenameBuffer.empty()) {
                    e.name = m_RenameBuffer;
                    MarkDirty();
                }
                m_RenamingEntity = 0;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0)))
                m_RenamingEntity = 0;
            return false;
        }

        ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        if (!e.prefab.empty())
            textColor = ImVec4(0.45f, 0.7f, 1.0f, 1.0f); // prefab instances are blue, like Unity
        if (!e.active)
            textColor.w *= 0.45f;
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);
        bool open = ImGui::TreeNodeEx("##node", flags, "      %s", e.name.c_str());
        ImGui::PopStyleColor();
        float x = ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing() - 2.0f;
        float y = start.y + ImGui::GetStyle().FramePadding.y;
        UI::DrawIcon(ImGui::GetWindowDrawList(), EntityIcon(e), {x, y}, {x + iconSize, y + iconSize},
                     ImGui::GetColorU32(ImGuiCol_Text));

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
            Select(e.id);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            Select(e.id);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !hasChildren)
            FocusSelected();

        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("IE_ENTITY", &e.id, sizeof(EntityID));
            ImGui::Text("%s", e.name.c_str());
            ImGui::TextDisabled("Drop on an object to make it a child, on the Project panel to create a prefab");
            ImGui::EndDragDropSource();
        }
        acceptDrops(e.id);

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
            if (ImGui::MenuItem("Create Empty Child"))
                createChildOf = e.id;
            if (ImGui::MenuItem("Unparent", nullptr, false, e.parent != 0)) {
                reparent = {e.id, 0};
                reparentRequested = true;
            }
            if (ImGui::MenuItem("Move Up"))
                moveUp = e.id;
            if (ImGui::MenuItem("Move Down"))
                moveDown = e.id;
            ImGui::Separator();
            if (ImGui::MenuItem("Create Prefab"))
                toPrefab = e.id;
            if (!e.prefab.empty() && ImGui::MenuItem("Unpack Prefab")) {
                e.prefab.clear();
                MarkDirty();
            }
            if (ImGui::MenuItem(e.active ? "Deactivate" : "Activate")) {
                e.active = !e.active;
                MarkDirty();
            }
            ImGui::EndPopup();
        }
        return open;
    };

    std::string sceneName = m_ScenePath.empty() ? "Untitled" : m_ScenePath.stem().string();
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    bool rootOpen = ImGui::TreeNodeEx("##scene-root", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen,
                                      "%s%s", sceneName.c_str(), m_Dirty ? "*" : "");
    acceptDrops(0);
    if (rootOpen) {
        if (!filter.empty()) {
            // Search results as a flat list.
            for (auto& e : entities) {
                if (e->IsUIOnly() || !ContainsNoCase(e->name, filter))
                    continue;
                ImGui::PushID(int(e->id));
                drawRow(*e, false, true);
                ImGui::PopID();
            }
        } else {
            std::function<void(EntityID, int)> drawChildren = [&](EntityID parent, int depth) {
                auto it = children.find(parent);
                if (it == children.end() || depth > 64)
                    return;
                for (Entity* e : it->second) {
                    ImGui::PushID(int(e->id));
                    bool hasChildren = children.contains(e->id);
                    // Keep the selected object visible: open its parents.
                    if (m_Selected && m_Selected != e->id && m_Scene.IsDescendant(m_Selected, e->id) && m_RevealSelection)
                        ImGui::SetNextItemOpen(true);
                    if (drawRow(*e, hasChildren, false) && hasChildren) {
                        drawChildren(e->id, depth + 1);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            };
            drawChildren(0, 0);
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
            float x = ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing() - 2.0f;
            float y = start.y + ImGui::GetStyle().FramePadding.y;
            UI::DrawIcon(ImGui::GetWindowDrawList(), UI::Icon::Image, {x, y}, {x + iconSize, y + iconSize},
                         ImGui::GetColorU32(ImGuiCol_Text));
            if (ImGui::IsItemClicked())
                m_FocusUIPanel = true;
            UI::Tooltip("Screen-space UI is edited in the UI panel");
        }
        ImGui::TreePop();
    }
    m_RevealSelection = false;

    // Empty space: deselect, drop to the scene root, create.
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, 40.0f)));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        Select(0);
    acceptDrops(0);
    if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        DrawCreateMenuItems();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::End();

    // Apply deferred actions.
    if (reparentRequested && reparent.first != reparent.second) {
        if (m_Scene.SetParent(reparent.first, reparent.second, true)) {
            // Keep the child right after its new parent's subtree so the list order matches the tree.
            MarkDirty();
        }
    }
    if (!instantiate.first.empty())
        InstantiateAsset(instantiate.first, instantiate.second);
    if (createChildOf) {
        Entity& child = CreateObject("GameObject");
        child.parent = createChildOf;
    }
    if (moveUp || moveDown) {
        EntityID id = moveUp ? moveUp : moveDown;
        Entity* e = m_Scene.Get(id);
        // Swap with the previous / next sibling in the scene order.
        auto& list = children[e && e->parent ? e->parent : 0];
        auto it = std::find(list.begin(), list.end(), e);
        if (it != list.end()) {
            Entity* other = moveUp ? (it != list.begin() ? *(it - 1) : nullptr) : (it + 1 != list.end() ? *(it + 1) : nullptr);
            if (other) {
                m_Scene.Move(id, m_Scene.IndexOf(other->id));
                MarkDirty();
            }
        }
    }
    if (toPrefab)
        CreatePrefab(toPrefab, m_ProjectCurrentDir);
    if (toDuplicate) {
        Select(toDuplicate);
        DuplicateSelected();
    }
    if (toDelete) {
        Select(toDelete);
        DeleteSelected();
    }
}

} // namespace ie
