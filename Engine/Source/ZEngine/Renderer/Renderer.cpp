#include "ZEngine/Renderer/Renderer.h"

#include "ZEngine/Core/Platform.h"
#include "ZEngine/Core/Window.h"
#include "ZEngine/Renderer/Pipeline.h"
#include "ZEngine/Renderer/Swapchain.h"
#include "ZEngine/Renderer/VulkanContext.h"
#include "ZEngine/Scene/Scene.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
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
    glm::vec4 lightDirection;
    glm::vec4 lightColor;
    glm::vec4 skyAmbient;
    glm::vec4 groundAmbient;
    glm::vec4 time;
};

struct DrawPushConstants {
    glm::mat4 model;
    glm::vec4 color;
    glm::vec4 params;
};
static_assert(sizeof(DrawPushConstants) <= 128, "Push constants must fit the guaranteed 128 bytes");

constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
const glm::vec4 kSelectionColor{1.0f, 0.55f, 0.1f, 1.0f};
const glm::vec4 kColliderColor{0.45f, 1.0f, 0.45f, 1.0f};

} // namespace

Renderer::Renderer(Window& window, const RendererSettings& settings)
    : m_Window(window), m_Settings(settings)
{
    m_Context = std::make_unique<VulkanContext>(window);

    uint32_t width = 0, height = 0;
    window.GetFramebufferSize(width, height);
    m_Swapchain = std::make_unique<Swapchain>(*m_Context, VkExtent2D{width, height}, settings.vsync);
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

    if (m_ScreenTarget)
        DestroyRenderTarget(*m_ScreenTarget);
    for (auto& mesh : m_Meshes) {
        m_Context->DestroyBuffer(mesh->vertexBuffer);
        m_Context->DestroyBuffer(mesh->indexBuffer);
    }
    m_Meshes.clear();

    for (VkPipeline p : {m_LitPipeline, m_SkyPipeline, m_GridPipeline, m_WirePipeline})
        if (p)
            vkDestroyPipeline(device, p, nullptr);
    vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_FrameSetLayout, nullptr);

    for (auto& frame : m_Frames) {
        m_Context->DestroyBuffer(frame.uniforms);
        vkDestroyFence(device, frame.inFlight, nullptr);
        vkDestroySemaphore(device, frame.imageAvailable, nullptr);
        vkDestroyCommandPool(device, frame.commandPool, nullptr);
    }
    m_Swapchain.reset();
    m_Context.reset();
}

// ---------------------------------------------------------------- meshes

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
    m_MeshByName[name] = mesh.get();
    m_Meshes.push_back(std::move(mesh));
    return m_Meshes.back().get();
}

Mesh* Renderer::FindMesh(const std::string& name) const
{
    auto it = m_MeshByName.find(name);
    return it != m_MeshByName.end() ? it->second : nullptr;
}

std::vector<std::string> Renderer::MeshNames() const
{
    std::vector<std::string> names;
    for (const auto& mesh : m_Meshes)
        names.push_back(mesh->name);
    return names;
}

// ---------------------------------------------------------------- render targets

std::unique_ptr<RenderTarget> Renderer::CreateRenderTarget(VkExtent2D extent)
{
    auto target = std::make_unique<RenderTarget>();
    CreateTargetImages(*target, extent);
    return target;
}

