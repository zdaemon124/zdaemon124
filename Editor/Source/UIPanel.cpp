// UI panel: edits screen-space UI at a chosen target resolution, separate from the 3D scene view.
#include "EditorApp.h"

#include "EditorUI.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace ie {

namespace {

struct Resolution {
    const char* name;
    uint32_t width;
    uint32_t height;
};

const Resolution kResolutions[] = {
    {"Game view size", 0, 0},
    {"1920 x 1080   16:9 Full HD", 1920, 1080},
    {"2560 x 1440   16:9 QHD", 2560, 1440},
    {"3840 x 2160   16:9 4K", 3840, 2160},
    {"1280 x 720    16:9 HD", 1280, 720},
    {"2560 x 1080   21:9", 2560, 1080},
    {"3440 x 1440   21:9", 3440, 1440},
    {"1920 x 1200   16:10", 1920, 1200},
    {"1280 x 800    Steam Deck", 1280, 800},
    {"1024 x 768    4:3", 1024, 768},
};

// Resize handles, clockwise from the top-left corner (screen space).
enum Handle { None = -1, Move = 0, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };

bool AffectsLeft(int h) { return h == TopLeft || h == BottomLeft || h == Left; }
bool AffectsRight(int h) { return h == TopRight || h == Right || h == BottomRight; }
bool AffectsTop(int h) { return h == TopLeft || h == Top || h == TopRight; }
bool AffectsBottom(int h) { return h == BottomLeft || h == Bottom || h == BottomRight; }

ImGuiMouseCursor HandleCursor(int h)
{
    switch (h) {
    case TopLeft:
    case BottomRight: return ImGuiMouseCursor_ResizeNWSE;
    case TopRight:
    case BottomLeft: return ImGuiMouseCursor_ResizeNESW;
    case Top:
    case Bottom: return ImGuiMouseCursor_ResizeNS;
    case Left:
    case Right: return ImGuiMouseCursor_ResizeEW;
    default: return ImGuiMouseCursor_ResizeAll;
    }
}

} // namespace

UILayoutResult EditorApp::ReferenceLayout() const
{
    UILayoutResult layout;
    layout.Build(m_Scene, m_Scene.settings.uiReferenceResolution);
    return layout;
}

void EditorApp::ReparentUI(EntityID child, EntityID newParent)
{
    Entity* e = m_Scene.Get(child);
    if (!e || !e->rectTransform || child == newParent)
        return;
    if (newParent && m_Scene.IsUIDescendant(newParent, child))
        return; // would create a cycle
    UILayoutResult layout = ReferenceLayout();
    const CanvasRect* current = layout.Find(child);
    CanvasRect rect = current ? *current : CanvasRect{};
    e->rectTransform->parent = newParent;
    const CanvasRect* parentRect = newParent ? layout.Find(newParent) : nullptr;
    // Keep the element exactly where it was on screen.
    UILayout::SetFromCanvasRect(*e->rectTransform, rect, parentRect ? *parentRect : layout.CanvasBounds());
    MarkDirty();
}

