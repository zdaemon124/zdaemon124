#pragma once

#include "IndeetsEngine/Renderer/MeshData.h"
#include "IndeetsEngine/Renderer/VulkanCommon.h"

#include <string>

namespace ie {

// GPU-side geometry, owned by the Renderer.
struct Mesh {
    std::string name;
    AllocatedBuffer vertexBuffer;
    AllocatedBuffer indexBuffer;
    uint32_t indexCount = 0;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

} // namespace ie
