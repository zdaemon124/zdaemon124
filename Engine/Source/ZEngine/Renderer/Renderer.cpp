#include "ZEngine/Renderer/Renderer.h"

#include "ZEngine/Core/Platform.h"
#include "ZEngine/Core/Window.h"
#include "ZEngine/Renderer/Pipeline.h"
#include "ZEngine/Renderer/Swapchain.h"
#include "ZEngine/Renderer/VulkanContext.h"
#include "ZEngine/Scene/Scene.h"

#include <cstring>

namespace ze {
namespace {

// std140 layout, must match FrameData in Shaders/Common.glsl.
struct FrameUniforms {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 viewProjection;
    glm::mat4 inverseViewProjection;
    glm::vec4 cameraPosition;
    glm::vec4 lightDirection; // xyz = direction, w = intensity
    glm::vec4 lightColor;
    glm::vec4 skyAmbient;
    glm::vec4 groundAmbient;
    glm::vec4 time; // x = seconds
};

struct DrawPushConstants {
    glm::mat4 model;
    glm::vec4 color;
    glm::vec4 params; // x = checker scale
};
static_assert(sizeof(DrawPushConstants) <= 128, "Push constants must fit the guaranteed 128 bytes");

} // namespace

Renderer::Renderer(Window& window, const RendererSettings& settings)
    : m_Window(window), m_Settings(settings)
{
    m_Context = std::make_unique<VulkanContext>(window);

    uint32_t width = 0, height = 0;
    window.GetFramebufferSize(width, height);
    m_Swapchain = std::make_unique<Swapchain>(*m_Context, VkExtent2D{width, height}, settings.vsync);
    CreateDepthBuffer();
    CreateFrameResources();
    CreateDescriptors();
    CreatePipelines();

    for (size_t i = 0; i < m_Primitives.size(); ++i) {
        auto type = static_cast<PrimitiveType>(i);
        m_Primitives[i] = CreateMesh(PrimitiveName(type), Primitives::Create(type));
    }
}

Renderer::~Renderer()
{
    VkDevice device = m_Context->Device();
    vkDeviceWaitIdle(device);

    for (auto& mesh : m_Meshes) {
        m_Context->DestroyBuffer(mesh->vertexBuffer);
        m_Context->DestroyBuffer(mesh->indexBuffer);
    }
    m_Meshes.clear();

    vkDestroyPipeline(device, m_LitPipeline, nullptr);
    vkDestroyPipeline(device, m_SkyPipeline, nullptr);
    vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_FrameSetLayout, nullptr);

    for (auto& frame : m_Frames) {
        m_Context->DestroyBuffer(frame.frameUniforms);
        vkDestroyFence(device, frame.inFlight, nullptr);
        vkDestroySemaphore(device, frame.imageAvailable, nullptr);
        vkDestroyCommandPool(device, frame.commandPool, nullptr);
    }
    m_Context->DestroyImage(m_Depth);
    m_Swapchain.reset();
    m_Context.reset();
}

Mesh* Renderer::CreateMesh(const std::string& name, const MeshData& data)
{
    auto mesh = std::make_unique<Mesh>();
    mesh->name = name;
    mesh->indexCount = static_cast<uint32_t>(data.indices.size());
    mesh->vertexBuffer = m_Context->CreateBufferWithData(
        data.vertices.data(), data.vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    mesh->indexBuffer = m_Context->CreateBufferWithData(
        data.indices.data(), data.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    if (!data.vertices.empty()) {
        mesh->boundsMin = mesh->boundsMax = data.vertices[0].position;
        for (const Vertex& v : data.vertices) {
            mesh->boundsMin = glm::min(mesh->boundsMin, v.position);
            mesh->boundsMax = glm::max(mesh->boundsMax, v.position);
        }
    }
    m_Meshes.push_back(std::move(mesh));
    return m_Meshes.back().get();
}

VkExtent2D Renderer::OutputExtent() const { return m_Swapchain->Extent(); }

float Renderer::AspectRatio() const
{
    VkExtent2D e = m_Swapchain->Extent();
    return e.height > 0 ? float(e.width) / float(e.height) : 1.0f;
}

void Renderer::WaitIdle() const { vkDeviceWaitIdle(m_Context->Device()); }

void Renderer::CreateFrameResources()
{
    VkDevice device = m_Context->Device();
    for (auto& frame : m_Frames) {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_Context->GraphicsQueueFamily();
        ZE_VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &frame.commandPool));

        VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool = frame.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        ZE_VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &frame.commandBuffer));

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        ZE_VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &frame.inFlight));

        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        ZE_VK_CHECK(vkCreateSemaphore(device, &semInfo, nullptr, &frame.imageAvailable));

        frame.frameUniforms = m_Context->CreateBuffer(sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
    }
}

