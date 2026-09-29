#include "EditorApp.h"

#include "EditorUI.h"

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
                 e->camera ? UI::Icon::Camera : e->light ? UI::Icon::Light : e->meshRenderer ? UI::Icon::Cube : UI::Icon::Empty,
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

    // Transform (always present).
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
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
        ImGui::EndPopup();
    }
    if (IsPlaying())
        ImGui::TextDisabled("Play mode: changes will be reverted when you press Stop.");

    ImGui::PopID();
    if (changed)
        MarkDirty();
    ImGui::End();
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
    if (changed)
        MarkDirty();
}

} // namespace ze
