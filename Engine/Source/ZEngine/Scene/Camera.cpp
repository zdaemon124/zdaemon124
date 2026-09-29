#include "ZEngine/Scene/Camera.h"

#include "ZEngine/Core/Input.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace ze {

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
    m_Yaw = glm::degrees(std::atan2(dir.x, dir.z));
    m_Pitch = glm::degrees(-std::asin(std::clamp(dir.y, -1.0f, 1.0f)));
    transform.SetEulerAngles({m_Pitch, m_Yaw, 0.0f});
}

void EditorCamera::Focus(const glm::vec3& target, float radius)
{
    float distance = std::max(radius, 0.25f) / std::tan(glm::radians(lens.fieldOfView) * 0.5f) * 1.2f;
    transform.position = target - transform.Forward() * distance;
}

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

    if (look) {
        m_Yaw += mouse.x * lookSensitivity;
        m_Pitch = std::clamp(m_Pitch + mouse.y * lookSensitivity, -89.0f, 89.0f);
        transform.SetEulerAngles({m_Pitch, m_Yaw, 0.0f});

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
        if (glm::dot(move, move) > 0.0f) {
            float speed = moveSpeed * (Input::GetKey(Key::LeftShift) ? 3.0f : 1.0f);
            transform.position += glm::normalize(move) * speed * deltaTime;
        }
        return;
    }
    if (pan) {
        float amount = 0.0025f * moveSpeed;
        transform.position += (-transform.Right() * mouse.x + transform.Up() * mouse.y) * amount;
    }
    if (allowStart) {
        if (float scroll = Input::ScrollDelta(); scroll != 0.0f)
            transform.position += transform.Forward() * scroll * moveSpeed * 0.15f;
    }
}

CameraData EditorCamera::Data(float aspect) const
{
    return MakeCameraData(transform, lens, aspect);
}

} // namespace ze
