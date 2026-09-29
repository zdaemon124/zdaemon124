#pragma once

#include "ZEngine/Renderer/VulkanCommon.h"

#include <filesystem>

namespace ze {

enum class VertexLayout { Mesh, UI, None };

struct GraphicsPipelineDesc {
    std::filesystem::path vertexShader;   // .spv
    std::filesystem::path fragmentShader; // .spv
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool depthTest = true;
    bool depthWrite = true;
    bool alphaBlend = false;
    float depthBiasConstant = 0.0f; // != 0 enables depth bias (negative = towards the camera)
    float depthBiasSlope = 0.0f;
    VertexLayout vertexLayout = VertexLayout::Mesh; // None for full-screen passes without vertex buffers
};

VkShaderModule LoadShaderModule(VkDevice device, const std::filesystem::path& path);
VkPipeline CreateGraphicsPipeline(VkDevice device, const GraphicsPipelineDesc& desc);

} // namespace ze
