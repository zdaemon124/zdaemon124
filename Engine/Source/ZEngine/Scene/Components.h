#pragma once

#include <glm/glm.hpp>

#include <string>

namespace ze {

// Colors are stored in sRGB like Unity's color pickers; shaders convert to linear.

struct MeshRendererComponent {
    std::string mesh = "Cube"; // name of a mesh registered in the renderer
    glm::vec4 color{1.0f};
    float checkerScale = 0.0f; // > 0 draws a world-space checker pattern
};

enum class ColliderShape { Box, Sphere, Capsule };

struct ColliderComponent {
    ColliderShape shape = ColliderShape::Box;
    glm::vec3 center{0.0f};
    glm::vec3 size{1.0f};  // box
    float radius = 0.5f;   // sphere / capsule
    float height = 2.0f;   // capsule, along local Y, including both caps
    float friction = 0.6f;
    float bounciness = 0.0f;
    bool isTrigger = false;
};

struct RigidbodyComponent {
    float mass = 1.0f;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    bool useGravity = true;
    bool isKinematic = false;
};

// Directional light shining along the entity's forward (+Z) axis.
struct LightComponent {
    glm::vec3 color{1.0f, 0.957f, 0.839f};
    float intensity = 1.0f;
};

struct CameraComponent {
    float fieldOfView = 60.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
};

const char* ColliderShapeName(ColliderShape shape);

} // namespace ze