void Renderer::CreateDescriptors()
{
    VkDevice device = m_Context->Device();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    ZE_VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_FrameSetLayout));

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = kFramesInFlight;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    ZE_VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_DescriptorPool));

    for (auto& frame : m_Frames) {
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_FrameSetLayout;
        ZE_VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &frame.descriptorSet));

        VkDescriptorBufferInfo bufferInfo{frame.frameUniforms.buffer, 0, sizeof(FrameUniforms)};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = frame.descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
}

void Renderer::CreatePipelines()
{
    VkDevice device = m_Context->Device();

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(DrawPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_FrameSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    ZE_VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_PipelineLayout));

    const auto shaderDir = Platform::ExecutableDir() / "shaders";

    GraphicsPipelineDesc lit;
    lit.vertexShader = shaderDir / "Lit.vert.spv";
    lit.fragmentShader = shaderDir / "Lit.frag.spv";
    lit.layout = m_PipelineLayout;
    lit.colorFormat = m_Swapchain->Format();
    lit.depthFormat = kDepthFormat;
    m_LitPipeline = CreateGraphicsPipeline(device, lit);

    GraphicsPipelineDesc sky;
    sky.vertexShader = shaderDir / "Sky.vert.spv";
    sky.fragmentShader = shaderDir / "Sky.frag.spv";
    sky.layout = m_PipelineLayout;
    sky.colorFormat = m_Swapchain->Format();
    sky.depthFormat = kDepthFormat;
    sky.cullMode = VK_CULL_MODE_NONE;
    sky.depthTest = false;
    sky.depthWrite = false;
    sky.meshVertexInput = false;
    m_SkyPipeline = CreateGraphicsPipeline(device, sky);
}

