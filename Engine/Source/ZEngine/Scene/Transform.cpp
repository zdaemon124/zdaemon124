#include "ZEngine/Scene/Transform.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace ze {

glm::quat QuatFromEuler(const glm::vec3& degrees)
{
    const glm::vec3 r = glm::radians(degrees);
    return glm::angleAxis(r.y, Axis::Up) * glm::angleAxis(r.x, Axis::Right) * glm::angleAxis(r.z, Axis::Forward);
}

void DecomposeWorld(const glm::mat4& m, glm::vec3& position, glm::quat& rotation, glm::vec3& scale)
{
    position = glm::vec3(m[3]);
    glm::vec3 x(m[0]), y(m[1]), z(m[2]);
    scale = {glm::length(x), glm::length(y), glm::length(z)};
    if (glm::dot(glm::cross(x, y), z) < 0.0f)
        scale.x = -scale.x; // mirrored
    glm::mat3 r(x / (scale.x != 0.0f ? scale.x : 1.0f), y / (scale.y != 0.0f ? scale.y : 1.0f),
                z / (scale.z != 0.0f ? scale.z : 1.0f));
    rotation = glm::normalize(glm::quat_cast(r));
}

glm::mat4 Transform::Matrix() const
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    m *= glm::mat4_cast(rotation);
    return glm::scale(m, scale);
}

void Transform::SetEulerAngles(const glm::vec3& degrees)
{
    rotation = QuatFromEuler(degrees);
}

glm::vec3 Transform::EulerAngles() const
{
    // Decompose R = Ry * Rx * Rz.
    float y = 0.0f, x = 0.0f, z = 0.0f;
    glm::extractEulerAngleYXZ(glm::mat4_cast(rotation), y, x, z);
    return glm::degrees(glm::vec3(x, y, z)) + glm::vec3(0.0f); // + 0 turns -0 into 0 for display
}

void Transform::Rotate(const glm::vec3& degrees)
{
    rotation = glm::normalize(rotation * QuatFromEuler(degrees));
}

} // namespace ze
