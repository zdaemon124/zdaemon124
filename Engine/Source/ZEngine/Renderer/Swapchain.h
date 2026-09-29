#pragma once

#include "ZEngine/Renderer/VulkanCommon.h"

#include <vector>

namespace ze {

class VulkanContext;

class Swapchain {
public:
    Swapchain(VulkanContext& context, VkExtent2D desiredExtent, bool vsync);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    void Recreate(VkExtent2D desiredExtent);

    VkSwapchainKHR Handle() const { return m_Swapchain; }
    VkFormat Format() const { return m_Format; }
    VkExtent2D Extent() const { return m_Extent; }
    uint32_t ImageCount() const { return static_cast<uint32_t>(m_Images.size()); }
    VkImage Image(uint32_t index) const { return m_Images[index]; }
    VkImageView ImageView(uint32_t index) const { return m_Views[index]; }
    // Signalled when rendering to image `index` finished; waited on by present.
    VkSemaphore RenderFinished(uint32_t index) const { return m_RenderFinished[index]; }

private:
    void Create(VkExtent2D desiredExtent, VkSwapchainKHR oldSwapchain);
    void DestroyViews();

    VulkanContext& m_Context;
    bool m_VSync;
    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;
    VkExtent2D m_Extent{};
    std::vector<VkImage> m_Images;
    std::vector<VkImageView> m_Views;
    std::vector<VkSemaphore> m_RenderFinished;
};

} // namespace ze
