#include "EditorApp.h"

#include "EditorUI.h"

#include <IndeetsEngine/Core/Platform.h>

#include <algorithm>
#include <optional>

namespace ie {

namespace {

// Collapsible component section with a remove ("x") button. Returns true when expanded.
bool ComponentHeader(const char* label, bool& keep)
{
    ImGui::Spacing();
    keep = true;
    return ImGui::CollapsingHeader(label, &keep, ImGuiTreeNodeFlags_DefaultOpen);
}

} // namespace

void EditorApp::DrawInspector()
{
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }
    if (!m_SelectedAsset.empty()) {
        DrawAssetInspector();
    } else if (Entity* e = Selected()) {
        if (e->prefab.size() && UI::BeginProperties("prefab")) {
            UI::PropertyLabel("Prefab");
            ImGui::TextColored(ImVec4(0.45f, 0.7f, 1.0f, 1.0f), "%s", e->prefab.c_str());
            UI::EndProperties();
            if (ImGui::SmallButton("Select Asset"))
                SelectAsset(e->prefab);
            ImGui::SameLine();
            if (ImGui::SmallButton("Apply"))
                ApplyPrefab(e->id);
            UI::Tooltip("Save this instance (with its children) into the prefab and update all instances");
            ImGui::SameLine();
            if (ImGui::SmallButton("Revert"))
                RevertPrefab(e->id);
            UI::Tooltip("Discard changes of this instance");
            ImGui::SameLine();
            if (ImGui::SmallButton("Unpack")) {
                e->prefab.clear();
                MarkDirty();
            }
            UI::Tooltip("Turn into regular objects (no link to the prefab)");
            ImGui::Separator();
        }
        if (e && DrawEntityInspector(*e))
            MarkDirty();
        if (IsPlaying())
            ImGui::TextDisabled("Play mode: changes will be reverted when you press Stop.");
    } else {
        DrawSceneSettings();
    }
    ImGui::End();
}

