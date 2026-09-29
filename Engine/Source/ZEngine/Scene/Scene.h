#pragma once

#include "ZEngine/Scene/Components.h"
#include "ZEngine/Scene/Primitives.h"
#include "ZEngine/Scene/Transform.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ze {

using EntityID = uint32_t;
inline constexpr EntityID kInvalidEntity = 0;

// Unity's GameObject: a name, a transform and optional components.
struct Entity {
    EntityID id = kInvalidEntity;
    std::string name;
    bool active = true;
    Transform transform;

    std::optional<MeshRendererComponent> meshRenderer;
    std::optional<ColliderComponent> collider;
    std::optional<RigidbodyComponent> rigidbody;
    std::optional<LightComponent> light;
    std::optional<CameraComponent> camera;
};

struct SceneSettings {
    glm::vec3 skyAmbient{0.45f, 0.52f, 0.62f};    // sRGB
    glm::vec3 groundAmbient{0.32f, 0.3f, 0.28f};  // sRGB
    float ambientIntensity = 1.0f;
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
};

class Scene {
public:
    Entity& CreateEntity(const std::string& name, EntityID id = kInvalidEntity);
    // GameObject.CreatePrimitive: mesh renderer + matching collider.
    Entity& CreatePrimitive(PrimitiveType type, const std::string& name = {});
    Entity& Duplicate(EntityID id);
    void DestroyEntity(EntityID id);
    void Clear();

    Entity* Get(EntityID id);
    const Entity* Get(EntityID id) const;
    Entity* Find(const std::string& name);
    int IndexOf(EntityID id) const;
    void Move(EntityID id, int newIndex);

    // First active entity with a light / camera component.
    const Entity* MainLight() const;
    const Entity* MainCamera() const;

    std::vector<std::unique_ptr<Entity>>& Entities() { return m_Entities; }
    const std::vector<std::unique_ptr<Entity>>& Entities() const { return m_Entities; }

    SceneSettings settings;

private:
    std::vector<std::unique_ptr<Entity>> m_Entities;
    EntityID m_NextID = 1;
};

} // namespace ze
