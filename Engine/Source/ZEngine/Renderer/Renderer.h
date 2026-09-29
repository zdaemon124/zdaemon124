#pragma once

#include "ZEngine/Renderer/Font.h"
#include "ZEngine/Renderer/Mesh.h"
#include "ZEngine/Renderer/Texture.h"
#include "ZEngine/Renderer/VulkanCommon.h"
#include "ZEngine/Scene/Camera.h"
#include "ZEngine/Scene/Primitives.h"

#include <array>
#include <filesystem>
#include <unordered_set>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ze {

class Window;
class VulkanContext;
class Swapchain;
class Scene;
struct Entity;
using EntityID = uint32_t;

struct RendererSettings {
    bool vsync = true;
    glm::vec4 clearColor{0.1f, 0.1f, 0.12f, 1.0f};
};

// Off-screen color + depth images a scene is rendered into (editor viewports, game view).
struct RenderTarget {
    AllocatedImage color;
    AllocatedImage depth;
    VkExtent2D extent{};
    // Cached ImGui texture handle (managed by ImGuiLayer).
    VkImageView uiView = VK_NULL_HANDLE;
    VkDescriptorSet uiTexture = VK_NULL_HANDLE;
};

struct SceneRenderOptions {
    bool drawGrid = false;
    EntityID selected = 0;       // draws a selection outline and its collider
    bool drawAllColliders = false;
    bool drawUI = true;          // screen-space UI (off for the editor scene view)
    bool drawWorld = true;       // sky + meshes; when false the target is cleared to `background`
    glm::vec4 background{0.12f, 0.12f, 0.14f, 1.0f};
    bool frustumCulling = true;
};

class Renderer {
public:
    static constexpr uint32_t kFramesInFlight = 2;
    static constexpr uint32_t kMaxViewsPerFrame = 8;
    static constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

    Renderer(Window& window, const RendererSettings& settings = {});
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // ---- Meshes (owned by the renderer, live until it is destroyed) ----
    Mesh* CreateMesh(const std::string& name, const MeshData& data);
    Mesh* FindMesh(const std::string& name) const;
    Mesh* GetPrimitive(PrimitiveType type) const { return m_Primitives[static_cast<size_t>(type)]; }
    std::vector<std::string> MeshNames() const;

    // ---- Textures (owned by the renderer) ----
    Texture* CreateTexture(const std::string& name, const ImageData& image);
    // Loads (and caches) an image; relative paths are resolved against the asset root. Null on failure.
    Texture* LoadTexture(const std::string& path);
    // Drops a cached texture so the next LoadTexture re-reads the file (waits for the GPU).
    void UnloadTexture(const std::string& path);
    Texture* WhiteTexture() const { return m_WhiteTexture; }
    void SetAssetRoot(const std::filesystem::path& root) { m_AssetRoot = root; }
    const std::filesystem::path& AssetRoot() const { return m_AssetRoot; }
    // Default UI font (Assets/Fonts/Default.ttf, else a system font). Null if none was found.
    Font* DefaultFont();

    // ---- Render targets ----
    std::unique_ptr<RenderTarget> CreateRenderTarget(VkExtent2D extent);
    // Waits for the GPU; call outside of BeginFrame/EndFrame.
    void ResizeRenderTarget(RenderTarget& target, VkExtent2D extent);
    void DestroyRenderTarget(RenderTarget& target);

    // ---- Frame ----
    // Returns false when the frame must be skipped (minimized window, swapchain recreated).
    bool BeginFrame();
    // Renders `scene` into `target`; afterwards the target is ready to be sampled.
    void DrawScene(RenderTarget& target, const Scene& scene, const CameraData& camera,
                   const SceneRenderOptions& options = {});
    void BlitToScreen(const RenderTarget& target);
    void BeginScreenPass(bool clear);
    void EndScreenPass();
    void EndFrame();
    VkCommandBuffer CommandBuffer() const { return m_Frames[m_FrameIndex].commandBuffer; }

    // Convenience for runtime apps: draws the scene full-screen and presents.
    void RenderToScreen(const Scene& scene, const CameraData& camera);

    VkExtent2D ScreenExtent() const;
    float ScreenAspectRatio() const;
    VkFormat ScreenFormat() const;
    uint32_t ScreenImageCount() const;

    void SetTime(float seconds) { m_Time = seconds; }
    void WaitIdle() const;
    VulkanContext& Context() { return *m_Context; }

private:
    struct FrameResources {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        AllocatedBuffer uniforms; // kMaxViewsPerFrame slots
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        AllocatedBuffer uiVertices;
        uint32_t uiVertexCount = 0;
    };

    void CreateFrameResources();
    void CreateDescriptors();
    void CreatePipelines();
    void RecreateSwapchain();
    void CreateTargetImages(RenderTarget& target, VkExtent2D extent);
    void DrawMesh(VkCommandBuffer cmd, const Mesh& mesh, const glm::mat4& model, const glm::vec4& color,
                  float checker = 0.0f);
    void DrawUI(VkCommandBuffer cmd, const Scene& scene, VkExtent2D extent);
    void CreateTextureResources();
    void DestroyTexture(Texture& texture);
    void DrawColliderWire(VkCommandBuffer cmd, const Entity& entity, const glm::vec4& color);

    Window& m_Window;
    RendererSettings m_Settings;
    std::unique_ptr<VulkanContext> m_Context;
    std::unique_ptr<Swapchain> m_Swapchain;

    std::array<FrameResources, kFramesInFlight> m_Frames{};
    uint32_t m_FrameIndex = 0;
    uint32_t m_ImageIndex = 0;
    uint32_t m_ViewIndex = 0;
    bool m_FrameActive = false;
    bool m_ScreenReady = false;   // swapchain image is in COLOR_ATTACHMENT layout
    bool m_SwapchainDirty = false;
    float m_Time = 0.0f;
    VkDeviceSize m_UniformStride = 0;
    std::unique_ptr<RenderTarget> m_ScreenTarget;

    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_FrameSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_LitPipeline = VK_NULL_HANDLE;
    VkPipeline m_SkyPipeline = VK_NULL_HANDLE;
    VkPipeline m_GridPipeline = VK_NULL_HANDLE;
    VkPipeline m_WirePipeline = VK_NULL_HANDLE; // null if fillModeNonSolid is unsupported

    static constexpr uint32_t kMaxUIVertices = 65536;
    VkDescriptorSetLayout m_TextureSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_TexturePool = VK_NULL_HANDLE;
    VkSampler m_LinearSampler = VK_NULL_HANDLE;
    VkPipelineLayout m_UIPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_UIPipeline = VK_NULL_HANDLE;
    std::unordered_map<std::string, std::unique_ptr<Texture>> m_Textures;
    std::unordered_set<std::string> m_FailedTextures;
    Texture* m_WhiteTexture = nullptr;
    std::filesystem::path m_AssetRoot;
    std::unique_ptr<Font> m_DefaultFont;
    bool m_FontSearched = false;
    std::vector<UIVertex> m_UIVertexScratch; // reused every frame to avoid allocations

    std::vector<std::unique_ptr<Mesh>> m_Meshes;
    std::unordered_map<std::string, Mesh*> m_MeshByName;
    std::array<Mesh*, static_cast<size_t>(PrimitiveType::Count)> m_Primitives{};
};

} // namespace ze
