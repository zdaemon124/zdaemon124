#include "EditorApp.h"

#include "EditorUI.h"

#include <ZEngine/Core/Platform.h>

#include <algorithm>

namespace ze {

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
    Entity* e = Selected();
    if (!e) {
        DrawSceneSettings();
        ImGui::End();
        return;
    }

    bool changed = false;
    ImGui::PushID(int(e->id));

    // Header: active toggle + name.
    UI::DrawIcon(ImGui::GetWindowDrawList(),
                 e->camera ? UI::Icon::Camera : e->light ? UI::Icon::Light : e->meshRenderer ? UI::Icon::Cube
                 : (e->uiImage || e->uiText) ? UI::Icon::Image : UI::Icon::Empty,
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
            if (ImGui::BeginCombo("##mesh", mr.mesh.c_str())) {
                for (const std::string& name : GetRenderer().MeshNames())
                    if (ImGui::Selectable(name.c_str(), name == mr.mesh)) {
                        mr.mesh = name;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            changed |= UI::PropertyColor("Color", mr.color);
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
                    if (ImGui::SmallButton("Set Native Size")) {
                        img.rect.size = {float(tex->width), float(tex->height)};
                        changed = true;
                    }
                }
            }
            DrawUIRectProperties(img.rect, changed);
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
            DrawUIRectProperties(txt.rect, changed);
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
        if (!e->uiImage && ImGui::MenuItem("UI Image")) { e->uiImage = UIImageComponent{}; changed = true; }
        if (!e->uiText && ImGui::MenuItem("UI Text")) { e->uiText = UITextComponent{}; changed = true; }
        ImGui::EndPopup();
    }
    if (IsPlaying())
        ImGui::TextDisabled("Play mode: changes will be reverted when you press Stop.");

    ImGui::PopID();
    if (changed)
        MarkDirty();
    ImGui::End();
}

void EditorApp::DrawUIRectProperties(UIRect& rect, bool& changed)
{
    if (!UI::BeginProperties("rect"))
        return;
    // Presets set anchor + pivot to the same point and snap the element there (like Alt+Shift in Unity).
    struct Preset {
        const char* name;
        glm::vec2 point;
    };
    static const Preset presets[] = {
        {"Top Left", {0, 1}},    {"Top", {0.5f, 1}},    {"Top Right", {1, 1}},
        {"Left", {0, 0.5f}},     {"Center", {0.5f, 0.5f}}, {"Right", {1, 0.5f}},
        {"Bottom Left", {0, 0}}, {"Bottom", {0.5f, 0}}, {"Bottom Right", {1, 0}},
    };
    const char* current = "Custom";
    for (const Preset& p : presets)
        if (rect.anchor == p.point && rect.pivot == p.point)
            current = p.name;
    UI::PropertyLabel("Anchor Preset");
    if (ImGui::BeginCombo("##preset", current)) {
        for (const Preset& p : presets)
            if (ImGui::Selectable(p.name, current == p.name)) {
                rect.anchor = rect.pivot = p.point;
                rect.position = {0.0f, 0.0f};
                changed = true;
            }
        ImGui::EndCombo();
    }
    auto vec2Row = [&](const char* label, glm::vec2& v, float speed) {
        UI::PropertyLabel(label);
        ImGui::PushID(label);
        changed |= ImGui::DragFloat2("##v", &v.x, speed);
        ImGui::PopID();
    };
    vec2Row("Position", rect.position, 1.0f);
    vec2Row("Size", rect.size, 1.0f);
    vec2Row("Anchor", rect.anchor, 0.01f);
    vec2Row("Pivot", rect.pivot, 0.01f);
    UI::PropertyLabel("Order");
    changed |= ImGui::DragInt("##order", &rect.order, 0.1f);
    UI::EndProperties();
    ImGui::TextDisabled("Tip: drag the element in the Game view to move it.");
}

bool EditorApp::DrawSpriteField(std::string& sprite)
{
    bool changed = false;
    std::string label = sprite.empty() ? "None (plain color)" : sprite;
    if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0.0f)))
        ImGui::OpenPopup("SpritePicker");
    UI::Tooltip("Click to choose, or drag an image from the Project panel here");
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ZE_ASSET")) {
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
    if (ImGui::CollapsingHeader("UI", ImGuiTreeNodeFlags_DefaultOpen) && UI::BeginProperties("ui")) {
        UI::PropertyLabel("Reference Resolution");
        changed |= ImGui::DragFloat2("##ref", &m_Scene.settings.uiReferenceResolution.x, 1.0f, 100.0f, 8192.0f, "%.0f");
        UI::EndProperties();
        ImGui::TextDisabled("UI scales with the screen height, like Unity's Canvas Scaler.");
    }
    if (changed)
        MarkDirty();
}

} // namespace ze