bool EditorApp::DrawEntityInspector(Entity& entity)
{
    Entity* e = &entity;
    bool changed = false;
    ImGui::PushID(int(e->id));

    // Header: active toggle + name.
    UI::DrawIcon(ImGui::GetWindowDrawList(),
                 e->camera ? UI::Icon::Camera : e->light ? UI::Icon::Light : e->meshRenderer ? UI::Icon::Cube
                 : e->IsUI() ? UI::Icon::Image : UI::Icon::Empty,
                 ImGui::GetCursorScreenPos(),
                 {ImGui::GetCursorScreenPos().x + ImGui::GetFrameHeight(), ImGui::GetCursorScreenPos().y + ImGui::GetFrameHeight()},
                 ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::Dummy({ImGui::GetFrameHeight(), ImGui::GetFrameHeight()});
    ImGui::SameLine();
    changed |= ImGui::Checkbox("##active", &e->active);
    UI::Tooltip("Active");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= ImGui::InputText("##name", &e->name);
    ImGui::TextDisabled("ID %u", e->id);

    // Transform (hidden for pure UI elements, which are placed by their rect).
    if (!e->IsUIOnly() && ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Keep the Euler angles the user typed instead of re-deriving them from the quaternion every frame.
        static glm::quat cachedRotation{1.0f, 0.0f, 0.0f, 0.0f};
        if (m_EulerCacheEntity != e->id || cachedRotation != e->transform.rotation) {
            m_EulerCache = e->transform.EulerAngles();
            cachedRotation = e->transform.rotation;
            m_EulerCacheEntity = e->id;
        }
        if (e->parent)
            if (const Entity* parent = m_Scene.Get(e->parent))
                ImGui::TextDisabled("Local to parent: %s", parent->name.c_str());
        if (UI::BeginProperties("transform")) {
            changed |= UI::PropertyVec3("Position", e->transform.position, 0.05f);
            if (UI::PropertyVec3("Rotation", m_EulerCache, 0.5f, "%.1f")) {
                e->transform.SetEulerAngles(m_EulerCache);
                cachedRotation = e->transform.rotation;
                changed = true;
            }
            changed |= UI::PropertyVec3("Scale", e->transform.scale, 0.02f);
            UI::EndProperties();
        }
    }

    bool keep = true;
    if (e->meshRenderer) {
        if (ComponentHeader("Mesh Renderer", keep) && UI::BeginProperties("mesh")) {
            MeshRendererComponent& mr = *e->meshRenderer;
            UI::PropertyLabel("Mesh");
            if (ImGui::BeginCombo("##mesh", mr.mesh.c_str(), ImGuiComboFlags_HeightLarge)) {
                for (const std::string& name : GetRenderer().MeshNames())
                    if (ImGui::Selectable(name.c_str(), name == mr.mesh)) {
                        mr.mesh = name;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            changed |= UI::PropertyColor("Color", mr.color);
            UI::PropertyLabel("Texture");
            ImGui::PushID("texture");
            changed |= DrawSpriteField(mr.texture);
            ImGui::PopID();
            changed |= UI::PropertyFloat("Checker Scale", mr.checkerScale, 0.01f, 0.0f, 100.0f);
            UI::EndProperties();
        }
        if (!keep) { e->meshRenderer.reset(); changed = true; }
    }

    if (e->collider) {
        const char* title = e->collider->shape == ColliderShape::Box      ? "Box Collider"
                            : e->collider->shape == ColliderShape::Sphere ? "Sphere Collider"
                                                                          : "Capsule Collider";
        if (ComponentHeader(title, keep) && UI::BeginProperties("collider")) {
            ColliderComponent& c = *e->collider;
            const char* shapes[] = {"Box", "Sphere", "Capsule"};
            int shape = int(c.shape);
            if (UI::PropertyCombo("Shape", shape, shapes, 3)) {
                c.shape = ColliderShape(shape);
                changed = true;
            }
            changed |= UI::PropertyBool("Is Trigger", c.isTrigger);
            changed |= UI::PropertyVec3("Center", c.center, 0.02f);
            if (c.shape == ColliderShape::Box)
                changed |= UI::PropertyVec3("Size", c.size, 0.02f);
            if (c.shape != ColliderShape::Box)
                changed |= UI::PropertyFloat("Radius", c.radius, 0.01f, 0.001f, 1000.0f);
            if (c.shape == ColliderShape::Capsule)
                changed |= UI::PropertyFloat("Height", c.height, 0.01f, 0.001f, 1000.0f);
            changed |= UI::PropertyFloat("Friction", c.friction, 0.01f, 0.0f, 1.0f);
            changed |= UI::PropertyFloat("Bounciness", c.bounciness, 0.01f, 0.0f, 1.0f);
            UI::EndProperties();
        }
        if (!keep) { e->collider.reset(); changed = true; }
    }

    if (e->rigidbody) {
        if (ComponentHeader("Rigidbody", keep) && UI::BeginProperties("rigidbody")) {
            RigidbodyComponent& rb = *e->rigidbody;
            changed |= UI::PropertyFloat("Mass", rb.mass, 0.05f, 0.001f, 100000.0f);
            changed |= UI::PropertyFloat("Drag", rb.linearDamping, 0.01f, 0.0f, 100.0f);
            changed |= UI::PropertyFloat("Angular Drag", rb.angularDamping, 0.01f, 0.0f, 100.0f);
            changed |= UI::PropertyBool("Use Gravity", rb.useGravity);
            changed |= UI::PropertyBool("Is Kinematic", rb.isKinematic);
            UI::EndProperties();
            if (!e->collider)
                ImGui::TextColored({1.0f, 0.75f, 0.3f, 1.0f}, "Add a collider so this body can simulate.");
        }
        if (!keep) { e->rigidbody.reset(); changed = true; }
    }

    if (e->light) {
        if (ComponentHeader("Light", keep) && UI::BeginProperties("light")) {
            changed |= UI::PropertyColor("Color", e->light->color);
            changed |= UI::PropertyFloat("Intensity", e->light->intensity, 0.01f, 0.0f, 10.0f);
            UI::EndProperties();
            ImGui::TextDisabled("Directional light shines along the object's blue (Z) axis.");
        }
        if (!keep) { e->light.reset(); changed = true; }
    }

    if (e->camera) {
        if (ComponentHeader("Camera", keep) && UI::BeginProperties("camera")) {
            changed |= UI::PropertyFloat("Field of View", e->camera->fieldOfView, 0.2f, 1.0f, 179.0f, "%.1f");
            changed |= UI::PropertyFloat("Near Clip", e->camera->nearClip, 0.01f, 0.001f, 100.0f);
            changed |= UI::PropertyFloat("Far Clip", e->camera->farClip, 1.0f, 1.0f, 100000.0f, "%.0f");
            UI::EndProperties();
        }
        if (!keep) { e->camera.reset(); changed = true; }
    }

    if (e->rectTransform) {
        if (ComponentHeader("Rect Transform", keep))
            DrawRectTransform(*e, changed);
        if (!keep) { e->rectTransform.reset(); changed = true; }
    }

    if (e->uiImage) {
        if (ComponentHeader("UI Image", keep) && UI::BeginProperties("uiimage")) {
            UIImageComponent& img = *e->uiImage;
            UI::PropertyLabel("Sprite");
            changed |= DrawSpriteField(img.sprite);
            changed |= UI::PropertyColor("Color", img.color);
            changed |= UI::PropertyBool("Preserve Aspect", img.preserveAspect);
            UI::EndProperties();
            if (!img.sprite.empty()) {
                if (Texture* tex = GetRenderer().LoadTexture(img.sprite)) {
                    ImGui::TextDisabled("%u x %u px", tex->width, tex->height);
                    ImGui::SameLine();
                    if (e->rectTransform && ImGui::SmallButton("Set Native Size")) {
                        e->rectTransform->size = {float(tex->width), float(tex->height)};
                        changed = true;
                    }
                }
            }
        }
        if (!keep) { e->uiImage.reset(); changed = true; }
    }

    if (e->uiText) {
        if (ComponentHeader("UI Text", keep)) {
            UITextComponent& txt = *e->uiText;
            ImGui::TextUnformatted("Text");
            changed |= ImGui::InputTextMultiline("##text", &txt.text, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.5f));
            if (UI::BeginProperties("uitext")) {
                changed |= UI::PropertyFloat("Font Size", txt.fontSize, 0.2f, 4.0f, 400.0f, "%.0f");
                changed |= UI::PropertyColor("Color", txt.color);
                const char* aligns[] = {"Left", "Center", "Right"};
                int align = int(txt.align);
                if (UI::PropertyCombo("Alignment", align, aligns, 3)) {
                    txt.align = TextAlign(align);
                    changed = true;
                }
                changed |= UI::PropertyBool("Shadow", txt.shadow);
                UI::EndProperties();
            }
        }
        if (!keep) { e->uiText.reset(); changed = true; }
    }

    // Add Component.
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    float width = 220.0f;
    ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
    if (ImGui::Button("Add Component", ImVec2(width, 0.0f)))
        ImGui::OpenPopup("AddComponent");
    if (ImGui::BeginPopup("AddComponent")) {
        if (!e->meshRenderer && ImGui::MenuItem("Mesh Renderer")) { e->meshRenderer = MeshRendererComponent{}; changed = true; }
        if (!e->collider) {
            if (ImGui::MenuItem("Box Collider")) { e->collider = ColliderComponent{}; changed = true; }
            if (ImGui::MenuItem("Sphere Collider")) {
                e->collider = ColliderComponent{};
                e->collider->shape = ColliderShape::Sphere;
                changed = true;
            }
            if (ImGui::MenuItem("Capsule Collider")) {
                e->collider = ColliderComponent{};
                e->collider->shape = ColliderShape::Capsule;
                changed = true;
            }
        }
        if (!e->rigidbody && ImGui::MenuItem("Rigidbody")) {
            e->rigidbody = RigidbodyComponent{};
            if (!e->collider)
                e->collider = ColliderComponent{};
            changed = true;
        }
        if (!e->light && ImGui::MenuItem("Light")) { e->light = LightComponent{}; changed = true; }
        if (!e->camera && ImGui::MenuItem("Camera")) { e->camera = CameraComponent{}; changed = true; }
        if (!e->uiImage && ImGui::MenuItem("UI Image")) {
            e->uiImage = UIImageComponent{};
            if (!e->rectTransform) e->rectTransform = RectTransform{};
            changed = true;
        }
        if (!e->uiText && ImGui::MenuItem("UI Text")) {
            e->uiText = UITextComponent{};
            if (!e->rectTransform) e->rectTransform = RectTransform{};
            changed = true;
        }
        if (!e->rectTransform && ImGui::MenuItem("Rect Transform")) { e->rectTransform = RectTransform{}; changed = true; }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

namespace {

// One cell of the anchor preset grid: -1 = stretch on that axis, otherwise the anchor value.
struct AnchorCell {
    float x;
    float y;
};

void DrawAnchorPresetIcon(ImDrawList* d, ImVec2 a, ImVec2 b, AnchorCell cell, bool selected)
{
    ImU32 frame = selected ? IM_COL32(90, 160, 255, 255) : IM_COL32(150, 150, 150, 255);
    ImU32 mark = IM_COL32(230, 90, 70, 255);
    ImVec2 inA(a.x + 7, a.y + 7), inB(b.x - 7, b.y - 7);
    d->AddRect(inA, inB, frame, 0.0f, 1.0f);
    auto px = [&](float x) { return inA.x + (inB.x - inA.x) * x; };
    auto py = [&](float y) { return inB.y - (inB.y - inA.y) * y; };
    if (cell.x < 0.0f && cell.y < 0.0f) {
        d->AddRectFilled({inA.x + 3, inA.y + 3}, {inB.x - 3, inB.y - 3}, IM_COL32(120, 120, 120, 160));
    } else if (cell.x < 0.0f) {
        d->AddLine({inA.x - 4, py(cell.y)}, {inB.x + 4, py(cell.y)}, mark, 2.0f);
    } else if (cell.y < 0.0f) {
        d->AddLine({px(cell.x), inA.y - 4}, {px(cell.x), inB.y + 4}, mark, 2.0f);
    } else {
        d->AddCircleFilled({px(cell.x), py(cell.y)}, 3.0f, mark);
    }
}

} // namespace

void EditorApp::DrawRectTransform(Entity& entity, bool& changed)
{
    RectTransform& rect = *entity.rectTransform;
    UILayoutResult layout = ReferenceLayout();
    CanvasRect parent = layout.ParentRect(entity);
    const bool stretchX = rect.anchorMin.x != rect.anchorMax.x;
    const bool stretchY = rect.anchorMin.y != rect.anchorMax.y;

    // ---- Anchor presets (Unity-style 4x4 grid).
    ImVec2 buttonSize(48.0f, 48.0f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool open = ImGui::Button("##anchors", buttonSize);
    AnchorCell current{stretchX ? -1.0f : rect.anchorMin.x, stretchY ? -1.0f : rect.anchorMin.y};
    DrawAnchorPresetIcon(ImGui::GetWindowDrawList(), pos, {pos.x + buttonSize.x, pos.y + buttonSize.y}, current, true);
    UI::Tooltip("Anchor presets");
    if (open)
        ImGui::OpenPopup("AnchorPresets");
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextDisabled("Anchors keep this element in place when the screen size or aspect changes.");
    ImGui::TextDisabled("Parent: %s", rect.parent && m_Scene.Get(rect.parent) ? m_Scene.Get(rect.parent)->name.c_str() : "Screen");
    ImGui::EndGroup();

    if (ImGui::BeginPopup("AnchorPresets")) {
        ImGui::TextUnformatted("Anchor Presets");
        ImGui::TextDisabled("Shift: also set pivot    Alt: also set position");
        const float values[] = {1.0f, 0.5f, 0.0f, -1.0f}; // rows: top, middle, bottom, stretch
        const float cols[] = {0.0f, 0.5f, 1.0f, -1.0f};   // left, center, right, stretch
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                if (col > 0)
                    ImGui::SameLine();
                AnchorCell cell{cols[col], values[row]};
                ImGui::PushID(row * 4 + col);
                ImVec2 p = ImGui::GetCursorScreenPos();
                if (ImGui::Button("##cell", ImVec2(44, 44))) {
                    ImGuiIO& io = ImGui::GetIO();
                    glm::vec2 amin(cell.x < 0 ? 0.0f : cell.x, cell.y < 0 ? 0.0f : cell.y);
                    glm::vec2 amax(cell.x < 0 ? 1.0f : cell.x, cell.y < 0 ? 1.0f : cell.y);
                    UILayout::SetAnchorsKeepRect(rect, amin, amax, parent);
                    if (io.KeyShift) {
                        CanvasRect now = UILayout::Resolve(rect, parent);
                        rect.pivot = {cell.x < 0 ? 0.5f : cell.x, cell.y < 0 ? 0.5f : cell.y};
                        UILayout::SetFromCanvasRect(rect, now, parent);
                    }
                    if (io.KeyAlt) {
                        for (int axis = 0; axis < 2; ++axis) {
                            rect.position[axis] = 0.0f;
                            if ((axis == 0 ? cell.x : cell.y) < 0.0f)
                                rect.size[axis] = 0.0f; // fill the parent on stretched axes
                        }
                    }
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                DrawAnchorPresetIcon(ImGui::GetWindowDrawList(), p, {p.x + 44, p.y + 44}, cell,
                                     cell.x == current.x && cell.y == current.y);
                ImGui::PopID();
            }
        }
        ImGui::EndPopup();
    }

    if (!UI::BeginProperties("recttransform"))
        return;
    // Unity naming: stretched axes show offsets from the anchor edges, others position + size.
    glm::vec2 offsetMin = rect.position - rect.size * rect.pivot;
    glm::vec2 offsetMax = rect.position + rect.size * (glm::vec2(1.0f) - rect.pivot);
    auto setOffsets = [&](glm::vec2 omin, glm::vec2 omax) {
        rect.size = omax - omin;
        rect.position = omin + rect.size * rect.pivot;
        changed = true;
    };
    auto twoFloats = [&](const char* label, const char* a, float& va, const char* b, float& vb) {
        UI::PropertyLabel(label);
        ImGui::PushID(label);
        float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        bool edited = false;
        ImGui::SetNextItemWidth(w);
        edited |= ImGui::DragFloat("##a", &va, 1.0f, 0.0f, 0.0f, (std::string(a) + " %.0f").c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w);
        edited |= ImGui::DragFloat("##b", &vb, 1.0f, 0.0f, 0.0f, (std::string(b) + " %.0f").c_str());
        ImGui::PopID();
        return edited;
    };

    if (stretchX) {
        float left = offsetMin.x + 0.0f, right = -offsetMax.x + 0.0f; // + 0 avoids showing -0
        if (twoFloats("Horizontal", "Left", left, "Right", right))
            setOffsets({left, offsetMin.y}, {-right, offsetMax.y});
    } else {
        changed |= twoFloats("Horizontal", "X", rect.position.x, "W", rect.size.x);
    }
    offsetMin = rect.position - rect.size * rect.pivot;
    offsetMax = rect.position + rect.size * (glm::vec2(1.0f) - rect.pivot);
    if (stretchY) {
        float top = -offsetMax.y + 0.0f, bottom = offsetMin.y + 0.0f;
        if (twoFloats("Vertical", "Top", top, "Bottom", bottom))
            setOffsets({offsetMin.x, bottom}, {offsetMax.x, -top});
    } else {
        changed |= twoFloats("Vertical", "Y", rect.position.y, "H", rect.size.y);
    }

    auto vec2Row = [&](const char* label, glm::vec2& v) {
        UI::PropertyLabel(label);
        ImGui::PushID(label);
        glm::vec2 before = v;
        bool edited = ImGui::DragFloat2("##v", &v.x, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::PopID();
        if (edited) {
            glm::vec2 after = v;
            v = before;
            return std::optional<glm::vec2>(after);
        }
        return std::optional<glm::vec2>();
    };
    // Anchor and pivot edits keep the element where it is, like Unity.
    if (auto v = vec2Row("Anchor Min", rect.anchorMin)) {
        UILayout::SetAnchorsKeepRect(rect, *v, glm::max(*v, rect.anchorMax), parent);
        changed = true;
    }
    if (auto v = vec2Row("Anchor Max", rect.anchorMax)) {
        UILayout::SetAnchorsKeepRect(rect, glm::min(rect.anchorMin, *v), *v, parent);
        changed = true;
    }
    if (auto v = vec2Row("Pivot", rect.pivot)) {
        CanvasRect now = UILayout::Resolve(rect, parent);
        rect.pivot = *v;
        UILayout::SetFromCanvasRect(rect, now, parent);
        changed = true;
    }
    UI::PropertyLabel("Order");
    changed |= ImGui::DragInt("##order", &rect.order, 0.1f);

    UI::PropertyLabel("Parent");
    const Entity* currentParent = rect.parent ? m_Scene.Get(rect.parent) : nullptr;
    if (ImGui::BeginCombo("##parent", currentParent ? currentParent->name.c_str() : "Screen")) {
        if (ImGui::Selectable("Screen", rect.parent == 0))
            ReparentUI(entity.id, 0), changed = true;
        for (const auto& other : m_Scene.Entities()) {
            if (!other->rectTransform || m_Scene.IsUIDescendant(other->id, entity.id))
                continue;
            ImGui::PushID(int(other->id));
            if (ImGui::Selectable(other->name.c_str(), other->id == rect.parent))
                ReparentUI(entity.id, other->id), changed = true;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    UI::EndProperties();
}

bool EditorApp::DrawSpriteField(std::string& sprite)
{
    bool changed = false;
    std::string label = sprite.empty() ? "None" : sprite;
    if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0.0f)))
        ImGui::OpenPopup("SpritePicker");
    UI::Tooltip("Click to choose, or drag an image from the Project panel here");
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("IE_ASSET")) {
            std::string asset(static_cast<const char*>(payload->Data));
            if (ImageIO::IsImageFile(Platform::Utf8ToPath(asset))) {
                sprite = asset;
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopup("SpritePicker")) {
        if (ImGui::Selectable("None", sprite.empty())) {
            sprite.clear();
            changed = true;
        }
        for (const std::string& asset : ListImageAssets()) {
            if (Texture* tex = GetRenderer().LoadTexture(asset)) {
                ImGui::Image(ImTextureRef(m_ImGui->Texture(*tex)), ImVec2(24.0f, 24.0f));
                ImGui::SameLine();
            }
            if (ImGui::Selectable(asset.c_str(), asset == sprite)) {
                sprite = asset;
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    return changed;
}

void EditorApp::DrawSceneSettings()
{
    ImGui::TextDisabled("No object selected");
    ImGui::Spacing();
    bool changed = false;
    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen) && UI::BeginProperties("env")) {
        changed |= UI::PropertyColor("Sky Ambient", m_Scene.settings.skyAmbient);
        changed |= UI::PropertyColor("Ground Ambient", m_Scene.settings.groundAmbient);
        changed |= UI::PropertyFloat("Ambient Intensity", m_Scene.settings.ambientIntensity, 0.01f, 0.0f, 5.0f);
        UI::EndProperties();
    }
    if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen) && UI::BeginProperties("physics")) {
        changed |= UI::PropertyVec3("Gravity", m_Scene.settings.gravity, 0.05f);
        UI::EndProperties();
    }
    if (ImGui::CollapsingHeader("UI Scaling", ImGuiTreeNodeFlags_DefaultOpen) && UI::BeginProperties("ui")) {
        SceneSettings& st = m_Scene.settings;
        const char* modes[] = {"Scale With Screen Size", "Constant Pixel Size"};
        int mode = int(st.uiScaleMode);
        if (UI::PropertyCombo("Scale Mode", mode, modes, 2)) {
            st.uiScaleMode = UIScaleMode(mode);
            changed = true;
        }
        UI::PropertyLabel("Reference Resolution");
        changed |= ImGui::DragFloat2("##ref", &st.uiReferenceResolution.x, 1.0f, 100.0f, 8192.0f, "%.0f");
        changed |= UI::PropertyFloat("Match Width/Height", st.uiMatchWidthOrHeight, 0.01f, 0.0f, 1.0f, "%.2f");
        UI::EndProperties();
        ImGui::TextDisabled("Match 0 = keep width, 1 = keep height (best for landscape PC games).");
    }
    if (changed)
        MarkDirty();
}

} // namespace ie