void Renderer::CreateTargetImages(RenderTarget& target, VkExtent2D extent)
{
    extent.width = std::max(extent.width, 1u);
    extent.height = std::max(extent.height, 1u);
    target.extent = extent;
    target.color = m_Context->CreateImage(extent, kColorFormat,
                                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                          VK_IMAGE_ASPECT_COLOR_BIT);
    target.depth = m_Context->CreateImage(extent, kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                          VK_IMAGE_ASPECT_DEPTH_BIT);

    // Start in a samplable layout so UI can show the target before it is first rendered.
    m_Context->ImmediateSubmit([&](VkCommandBuffer cmd) {
        TransitionImage(cmd, target.color.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkClearColorValue clear{};
        std::memcpy(clear.float32, &m_Settings.clearColor, sizeof(float) * 4);
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, target.color.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
        TransitionImage(cmd, target.color.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
}

void Renderer::ResizeRenderTarget(RenderTarget& target, VkExtent2D extent)
{
    extent.width = std::max(extent.width, 1u);
    extent.height = std::max(extent.height, 1u);
    if (extent.width == target.extent.width && extent.height == target.extent.height)
        return;
    WaitIdle();
    m_Context->DestroyImage(target.color);
    m_Context->DestroyImage(target.depth);
    CreateTargetImages(target, extent);
}

void Renderer::DestroyRenderTarget(RenderTarget& target)
{
    m_Context->DestroyImage(target.color);
    m_Context->DestroyImage(target.depth);
    target.extent = {};
}

// ---------------------------------------------------------------- setup

void Renderer::CreateFrameResources()
{
    VkDevice device = m_Context->Device();
    VkDeviceSize alignment = m_Context->Properties().limits.minUniformBufferOffsetAlignment;
    m_UniformStride = (sizeof(FrameUniforms) + alignment - 1) & ~(alignment - 1);

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

        frame.uniforms = m_Context->CreateBuffer(m_UniformStride * kMaxViewsPerFrame,
                                                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
    }
}

void Renderer::CreateDescriptors()
{
    VkDevice device = m_Context->Device();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    ZE_VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_FrameSetLayout));

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kFramesInFlight};
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

        VkDescriptorBufferInfo bufferInfo{frame.uniforms.buffer, 0, sizeof(FrameUniforms)};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = frame.descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
}

void Renderer::CreatePipelines()
{
    VkDevice device = m_Context->Device();

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = kPushStages;
    pushRange.size = sizeof(DrawPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_FrameSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    ZE_VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_PipelineLayout));

    const auto dir = Platform::ExecutableDir() / "shaders";
    GraphicsPipelineDesc base;
    base.layout = m_PipelineLayout;
    base.colorFormat = kColorFormat;
    base.depthFormat = kDepthFormat;

    GraphicsPipelineDesc lit = base;
    lit.vertexShader = dir / "Lit.vert.spv";
    lit.fragmentShader = dir / "Lit.frag.spv";
    m_LitPipeline = CreateGraphicsPipeline(device, lit);

    GraphicsPipelineDesc sky = base;
    sky.vertexShader = dir / "Sky.vert.spv";
    sky.fragmentShader = dir / "Sky.frag.spv";
    sky.cullMode = VK_CULL_MODE_NONE;
    sky.depthTest = false;
    sky.depthWrite = false;
    sky.meshVertexInput = false;
    m_SkyPipeline = CreateGraphicsPipeline(device, sky);

    GraphicsPipelineDesc grid = base;
    grid.vertexShader = dir / "Lit.vert.spv";
    grid.fragmentShader = dir / "Grid.frag.spv";
    grid.cullMode = VK_CULL_MODE_NONE;
    grid.depthWrite = false;
    grid.alphaBlend = true;
    m_GridPipeline = CreateGraphicsPipeline(device, grid);

    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(m_Context->PhysicalDevice(), &features);
    if (features.fillModeNonSolid) {
        GraphicsPipelineDesc wire = base;
        wire.vertexShader = dir / "Lit.vert.spv";
        wire.fragmentShader = dir / "Unlit.frag.spv";
        wire.polygonMode = VK_POLYGON_MODE_LINE;
        wire.cullMode = VK_CULL_MODE_NONE;
        wire.depthWrite = false;
        wire.depthBiasConstant = -2.0f;
        wire.depthBiasSlope = -2.0f;
        m_WirePipeline = CreateGraphicsPipeline(device, wire);
    } else {
        Log::Warn("GPU does not support wireframe rendering; selection outlines are disabled");
    }
}

void Renderer::RecreateSwapchain()
{
    uint32_t width = 0, height = 0;
    m_Window.GetFramebufferSize(width, height);
    if (width == 0 || height == 0)
        return; // minimized, try again later
    m_Swapchain->Recreate({width, height});
    m_SwapchainDirty = false;
}

VkExtent2D Renderer::ScreenExtent() const { return m_Swapchain->Extent(); }
VkFormat Renderer::ScreenFormat() const { return m_Swapchain->Format(); }
uint32_t Renderer::ScreenImageCount() const { return m_Swapchain->ImageCount(); }

float Renderer::ScreenAspectRatio() const
{
    VkExtent2D e = m_Swapchain->Extent();
    return e.height > 0 ? float(e.width) / float(e.height) : 1.0f;
}

void Renderer::WaitIdle() const { vkDeviceWaitIdle(m_Context->Device()); }

// ---------------------------------------------------------------- frame

bool Renderer::BeginFrame()
{
    if (m_Window.ConsumeResized())
        m_SwapchainDirty = true;
    if (m_Window.IsMinimized())
        return false;
    if (m_SwapchainDirty) {
        RecreateSwapchain();
        if (m_SwapchainDirty)
            return false;
    }

    VkDevice device = m_Context->Device();
    FrameResources& frame = m_Frames[m_FrameIndex];
    ZE_VK_CHECK(vkWaitForFences(device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX));

    VkResult acquire = vkAcquireNextImageKHR(device, m_Swapchain->Handle(), UINT64_MAX, frame.imageAvailable,
                                             VK_NULL_HANDLE, &m_ImageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        m_SwapchainDirty = true;
        return false;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
        Log::Fatal("vkAcquireNextImageKHR failed: {}", VkResultString(acquire));
    ZE_VK_CHECK(vkResetFences(device, 1, &frame.inFlight));

    ZE_VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ZE_VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &begin));

    m_ViewIndex = 0;
    m_ScreenReady = false;
    m_FrameActive = true;
    return true;
}

