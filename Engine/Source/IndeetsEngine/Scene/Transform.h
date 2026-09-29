#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ie {

// Unity-compatible conventions: left-handed, +Y up, +Z forward, +X right.
namespace Axis {
inline constexpr glm::vec3 Right{1.0f, 0.0f, 0.0f};
inline constexpr glm::vec3 Up{0.0f, 1.0f, 0.0f};
inline constexpr glm::vec3 Forward{0.0f, 0.0f, 1.0f};
} // namespace Axis

struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    glm::mat4 Matrix() const;

    glm::vec3 Forward() const { return rotation * Axis::Forward; }
    glm::vec3 Right() const { return rotation * Axis::Right; }
    glm::vec3 Up() const { return rotation * Axis::Up; }

    // Euler angles in degrees, applied Z, then X, then Y (same order as Unity).
    void SetEulerAngles(const glm::vec3& degrees);
    glm::vec3 EulerAngles() const;
    void Rotate(const glm::vec3& degrees); // local-space rotation
};

glm::quat QuatFromEuler(const glm::vec3& degrees);

// Splits a TRS matrix (no shear) into position, rotation and scale.
void DecomposeWorld(const glm::mat4& m, glm::vec3& position, glm::quat& rotation, glm::vec3& scale);

} // namespace ie