void EditorApp::DrawUIPanel()
{
    if (m_FocusUIPanel) {
        ImGui::SetNextWindowFocus();
        m_FocusUIPanel = false;
    }
    // Existing layouts: open next to the Scene view the first time.
    if (ImGuiWindow* scene = ImGui::FindWindowByName("Scene"); scene && scene->DockId)
        ImGui::SetNextWindowDockID(scene->DockId, ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    bool open = ImGui::Begin("UI");
    ImGui::PopStyleVar();
    m_UIPanelVisible = open;
    m_UIPanelFocused = open && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!open) {
        ImGui::End();
        return;
    }

    // ---- Toolbar
    ImGui::SetCursorPos({6.0f, ImGui::GetCursorPosY() + 4.0f});
    EntityID parent = SelectedUIParent();
    if (ImGui::Button("+ Image"))
        CreateUIImage("", {0.0f, 0.0f}, parent);
    UI::Tooltip("New image (inside the selected element, if any)");
    ImGui::SameLine(0.0f, 2.0f);
    if (ImGui::Button("+ Text"))
        CreateUIText("New Text", parent);
    ImGui::SameLine(0.0f, 2.0f);
    if (ImGui::Button("+ Group"))
        CreateUIGroup("Group", parent);
    UI::Tooltip("Invisible container to lay out children together");

    ImGui::SameLine(0.0f, 16.0f);
    ImGui::SetNextItemWidth(230.0f);
    if (ImGui::BeginCombo("##res", kResolutions[m_UIResolution].name)) {
        for (int i = 0; i < int(std::size(kResolutions)); ++i)
            if (ImGui::Selectable(kResolutions[i].name, i == m_UIResolution)) {
                m_UIResolution = i;
                m_UIZoom = 0.0f;
                m_UIPan = {0.0f, 0.0f};
            }
        ImGui::EndCombo();
    }
    UI::Tooltip("Preview resolution: check that the layout adapts to every screen");
    ImGui::SameLine();
    const char* backgrounds[] = {"Game camera", "Dark", "Light"};
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("##bg", &m_UIBackground, backgrounds, 3);
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &m_UISnap);
    UI::Tooltip("Snap edges and centers to the parent, the screen and whole pixels");
    ImGui::SameLine();
    ImGui::Checkbox("Anchors", &m_UIShowAnchors);
    ImGui::SameLine();
    if (ImGui::Button("Fit")) {
        m_UIZoom = 0.0f;
        m_UIPan = {0.0f, 0.0f};
    }
    ImGui::SameLine();
    if (ImGui::Button("1:1"))
        m_UIZoom = 1.0f;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

    const Resolution& res = kResolutions[m_UIResolution];
    m_UITargetSize = res.width ? VkExtent2D{res.width, res.height} : m_GameViewSize;

    // ---- Layers + canvas
    ImGui::BeginChild("layers", ImVec2(230.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    DrawUILayers();
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("canvas", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawUICanvas();
    ImGui::EndChild();
    ImGui::End();
}

void EditorApp::DrawUILayers()
{
    ImGui::SetCursorPos({6.0f, 4.0f});
    ImGui::TextDisabled("LAYERS  (drag to nest)");

    // Children lists in draw order (back to front).
    std::unordered_map<EntityID, std::vector<Entity*>> children;
    for (auto& e : m_Scene.Entities()) {
        if (!e->rectTransform)
            continue;
        EntityID p = e->rectTransform->parent;
        const Entity* pe = p ? m_Scene.Get(p) : nullptr;
        children[pe && pe->rectTransform ? p : 0].push_back(e.get());
    }
    for (auto& [id, list] : children)
        std::stable_sort(list.begin(), list.end(),
                         [](Entity* a, Entity* b) { return a->rectTransform->order < b->rectTransform->order; });

    EntityID dropTarget = kInvalidEntity, dragged = kInvalidEntity;
    bool dropToScreen = false;
    EntityID toDelete = 0;

    auto acceptDrop = [&](EntityID target, bool screen) {
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("IE_ENTITY")) {
                dragged = *static_cast<const EntityID*>(payload->Data);
                dropTarget = target;
                dropToScreen = screen;
            }
            ImGui::EndDragDropTarget();
        }
    };

    ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
    bool rootOpen = ImGui::TreeNodeEx("Screen", rootFlags);
    acceptDrop(0, true);
    if (rootOpen) {
        std::function<void(EntityID, int)> drawChildren = [&](EntityID parentId, int depth) {
            auto it = children.find(parentId);
            if (it == children.end() || depth > 32)
                return;
            for (Entity* e : it->second) {
                ImGui::PushID(int(e->id));
                bool hasChildren = children.contains(e->id);

                // Visibility toggle ("eye").
                ImVec2 p = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("eye", ImVec2(16.0f, ImGui::GetFrameHeight()))) {
                    e->active = !e->active;
                    MarkDirty();
                }
                ImVec2 c(p.x + 8.0f, p.y + ImGui::GetFrameHeight() * 0.5f);
                ImGui::GetWindowDrawList()->AddCircle(c, 5.0f, ImGui::GetColorU32(ImGuiCol_TextDisabled), 12, 1.5f);
                if (e->active)
                    ImGui::GetWindowDrawList()->AddCircleFilled(c, 3.0f, ImGui::GetColorU32(ImGuiCol_Text));
                UI::Tooltip(e->active ? "Hide" : "Show");
                ImGui::SameLine(0.0f, 2.0f);

                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                                           ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding;
                if (!hasChildren)
                    flags |= ImGuiTreeNodeFlags_Leaf;
                if (e->id == m_Selected)
                    flags |= ImGuiTreeNodeFlags_Selected;
                const char* kind = e->uiText ? "T" : e->uiImage ? "I" : "G";
                if (!e->active)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                bool open = ImGui::TreeNodeEx("##node", flags, "[%s] %s", kind, e->name.c_str());
                if (!e->active)
                    ImGui::PopStyleColor();
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                    Select(e->id);
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("IE_ENTITY", &e->id, sizeof(EntityID));
                    ImGui::Text("Move '%s'", e->name.c_str());
                    ImGui::EndDragDropSource();
                }
                acceptDrop(e->id, false);
                if (ImGui::BeginPopupContextItem("ctx")) {
                    Select(e->id);
                    if (ImGui::MenuItem("Add Image inside"))
                        CreateUIImage("", {0.0f, 0.0f}, e->id);
                    if (ImGui::MenuItem("Add Text inside"))
                        CreateUIText("New Text", e->id);
                    if (ImGui::MenuItem("Add Group inside"))
                        CreateUIGroup("Group", e->id);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Bring to Front")) {
                        e->rectTransform->order += 1;
                        MarkDirty();
                    }
                    if (ImGui::MenuItem("Send to Back")) {
                        e->rectTransform->order -= 1;
                        MarkDirty();
                    }
                    if (ImGui::MenuItem("Move to Screen root", nullptr, false, e->rectTransform->parent != 0))
                        ReparentUI(e->id, 0);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete (with children)", "Del"))
                        toDelete = e->id;
                    ImGui::EndPopup();
                }
                if (open) {
                    drawChildren(e->id, depth + 1);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        };
        drawChildren(0, 0);
        ImGui::TreePop();
    }
    if (children.empty())
        ImGui::TextDisabled("  No UI yet. Use + Image / + Text,\n  or GameObject > UI > Sample HUD.");

    // Empty space: drop to the screen root, click to deselect.
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, 20.0f)));
    acceptDrop(0, true);

    if (dragged && (dropTarget || dropToScreen) && dragged != dropTarget)
        ReparentUI(dragged, dropToScreen ? 0 : dropTarget);
    if (toDelete) {
        Select(toDelete);
        DeleteSelected();
    }
}

