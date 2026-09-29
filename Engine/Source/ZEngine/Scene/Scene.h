#pragma once

#include "ZEngine/Scene/Transform.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ze {

struct Mesh;

using EntityID = uint32_t;

struct MeshRenderer {
    Mesh* mesh = nullptr;
    glm::vec4 color{1.0f};      // linear RGBA
    float checkerScale = 0.0f;  // > 0 draws a world-space checker pattern (ground helper)
};

struct Entity {
    EntityID id = 0;
    std::string name;
    bool active = true;
    Transform transform;
    MeshRenderer meshRenderer;
};

struct DirectionalLight {
    glm::vec3 direction = glm::normalize(glm::vec3(0.4f, -1.0f, 0.6f)); // direction the light travels
    glm::vec3 color{1.0f, 0.96f, 0.9f};
    float intensity = 1.0f;
    glm::vec3 skyAmbient{0.22f, 0.27f, 0.36f};
    glm::vec3 groundAmbient{0.10f, 0.09f, 0.08f};
};

// Minimal scene container. Will be replaced by an ECS when components are added.
class Scene {
public:
    Entity& CreateEntity(const std::string& name);
    void DestroyEntity(EntityID id);
    Entity* Find(const std::string& name);
    Entity* Get(EntityID id);

    const std::vector<std::unique_ptr<Entity>>& Entities() const { return m_Entities; }

    DirectionalLight light;

private:
    std::vector<std::unique_ptr<Entity>> m_Entities;
    EntityID m_NextID = 1;
};

} // namespace ze
