#pragma once

#include "ZEngine/Renderer/Mesh.h"
#include "ZEngine/Renderer/VulkanCommon.h"
#include "ZEngine/Scene/Camera.h"
#include "ZEngine/Scene/Primitives.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace ze {

class Window;
class VulkanContext;
class Swapchain;
class Scene;

struct RendererSettings {
    bool vsync = true;
    glm::vec4 clearColor{0.1f, 0.1f, 0.12f, 1.0f};
};

class Renderer {
public:
    static constexpr uint32_t kFramesInFlight = 2;

    Renderer(Window& window, const RendererSettings& settings = {});
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Meshes are owned by the renderer and live until it is destroyed.
    Mesh* CreateMesh(const std::string& name, const MeshData& data);
    Mesh* GetPrimitive(PrimitiveType type) const { return m_Primitives[static_cast<size_t>(type)]; }

    // Draws one frame of `scene` as seen by `camera` and presents it.
    void Render(const Scene& scene, const CameraData& camera, float time);

    // Current output size in pixels (use it for the camera aspect ratio).
    VkExtent2D OutputExtent() const;
    float AspectRatio() const;

    void WaitIdle() const;
    VulkanContext& Context() { return *m_Context; }

private:
    struct FrameResources {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        AllocatedBuffer frameUniforms;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };

    void CreateFrameResources();
    void CreateDescriptors();
    void CreatePipelines();
    void CreateDepthBuffer();
    void RecreateSwapchain();
    void RecordScene(VkCommandBuffer cmd, const Scene& scene, const FrameResources& frame);

    Window& m_Window;
    RendererSettings m_Settings;
    std::unique_ptr<VulkanContext> m_Context;
    std::unique_ptr<Swapchain> m_Swapchain;
    AllocatedImage m_Depth;
    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

    std::array<FrameResources, kFramesInFlight> m_Frames{};
    uint32_t m_FrameIndex = 0;
    bool m_SwapchainDirty = false;

    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_FrameSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_LitPipeline = VK_NULL_HANDLE;
    VkPipeline m_SkyPipeline = VK_NULL_HANDLE;

    std::vector<std::unique_ptr<Mesh>> m_Meshes;
    std::array<Mesh*, static_cast<size_t>(PrimitiveType::Count)> m_Primitives{};
};

} // namespace ze