void EditorApp::DrawUICanvas()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    if (avail.x < 4.0f || avail.y < 4.0f)
        return;
    ImGui::InvisibleButton("canvas", avail,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + avail.x, origin.y + avail.y}, IM_COL32(28, 28, 30, 255));

    // ---- View transform (fit / zoom / pan)
    glm::vec2 target(float(m_UITarget->extent.width), float(m_UITarget->extent.height));
    float fit = std::min(avail.x / target.x, avail.y / target.y) * 0.94f;
    float k = m_UIZoom > 0.0f ? m_UIZoom : fit;
    if (hovered && io.MouseWheel != 0.0f) {
        float newK = std::clamp(k * (io.MouseWheel > 0.0f ? 1.15f : 1.0f / 1.15f), 0.05f, 8.0f);
        // Zoom around the mouse cursor.
        glm::vec2 center(origin.x + avail.x * 0.5f + m_UIPan.x, origin.y + avail.y * 0.5f + m_UIPan.y);
        glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
        m_UIPan += (mouse - center) * (1.0f - newK / k);
        m_UIZoom = newK;
        k = newK;
    }
    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right))) {
        m_UIPan += glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
    }
    ImVec2 dmin(origin.x + avail.x * 0.5f + m_UIPan.x - target.x * k * 0.5f,
                origin.y + avail.y * 0.5f + m_UIPan.y - target.y * k * 0.5f);
    ImVec2 dmax(dmin.x + target.x * k, dmin.y + target.y * k);

    draw->PushClipRect(origin, {origin.x + avail.x, origin.y + avail.y}, true);
    draw->AddRectFilled({dmin.x + 4, dmin.y + 4}, {dmax.x + 4, dmax.y + 4}, IM_COL32(0, 0, 0, 90));
    draw->AddImage(ImTextureRef(m_ImGui->Texture(*m_UITarget)), dmin, dmax);
    draw->AddRect(dmin, dmax, IM_COL32(90, 90, 95, 255));

    UILayoutResult layout;
    layout.Build(m_Scene, target);
    auto toDisplay = [&](glm::vec2 targetPx) { return ImVec2(dmin.x + targetPx.x * k, dmin.y + targetPx.y * k); };
    auto canvasToDisplay = [&](glm::vec2 c) { return toDisplay({c.x * layout.Scale(), target.y - c.y * layout.Scale()}); };
    glm::vec2 mouseCanvas = layout.ScreenToCanvas({(io.MousePos.x - dmin.x) / k, (io.MousePos.y - dmin.y) / k});

    // Hover outline.
    const Entity* hoveredEntity = nullptr;
    if (hovered) {
        const auto& order = layout.DrawOrder();
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            const CanvasRect& r = *layout.Find((*it)->id);
            if (mouseCanvas.x >= r.min.x && mouseCanvas.x <= r.max.x && mouseCanvas.y >= r.min.y && mouseCanvas.y <= r.max.y) {
                hoveredEntity = *it;
                break;
            }
        }
    }
    if (hoveredEntity && hoveredEntity->id != m_Selected) {
        const CanvasRect& r = *layout.Find(hoveredEntity->id);
        draw->AddRect(canvasToDisplay({r.min.x, r.max.y}), canvasToDisplay({r.max.x, r.min.y}), IM_COL32(120, 170, 255, 160));
    }

    // ---- Selection: handles, anchors, pivot
    Entity* selected = Selected();
    const CanvasRect* selRect = selected && selected->rectTransform ? layout.Find(selected->id) : nullptr;
    ImVec2 handlePos[9];
    int hoveredHandle = None;
    if (selRect) {
        ImVec2 a = canvasToDisplay({selRect->min.x, selRect->max.y}); // top-left on screen
        ImVec2 b = canvasToDisplay({selRect->max.x, selRect->min.y}); // bottom-right
        ImVec2 m((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        handlePos[TopLeft] = a;
        handlePos[Top] = {m.x, a.y};
        handlePos[TopRight] = {b.x, a.y};
        handlePos[Right] = {b.x, m.y};
        handlePos[BottomRight] = b;
        handlePos[Bottom] = {m.x, b.y};
        handlePos[BottomLeft] = {a.x, b.y};
        handlePos[Left] = {a.x, m.y};
        if (hovered)
            for (int h = TopLeft; h <= Left; ++h)
                if (std::abs(io.MousePos.x - handlePos[h].x) <= 6.0f && std::abs(io.MousePos.y - handlePos[h].y) <= 6.0f)
                    hoveredHandle = h;

        if (m_UIShowAnchors) {
            CanvasRect parent = layout.ParentRect(*selected);
            const RectTransform& rt = *selected->rectTransform;
            glm::vec2 amin = parent.min + rt.anchorMin * parent.Size();
            glm::vec2 amax = parent.min + rt.anchorMax * parent.Size();
            draw->AddRect(canvasToDisplay({parent.min.x, parent.max.y}), canvasToDisplay({parent.max.x, parent.min.y}),
                          IM_COL32(255, 255, 255, 50));
            ImU32 anchorColor = IM_COL32(240, 240, 240, 230);
            for (glm::vec2 corner : {amin, amax, glm::vec2(amin.x, amax.y), glm::vec2(amax.x, amin.y)}) {
                ImVec2 p = canvasToDisplay(corner);
                draw->AddTriangleFilled({p.x, p.y}, {p.x - 5, p.y - 9}, {p.x + 5, p.y - 9}, anchorColor);
            }
            if (amin != amax)
                draw->AddRect(canvasToDisplay({amin.x, amax.y}), canvasToDisplay({amax.x, amin.y}), IM_COL32(240, 240, 240, 90));
        }
        draw->AddRect(a, b, IM_COL32(255, 140, 25, 255), 0.0f, 1.5f);
        for (int h = TopLeft; h <= Left; ++h) {
            ImVec2 p = handlePos[h];
            draw->AddRectFilled({p.x - 4, p.y - 4}, {p.x + 4, p.y + 4}, h == hoveredHandle ? IM_COL32(255, 200, 80, 255) : IM_COL32(255, 255, 255, 255));
            draw->AddRect({p.x - 4, p.y - 4}, {p.x + 4, p.y + 4}, IM_COL32(40, 40, 40, 255));
        }
        glm::vec2 pivot = selRect->min + selected->rectTransform->pivot * (selRect->max - selRect->min);
        draw->AddCircle(canvasToDisplay(pivot), 5.0f, IM_COL32(80, 160, 255, 255), 12, 2.0f);
    }

    // ---- Start / continue / end dragging
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (selRect && hoveredHandle != None) {
            m_UIDragHandle = hoveredHandle;
        } else if (hoveredEntity) {
            Select(hoveredEntity->id);
            m_UIDragHandle = Move;
        } else {
            Select(0);
            m_UIDragHandle = None;
        }
        Entity* e = Selected();
        const CanvasRect* r = e && e->rectTransform ? layout.Find(e->id) : nullptr;
        if (r) {
            m_UIDragEntity = e->id;
            m_UIDragStartRect = *r;
            m_UIDragStartMouse = mouseCanvas;
        } else {
            m_UIDragHandle = None;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        m_UIDragHandle = None;
    if (hoveredHandle != None || m_UIDragHandle > Move)
        ImGui::SetMouseCursor(HandleCursor(m_UIDragHandle > Move ? m_UIDragHandle : hoveredHandle));

    std::vector<std::pair<ImVec2, ImVec2>> guides;
    Entity* dragEntity = m_UIDragHandle != None ? m_Scene.Get(m_UIDragEntity) : nullptr;
    if (dragEntity && dragEntity->rectTransform && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
        glm::vec2 delta = mouseCanvas - m_UIDragStartMouse;
        CanvasRect r = m_UIDragStartRect;
        const int h = m_UIDragHandle;
        if (h == Move) {
            r.min += delta;
            r.max += delta;
        } else {
            if (AffectsLeft(h)) r.min.x = std::min(r.min.x + delta.x, r.max.x - 1.0f);
            if (AffectsRight(h)) r.max.x = std::max(r.max.x + delta.x, r.min.x + 1.0f);
            if (AffectsTop(h)) r.max.y = std::max(r.max.y + delta.y, r.min.y + 1.0f);
            if (AffectsBottom(h)) r.min.y = std::min(r.min.y + delta.y, r.max.y - 1.0f);
        }

        CanvasRect parent = layout.ParentRect(*dragEntity);
        if (m_UISnap) {
            // Snap edges / centers to the parent, the screen and whole units.
            CanvasRect screen = layout.CanvasBounds();
            float threshold = 7.0f / (k * layout.Scale());
            for (int axis = 0; axis < 2; ++axis) {
                std::vector<float> lines = {parent.min[axis], (parent.min[axis] + parent.max[axis]) * 0.5f, parent.max[axis],
                                            screen.min[axis], (screen.min[axis] + screen.max[axis]) * 0.5f, screen.max[axis]};
                bool moveMin = h == Move || (axis == 0 ? AffectsLeft(h) : AffectsBottom(h));
                bool moveMax = h == Move || (axis == 0 ? AffectsRight(h) : AffectsTop(h));
                float best = threshold, bestShift = 0.0f, bestLine = 0.0f;
                bool found = false;
                auto consider = [&](float value) {
                    for (float line : lines)
                        if (std::abs(line - value) < best) {
                            best = std::abs(line - value);
                            bestShift = line - value;
                            bestLine = line;
                            found = true;
                        }
                };
                if (moveMin) consider(r.min[axis]);
                if (moveMax) consider(r.max[axis]);
                if (h == Move) consider((r.min[axis] + r.max[axis]) * 0.5f);
                if (found) {
                    if (moveMin) r.min[axis] += bestShift;
                    if (moveMax) r.max[axis] += bestShift;
                    CanvasRect all = layout.CanvasBounds();
                    if (axis == 0)
                        guides.push_back({canvasToDisplay({bestLine, all.max.y}), canvasToDisplay({bestLine, all.min.y})});
                    else
                        guides.push_back({canvasToDisplay({all.min.x, bestLine}), canvasToDisplay({all.max.x, bestLine})});
                } else {
                    if (moveMin && moveMax) {
                        float shift = std::round(r.min[axis]) - r.min[axis];
                        r.min[axis] += shift;
                        r.max[axis] += shift;
                    } else {
                        if (moveMin) r.min[axis] = std::round(r.min[axis]);
                        if (moveMax) r.max[axis] = std::round(r.max[axis]);
                    }
                }
            }
        }
        UILayout::SetFromCanvasRect(*dragEntity->rectTransform, r, parent);
        MarkDirty();
    }
    for (const auto& [a, b] : guides)
        draw->AddLine(a, b, IM_COL32(255, 60, 200, 220), 1.0f);

    // Arrow keys nudge the selection (Shift = 10).
    if (m_UIPanelFocused && selected && selected->rectTransform && !io.WantTextInput) {
        float step = io.KeyShift ? 10.0f : 1.0f;
        glm::vec2 nudge(0.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) nudge.x -= step;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) nudge.x += step;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) nudge.y += step;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) nudge.y -= step;
        if (nudge != glm::vec2(0.0f)) {
            selected->rectTransform->position += nudge;
            MarkDirty();
        }
    }

    // Info line.
    std::string info = std::format("{} x {}   scale {:.2f}   zoom {:.0f}%", m_UITarget->extent.width, m_UITarget->extent.height,
                                   layout.Scale(), k * 100.0f);
    draw->AddText({origin.x + 8.0f, origin.y + avail.y - 20.0f}, IM_COL32(170, 170, 170, 255), info.c_str());
    if (selected && selected->rectTransform) {
        std::string sel = std::format("{}   pos {:.0f}, {:.0f}   size {:.0f} x {:.0f}", selected->name,
                                      selected->rectTransform->position.x, selected->rectTransform->position.y,
                                      selRect ? selRect->Size().x : 0.0f, selRect ? selRect->Size().y : 0.0f);
        draw->AddText({origin.x + 8.0f, origin.y + 6.0f}, IM_COL32(230, 230, 230, 255), sel.c_str());
    }
    draw->PopClipRect();
}

} // namespace ie
