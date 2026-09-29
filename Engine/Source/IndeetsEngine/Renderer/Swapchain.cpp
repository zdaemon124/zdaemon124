#include "IndeetsEngine/Renderer/Swapchain.h"

#include "IndeetsEngine/Renderer/VulkanContext.h"

#include <algorithm>

namespace ie {

Swapchain::Swapchain(VulkanContext& context, VkExtent2D desiredExtent, bool vsync)
    : m_Context(context), m_VSync(vsync)
{
    Create(desiredExtent, VK_NULL_HANDLE);
}

Swapchain::~Swapchain()
{
    DestroyViews();
    vkDestroySwapchainKHR(m_Context.Device(), m_Swapchain, nullptr);
}

void Swapchain::Recreate(VkExtent2D desiredExtent)
{
    vkDeviceWaitIdle(m_Context.Device());
    DestroyViews();
    VkSwapchainKHR old = m_Swapchain;
    Create(desiredExtent, old);
    vkDestroySwapchainKHR(m_Context.Device(), old, nullptr);
}

void Swapchain::Create(VkExtent2D desiredExtent, VkSwapchainKHR oldSwapchain)
{
    VkPhysicalDevice gpu = m_Context.PhysicalDevice();
    VkSurfaceKHR surface = m_Context.Surface();

    VkSurfaceCapabilitiesKHR caps;
    IE_VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps));

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, formats.data());

    // UNORM swapchain: scene shaders output gamma-encoded colors and ImGui expects UNORM.
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount, modes.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // always available, vsync
    if (!m_VSync) {
        for (auto mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
                presentMode = mode;
            else if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR && presentMode != VK_PRESENT_MODE_MAILBOX_KHR)
                presentMode = mode;
        }
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp(desiredExtent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(desiredExtent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0)
        imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface;
    info.minImageCount = imageCount;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = oldSwapchain;
    IE_VK_CHECK(vkCreateSwapchainKHR(m_Context.Device(), &info, nullptr, &m_Swapchain));

    m_Format = chosen.format;
    m_Extent = extent;

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(m_Context.Device(), m_Swapchain, &count, nullptr);
    m_Images.resize(count);
    vkGetSwapchainImagesKHR(m_Context.Device(), m_Swapchain, &count, m_Images.data());

    m_Views.resize(count);
    m_RenderFinished.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = m_Images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_Format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        IE_VK_CHECK(vkCreateImageView(m_Context.Device(), &viewInfo, nullptr, &m_Views[i]));

        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        IE_VK_CHECK(vkCreateSemaphore(m_Context.Device(), &semInfo, nullptr, &m_RenderFinished[i]));
    }
}

void Swapchain::DestroyViews()
{
    for (VkImageView view : m_Views)
        vkDestroyImageView(m_Context.Device(), view, nullptr);
    for (VkSemaphore sem : m_RenderFinished)
        vkDestroySemaphore(m_Context.Device(), sem, nullptr);
    m_Views.clear();
    m_RenderFinished.clear();
    m_Images.clear();
}

} // namespace ie
