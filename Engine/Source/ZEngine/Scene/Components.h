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

// ---- Screen-space UI (drawn over the game camera, edited in the UI panel) ----

// Unity-like RectTransform. Coordinates are in reference pixels (SceneSettings::uiReferenceResolution),
// anchors/pivot in 0..1 of the parent rect measured from its bottom-left corner.
//  - anchorMin == anchorMax: `size` is the element size and `position` the pivot offset from the anchor.
//  - anchorMin != anchorMax (stretch): the element spans the anchors and `size` is added to that span
//    (so size = 0 fills the anchor area exactly, negative values inset it).
struct RectTransform {
    glm::vec2 anchorMin{0.5f, 0.5f};
    glm::vec2 anchorMax{0.5f, 0.5f};
    glm::vec2 pivot{0.5f, 0.5f};
    glm::vec2 position{0.0f};
    glm::vec2 size{100.0f, 100.0f};
    int order = 0;           // draw order among siblings (higher = on top)
    uint32_t parent = 0;     // parent UI entity id, 0 = the screen

    static RectTransform Anchored(glm::vec2 anchor, glm::vec2 position, glm::vec2 size, int order = 0)
    {
        RectTransform r;
        r.anchorMin = r.anchorMax = r.pivot = anchor;
        r.position = position;
        r.size = size;
        r.order = order;
        return r;
    }
};

struct UIImageComponent {
    std::string sprite;  // image path relative to Assets ("" = plain color)
    glm::vec4 color{1.0f};
    bool preserveAspect = false;
};

enum class TextAlign { Left, Center, Right };

struct UITextComponent {
    std::string text = "New Text";
    float fontSize = 36.0f;
    glm::vec4 color{1.0f};
    TextAlign align = TextAlign::Center;
    bool shadow = true;
};

enum class UIScaleMode { ScaleWithScreenSize, ConstantPixelSize };

const char* ColliderShapeName(ColliderShape shape);

} // namespace ze