void Renderer::DrawScene(RenderTarget& target, const Scene& scene, const CameraData& camera,
                         const SceneRenderOptions& options)
{
    if (!m_FrameActive)
        return;
    if (m_ViewIndex >= kMaxViewsPerFrame) {
        Log::Warn("Too many scene views in one frame (max {})", kMaxViewsPerFrame);
        return;
    }
    FrameResources& frame = m_Frames[m_FrameIndex];
    VkCommandBuffer cmd = frame.commandBuffer;
    uint32_t uniformOffset = static_cast<uint32_t>(m_ViewIndex++ * m_UniformStride);

    // Per-view uniforms.
    FrameUniforms u{};
    u.view = camera.view;
    u.projection = camera.projection;
    u.viewProjection = camera.projection * camera.view;
    u.inverseViewProjection = glm::inverse(u.viewProjection);
    u.cameraPosition = glm::vec4(camera.position, 1.0f);
    if (const Entity* light = scene.MainLight()) {
        u.lightDirection = glm::vec4(glm::normalize(light->transform.Forward()), light->light->intensity);
        u.lightColor = glm::vec4(light->light->color, 1.0f);
    } else {
        u.lightDirection = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
        u.lightColor = glm::vec4(0.0f);
    }
    u.skyAmbient = glm::vec4(scene.settings.skyAmbient, scene.settings.ambientIntensity);
    u.groundAmbient = glm::vec4(scene.settings.groundAmbient, 1.0f);
    u.time = glm::vec4(m_Time, 0.0f, 0.0f, 0.0f);
    std::memcpy(static_cast<char*>(frame.uniforms.mapped) + uniformOffset, &u, sizeof(u));
    vmaFlushAllocation(m_Context->Allocator(), frame.uniforms.allocation, uniformOffset, sizeof(u));

    TransitionImage(cmd, target.color.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    TransitionImage(cmd, target.depth.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_DEPTH_BIT);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = target.color.view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    std::memcpy(color.clearValue.color.float32, &m_Settings.clearColor, sizeof(float) * 4);

    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = target.depth.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, target.extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &rendering);

    VkViewport viewport{0.0f, 0.0f, float(target.extent.width), float(target.extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, target.extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1, &frame.descriptorSet, 1,
                            &uniformOffset);

    // Sky: one full-screen triangle behind everything.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_SkyPipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // Opaque geometry.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_LitPipeline);
    for (const auto& entity : scene.Entities()) {
        if (!entity->active || !entity->meshRenderer)
            continue;
        const MeshRendererComponent& mr = *entity->meshRenderer;
        if (const Mesh* mesh = FindMesh(mr.mesh))
            DrawMesh(cmd, *mesh, entity->transform.Matrix(), mr.color, mr.checkerScale);
    }

    // Editor overlays.
    if (options.drawGrid) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GridPipeline);
        glm::vec3 center = glm::floor(camera.position / 10.0f) * 10.0f;
        glm::mat4 model = glm::translate(glm::mat4(1.0f), {center.x, 0.0f, center.z}) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(60.0f));
        DrawMesh(cmd, *GetPrimitive(PrimitiveType::Plane), model, glm::vec4(1.0f));
    }
    if (m_WirePipeline) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_WirePipeline);
        for (const auto& entity : scene.Entities()) {
            bool selected = entity->id == options.selected && options.selected != 0;
            if (!entity->active || (!selected && !options.drawAllColliders))
                continue;
            if (selected && entity->meshRenderer)
                if (const Mesh* mesh = FindMesh(entity->meshRenderer->mesh))
                    DrawMesh(cmd, *mesh, entity->transform.Matrix(), kSelectionColor);
            if (entity->collider)
                DrawColliderWire(cmd, *entity, kColliderColor);
        }
    }

    vkCmdEndRendering(cmd);
    TransitionImage(cmd, target.color.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Renderer::DrawMesh(VkCommandBuffer cmd, const Mesh& mesh, const glm::mat4& model, const glm::vec4& color,
                        float checker)
{
    if (mesh.indexCount == 0)
        return;
    DrawPushConstants push{model, color, glm::vec4(checker, 0.0f, 0.0f, 0.0f)};
    vkCmdPushConstants(cmd, m_PipelineLayout, kPushStages, 0, sizeof(push), &push);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer.buffer, &offset);
    vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
}

