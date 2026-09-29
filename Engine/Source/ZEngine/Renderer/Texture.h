#pragma once

#include "ZEngine/Renderer/VulkanCommon.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ze {

// GPU texture (RGBA8, gamma-encoded like the UI and render targets) with its descriptor set.
struct Texture {
    std::string name;
    AllocatedImage image;
    VkDescriptorSet descriptor = VK_NULL_HANDLE; // set = 0, binding = 0 of the UI pipeline
    uint32_t width = 0;
    uint32_t height = 0;
    // Cached ImGui texture handle (managed by ImGuiLayer).
    VkImageView uiView = VK_NULL_HANDLE;
    VkDescriptorSet uiTexture = VK_NULL_HANDLE;
};

struct ImageData {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels; // RGBA8
};

namespace ImageIO {
bool Load(const std::filesystem::path& path, ImageData& out);
bool SavePng(const std::filesystem::path& path, const ImageData& image);
bool IsImageFile(const std::filesystem::path& path);
} // namespace ImageIO

} // namespace ze
