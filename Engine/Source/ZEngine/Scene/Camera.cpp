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

void EditorCamera::Update(float deltaTime)
{
    const glm::vec2 mouse = Input::MouseDelta();

    bool look = Input::GetMouseButton(MouseButton::Right);
    if (look != m_Looking) {
        Input::SetCursorLocked(look);
        m_Looking = look;
    }

    if (look) {
        m_Yaw += mouse.x * lookSensitivity;
        m_Pitch = std::clamp(m_Pitch + mouse.y * lookSensitivity, -89.0f, 89.0f);
        transform.SetEulerAngles({m_Pitch, m_Yaw, 0.0f});

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
    } else if (Input::GetMouseButton(MouseButton::Middle)) {
        float pan = 0.01f * moveSpeed * 0.25f;
        transform.position += (-transform.Right() * mouse.x + transform.Up() * mouse.y) * pan;
    }

    if (float scroll = Input::ScrollDelta(); scroll != 0.0f)
        transform.position += transform.Forward() * scroll * moveSpeed * 0.15f;
}

CameraData EditorCamera::Data(float aspect) const
{
    CameraData data;
    data.position = transform.position;
    glm::mat4 world = glm::translate(glm::mat4(1.0f), transform.position) * glm::mat4_cast(transform.rotation);
    data.view = glm::inverse(world);
    data.projection = lens.Projection(aspect);
    return data;
}

} // namespace ze
