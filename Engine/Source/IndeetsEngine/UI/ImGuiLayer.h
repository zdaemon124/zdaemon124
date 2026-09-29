#pragma once

#include "IndeetsEngine/Renderer/VulkanCommon.h"

#include <imgui.h>

#include <string>

namespace ie {

class Window;
class Renderer;
struct RenderTarget;
struct Texture;

// Dear ImGui integration (GLFW + Vulkan dynamic rendering) with an editor theme.
class ImGuiLayer {
public:
    ImGuiLayer(Window& window, Renderer& renderer, const std::string& iniPath);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void BeginFrame();
    // Records ImGui draw commands into the current screen pass.
    void Render(VkCommandBuffer cmd);

    // Texture handle for showing a render target with ImGui::Image.
    ImTextureID Texture(RenderTarget& target);
    void ReleaseTexture(RenderTarget& target);
    // Texture handle for showing a loaded texture (sprite previews).
    ImTextureID Texture(ie::Texture& texture);

private:
    ImTextureID TextureFor(VkImageView view, VkImageView& cachedView, VkDescriptorSet& cachedSet);
    void ApplyTheme();
    void LoadFonts();

    Renderer& m_Renderer;
    std::string m_IniPath;
    VkFormat m_ColorFormat = VK_FORMAT_UNDEFINED;
};

} // namespace ie