void Renderer::DrawColliderWire(VkCommandBuffer cmd, const Entity& entity, const glm::vec4& color)
{
    // Mirrors the shape sizing used by PhysicsWorld.
    const ColliderComponent& c = *entity.collider;
    const glm::vec3 s = glm::abs(entity.transform.scale);
    glm::mat4 base = glm::translate(glm::mat4(1.0f), entity.transform.position) *
                     glm::mat4_cast(entity.transform.rotation) *
                     glm::translate(glm::mat4(1.0f), c.center * s);
    switch (c.shape) {
    case ColliderShape::Box:
        DrawMesh(cmd, *GetPrimitive(PrimitiveType::Cube), glm::scale(base, c.size * s), color);
        break;
    case ColliderShape::Sphere: {
        float r = c.radius * std::max({s.x, s.y, s.z});
        DrawMesh(cmd, *GetPrimitive(PrimitiveType::Sphere), glm::scale(base, glm::vec3(r * 2.0f)), color);
        break;
    }
    case ColliderShape::Capsule: {
        float r = c.radius * std::max(s.x, s.z);
        float h = std::max(c.height * s.y, r * 2.0f);
        DrawMesh(cmd, *GetPrimitive(PrimitiveType::Capsule), glm::scale(base, glm::vec3(r * 2.0f, h * 0.5f, r * 2.0f)),
                 color);
        break;
    }
    }
}

void Renderer::BlitToScreen(const RenderTarget& target)
{
    if (!m_FrameActive)
        return;
    VkCommandBuffer cmd = CommandBuffer();
    VkImage swapImage = m_Swapchain->Image(m_ImageIndex);
    VkExtent2D screen = m_Swapchain->Extent();

    TransitionImage(cmd, target.color.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    TransitionImage(cmd, swapImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkImageBlit2 region{VK_STRUCTURE_TYPE_IMAGE_BLIT_2};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = {int32_t(target.extent.width), int32_t(target.extent.height), 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[1] = {int32_t(screen.width), int32_t(screen.height), 1};

    VkBlitImageInfo2 blit{VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2};
    blit.srcImage = target.color.image;
    blit.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blit.dstImage = swapImage;
    blit.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blit.regionCount = 1;
    blit.pRegions = &region;
    blit.filter = VK_FILTER_LINEAR;
    vkCmdBlitImage2(cmd, &blit);

    TransitionImage(cmd, target.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    TransitionImage(cmd, swapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    m_ScreenReady = true;
}

void Renderer::BeginScreenPass(bool clear)
{
    VkCommandBuffer cmd = CommandBuffer();
    if (!m_ScreenReady) {
        TransitionImage(cmd, m_Swapchain->Image(m_ImageIndex), VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        clear = true;
        m_ScreenReady = true;
    }
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = m_Swapchain->ImageView(m_ImageIndex);
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    std::memcpy(color.clearValue.color.float32, &m_Settings.clearColor, sizeof(float) * 4);

    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, m_Swapchain->Extent()};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(cmd, &rendering);
}

void Renderer::EndScreenPass() { vkCmdEndRendering(CommandBuffer()); }

void Renderer::EndFrame()
{
    if (!m_FrameActive)
        return;
    if (!m_ScreenReady) {
        BeginScreenPass(true);
        EndScreenPass();
    }
    FrameResources& frame = m_Frames[m_FrameIndex];
    VkCommandBuffer cmd = frame.commandBuffer;
    TransitionImage(cmd, m_Swapchain->Image(m_ImageIndex), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    ZE_VK_CHECK(vkEndCommandBuffer(cmd));

    VkSemaphore renderFinished = m_Swapchain->RenderFinished(m_ImageIndex);
    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitInfo.semaphore = frame.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalInfo.semaphore = renderFinished;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
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

    VkSwapchainKHR swapchain = m_Swapchain->Handle();
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &m_ImageIndex;
    VkResult result = vkQueuePresentKHR(m_Context->GraphicsQueue(), &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        m_SwapchainDirty = true;
    else if (result != VK_SUCCESS)
        Log::Fatal("vkQueuePresentKHR failed: {}", VkResultString(result));

    m_FrameIndex = (m_FrameIndex + 1) % kFramesInFlight;
    m_FrameActive = false;
}

void Renderer::RenderToScreen(const Scene& scene, const CameraData& camera)
{
    VkExtent2D screen = ScreenExtent();
    if (!m_ScreenTarget)
        m_ScreenTarget = CreateRenderTarget(screen);
    else
        ResizeRenderTarget(*m_ScreenTarget, screen);

    if (!BeginFrame())
        return;
    DrawScene(*m_ScreenTarget, scene, camera);
    BlitToScreen(*m_ScreenTarget);
    EndFrame();
}

} // namespace ze
