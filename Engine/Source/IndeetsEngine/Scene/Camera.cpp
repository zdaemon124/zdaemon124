#include "IndeetsEngine/Scene/Camera.h"

#include "IndeetsEngine/Core/Input.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace ie {

glm::mat4 PerspectiveLens::Projection(float aspect) const
{
    glm::mat4 proj = glm::perspectiveLH_ZO(glm::radians(fieldOfView), aspect, nearClip, farClip);
    proj[1][1] *= -1.0f; // Vulkan clip space has +Y pointing down
    return proj;
}

glm::mat4 PerspectiveLens::ProjectionYUp(float aspect) const
{
    return glm::perspectiveLH_ZO(glm::radians(fieldOfView), aspect, nearClip, farClip);
}

CameraData MakeCameraData(const Transform& transform, const PerspectiveLens& lens, float aspect)
{
    CameraData data;
    data.position = transform.position;
    glm::mat4 world = glm::translate(glm::mat4(1.0f), transform.position) * glm::mat4_cast(transform.rotation);
    data.view = glm::inverse(world);
    data.projection = lens.Projection(aspect);
    return data;
}

EditorCamera::EditorCamera()
{
    transform.position = {0.0f, 4.0f, -10.0f};
    LookAt({0.0f, 0.5f, 0.0f});
}

void EditorCamera::LookAt(const glm::vec3& target)
{
    glm::vec3 dir = glm::normalize(target - transform.position);
    m_Yaw = m_TargetYaw = glm::degrees(std::atan2(dir.x, dir.z));
    m_Pitch = m_TargetPitch = glm::degrees(-std::asin(std::clamp(dir.y, -1.0f, 1.0f)));
    ApplyRotation();
}

void EditorCamera::ApplyRotation()
{
    transform.SetEulerAngles({m_Pitch, m_Yaw, 0.0f});
}

void EditorCamera::Focus(const glm::vec3& target, float radius)
{
    float distance = std::max(radius, 0.25f) / std::tan(glm::radians(lens.fieldOfView) * 0.5f) * 1.2f;
    m_FocusStart = transform.position;
    m_FocusTarget = target - transform.Forward() * distance;
    m_FocusTime = 0.0f;
    m_Velocity = glm::vec3(0.0f);
    m_ZoomOffset = glm::vec3(0.0f);
}

namespace {
// Frame-rate independent exponential smoothing factor.
float Damp(float smoothing, float dt)
{
    return smoothing <= 0.0f ? 1.0f : 1.0f - std::exp(-dt / smoothing);
}
} // namespace

void EditorCamera::Update(float deltaTime, bool allowStart)
{
    const glm::vec2 mouse = Input::MouseDelta();

    bool rmb = Input::GetMouseButton(MouseButton::Right);
    bool mmb = Input::GetMouseButton(MouseButton::Middle);
    bool look = rmb && (m_Looking || (allowStart && Input::GetMouseButtonDown(MouseButton::Right)));
    bool pan = !look && mmb && (m_Panning || (allowStart && Input::GetMouseButtonDown(MouseButton::Middle)));
    if (look != m_Looking)
        Input::SetCursorLocked(look);
    m_Looking = look;
    m_Panning = pan;

    glm::vec3 desiredVelocity{0.0f};
    if (look) {
        m_FocusTime = -1.0f;
        m_TargetYaw += mouse.x * lookSensitivity;
        m_TargetPitch = std::clamp(m_TargetPitch + mouse.y * lookSensitivity, -89.0f, 89.0f);

        // Wheel while flying changes speed, like Unity.
        if (float scroll = Input::ScrollDelta(); scroll != 0.0f)
            moveSpeed = std::clamp(moveSpeed * (scroll > 0.0f ? 1.2f : 1.0f / 1.2f), 0.5f, 200.0f);

        glm::vec3 move{0.0f};
        if (Input::GetKey(Key::W)) move += transform.Forward();
        if (Input::GetKey(Key::S)) move -= transform.Forward();
        if (Input::GetKey(Key::D)) move += transform.Right();
        if (Input::GetKey(Key::A)) move -= transform.Right();
        if (Input::GetKey(Key::E)) move += Axis::Up;
        if (Input::GetKey(Key::Q)) move -= Axis::Up;
        if (glm::dot(move, move) > 0.0f)
            desiredVelocity = glm::normalize(move) * moveSpeed * (Input::GetKey(Key::LeftShift) ? 3.0f : 1.0f);
    } else {
        if (pan) {
            m_FocusTime = -1.0f;
            float amount = 0.0025f * moveSpeed;
            transform.position += (-transform.Right() * mouse.x + transform.Up() * mouse.y) * amount;
        }
        if (allowStart) {
            if (float scroll = Input::ScrollDelta(); scroll != 0.0f) {
                m_FocusTime = -1.0f;
                m_ZoomOffset += transform.Forward() * scroll * moveSpeed * 0.2f;
            }
        }
    }

    // Smooth rotation towards the mouse target.
    float lookT = Damp(lookSmoothing, deltaTime);
    m_Yaw += (m_TargetYaw - m_Yaw) * lookT;
    m_Pitch += (m_TargetPitch - m_Pitch) * lookT;
    ApplyRotation();

    // Accelerate / decelerate instead of starting and stopping instantly.
    m_Velocity += (desiredVelocity - m_Velocity) * Damp(moveSmoothing, deltaTime);
    if (glm::dot(m_Velocity, m_Velocity) < 1e-6f)
        m_Velocity = glm::vec3(0.0f);
    transform.position += m_Velocity * deltaTime;

    // Smooth wheel zoom.
    glm::vec3 zoomStep = m_ZoomOffset * Damp(0.07f, deltaTime);
    transform.position += zoomStep;
    m_ZoomOffset -= zoomStep;

    // Fly to a focused object (ease-out over ~0.3 s).
    if (m_FocusTime >= 0.0f) {
        m_FocusTime += deltaTime;
        float t = std::min(m_FocusTime / 0.3f, 1.0f);
        float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        transform.position = m_FocusStart + (m_FocusTarget - m_FocusStart) * ease;
        if (t >= 1.0f)
            m_FocusTime = -1.0f;
    }
}

CameraData EditorCamera::Data(float aspect) const
{
    return MakeCameraData(transform, lens, aspect);
}

} // namespace ie
