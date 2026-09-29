#include "ZEngine/Renderer/Pipeline.h"

#include "ZEngine/Core/Platform.h"
#include "ZEngine/Renderer/Mesh.h"

#include <array>
#include <cstddef>

namespace ze {

VkShaderModule LoadShaderModule(VkDevice device, const std::filesystem::path& path)
{
    std::vector<char> code = Platform::ReadBinaryFile(path);
    if (code.empty())
        Log::Fatal("Failed to read shader '{}'", path.string());

    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    ZE_VK_CHECK(vkCreateShaderModule(device, &info, nullptr, &module));
    return module;
}

VkPipeline CreateGraphicsPipeline(VkDevice device, const GraphicsPipelineDesc& desc)
{
    VkShaderModule vert = LoadShaderModule(device, desc.vertexShader);
    VkShaderModule frag = LoadShaderModule(device, desc.fragmentShader);

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    std::array<VkVertexInputAttributeDescription, 3> attributes{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
    }};
    VkVertexInputBindingDescription uiBinding{0, sizeof(UIVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    std::array<VkVertexInputAttributeDescription, 3> uiAttributes{{
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, position)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, uv)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIVertex, color)},
    }};
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (desc.vertexLayout != VertexLayout::None) {
        bool ui = desc.vertexLayout == VertexLayout::UI;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = ui ? &uiBinding : &binding;
        vertexInput.vertexAttributeDescriptionCount = 3;
        vertexInput.pVertexAttributeDescriptions = ui ? uiAttributes.data() : attributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = desc.topology;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = desc.polygonMode;
    raster.cullMode = desc.cullMode;
    // Unity convention: front faces are clockwise as seen on screen.
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    if (desc.depthBiasConstant != 0.0f || desc.depthBiasSlope != 0.0f) {
        raster.depthBiasEnable = VK_TRUE;
        raster.depthBiasConstantFactor = desc.depthBiasConstant;
        raster.depthBiasSlopeFactor = desc.depthBiasSlope;
    }

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = desc.depthTest;
    depth.depthWriteEnable = desc.depthWrite;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (desc.alphaBlend) {
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    // Vulkan 1.3 dynamic rendering: attachment formats instead of a VkRenderPass.
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &desc.colorFormat;
    rendering.depthAttachmentFormat = desc.depthFormat;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = static_cast<uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = desc.layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    ZE_VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline));

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    return pipeline;
}

} // namespace ze
