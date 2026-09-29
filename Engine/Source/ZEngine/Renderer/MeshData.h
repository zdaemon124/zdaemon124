#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <vector>

namespace ze {

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// Screen-space UI vertex: pixel position (top-left origin), UV, gamma-space color.
struct UIVertex {
    glm::vec2 position;
    glm::vec2 uv;
    glm::vec4 color;
};

// CPU-side geometry.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

} // namespace ze