void Renderer::CreateDepthBuffer()
{
    m_Context->DestroyImage(m_Depth);
    m_Depth = m_Context->CreateImage(m_Swapchain->Extent(), kDepthFormat,
                                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Renderer::RecreateSwapchain()
{
    uint32_t width = 0, height = 0;
    m_Window.GetFramebufferSize(width, height);
    if (width == 0 || height == 0)
        return; // minimized, try again later
    m_Swapchain->Recreate({width, height});
    CreateDepthBuffer();
    m_SwapchainDirty = false;
}

void Renderer::Render(const Scene& scene, const CameraData& camera, float time)
{
    if (m_Window.ConsumeResized())
        m_SwapchainDirty = true;
    if (m_SwapchainDirty) {
        RecreateSwapchain();
        if (m_SwapchainDirty)
            return;
    }

    VkDevice device = m_Context->Device();
    FrameResources& frame = m_Frames[m_FrameIndex];
    ZE_VK_CHECK(vkWaitForFences(device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX));

    uint32_t imageIndex = 0;
    VkResult acquire = vkAcquireNextImageKHR(device, m_Swapchain->Handle(), UINT64_MAX, frame.imageAvailable,
                                             VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        m_SwapchainDirty = true;
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
        Log::Fatal("vkAcquireNextImageKHR failed: {}", VkResultString(acquire));
    ZE_VK_CHECK(vkResetFences(device, 1, &frame.inFlight));

    // Per-frame uniforms.
    const DirectionalLight& light = scene.light;
    FrameUniforms uniforms{};
    uniforms.view = camera.view;
    uniforms.projection = camera.projection;
    uniforms.viewProjection = camera.projection * camera.view;
    uniforms.inverseViewProjection = glm::inverse(uniforms.viewProjection);
    uniforms.cameraPosition = glm::vec4(camera.position, 1.0f);
    uniforms.lightDirection = glm::vec4(glm::normalize(light.direction), light.intensity);
    uniforms.lightColor = glm::vec4(light.color, 1.0f);
    uniforms.skyAmbient = glm::vec4(light.skyAmbient, 1.0f);
    uniforms.groundAmbient = glm::vec4(light.groundAmbient, 1.0f);
    uniforms.time = glm::vec4(time, 0.0f, 0.0f, 0.0f);
    std::memcpy(frame.frameUniforms.mapped, &uniforms, sizeof(uniforms));
    vmaFlushAllocation(m_Context->Allocator(), frame.frameUniforms.allocation, 0, VK_WHOLE_SIZE);

    // Record.
    VkCommandBuffer cmd = frame.commandBuffer;
    ZE_VK_CHECK(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ZE_VK_CHECK(vkBeginCommandBuffer(cmd, &begin));

    VkImage swapImage = m_Swapchain->Image(imageIndex);
    TransitionImage(cmd, swapImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    TransitionImage(cmd, m_Depth.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_DEPTH_BIT);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_Swapchain->ImageView(imageIndex);
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    std::memcpy(color.clearValue.color.float32, &m_Settings.clearColor, sizeof(float) * 4);

    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = m_Depth.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, m_Swapchain->Extent()};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &rendering);
    RecordScene(cmd, scene, frame);
    vkCmdEndRendering(cmd);

    TransitionImage(cmd, swapImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    ZE_VK_CHECK(vkEndCommandBuffer(cmd));

    // Submit.
    VkSemaphore renderFinished = m_Swapchain->RenderFinished(imageIndex);
    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitInfo.semaphore = frame.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalInfo.semaphore = renderFinished;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmd;

    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &waitInfo;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signalInfo;
    ZE_VK_CHECK(vkQueueSubmit2(m_Context->GraphicsQueue(), 1, &submit, frame.inFlight));

    // Present.
    VkSwapchainKHR swapchain = m_Swapchain->Handle();
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &imageIndex;
    VkResult presentResult = vkQueuePresentKHR(m_Context->GraphicsQueue(), &present);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
        m_SwapchainDirty = true;
    else if (presentResult != VK_SUCCESS)
        Log::Fatal("vkQueuePresentKHR failed: {}", VkResultString(presentResult));

    m_FrameIndex = (m_FrameIndex + 1) % kFramesInFlight;
}

void Renderer::RecordScene(VkCommandBuffer cmd, const Scene& scene, const FrameResources& frame)
{
    VkExtent2D extent = m_Swapchain->Extent();
    VkViewport viewport{0.0f, 0.0f, float(extent.width), float(extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1, &frame.descriptorSet,
                            0, nullptr);

    // Sky: one full-screen triangle behind everything.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyPipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_LitPipeline);
    for (const auto& entity : scene.Entities()) {
        const MeshRenderer& mr = entity->meshRenderer;
        if (!entity->active || !mr.mesh || mr.mesh->indexCount == 0)
            continue;

        DrawPushConstants push{};
        push.model = entity->transform.Matrix();
        push.color = mr.color;
        push.params = glm::vec4(mr.checkerScale, 0.0f, 0.0f, 0.0f);
        vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(push), &push);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &mr.mesh->vertexBuffer.buffer, &offset);
        vkCmdBindIndexBuffer(cmd, mr.mesh->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, mr.mesh->indexCount, 1, 0, 0, 0);
    }
}

} // namespace ze
