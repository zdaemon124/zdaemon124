#pragma once

#include "ZEngine/Scene/Transform.h"

namespace ze {

// What the renderer needs to draw a view.
struct CameraData {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 position{0.0f};
};

struct PerspectiveLens {
    float fieldOfView = 60.0f; // vertical, degrees
    float nearClip = 0.1f;
    float farClip = 1000.0f;

    glm::mat4 Projection(float aspect) const;          // Vulkan clip space (Y down)
    glm::mat4 ProjectionYUp(float aspect) const;       // same without the Y flip (for gizmo libraries)
};

// Camera data looking from `transform` (scale is ignored).
CameraData MakeCameraData(const Transform& transform, const PerspectiveLens& lens, float aspect);

// Scene-view camera: hold RMB to look around and fly with WASD/QE (Shift = faster),
// MMB drag to pan, mouse wheel to move forward/backward.
class EditorCamera {
public:
    EditorCamera();

    // allowStart: whether a new look/pan/zoom may begin this frame (e.g. mouse is over the viewport).
    void Update(float deltaTime, bool allowStart = true);
    // Moves the camera so that a sphere of `radius` around `target` fills the view.
    void Focus(const glm::vec3& target, float radius);
    bool IsControlling() const { return m_Looking || m_Panning; }
    void LookAt(const glm::vec3& target);
    CameraData Data(float aspect) const;

    Transform transform;
    PerspectiveLens lens;
    float moveSpeed = 6.0f;
    float lookSensitivity = 0.15f; // degrees per pixel

private:
    float m_Yaw = 0.0f;   // around +Y
    float m_Pitch = 0.0f; // around +X
    bool m_Looking = false;
    bool m_Panning = false;
};

} // namespace ze
