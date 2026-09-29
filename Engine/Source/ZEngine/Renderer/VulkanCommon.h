#pragma once

// Include order matters: volk first (function pointers), then GLFW's Vulkan helpers.
#include <volk.h>
#include <vk_mem_alloc.h>

#include "ZEngine/Core/Log.h"

namespace ze {
const char* VkResultString(VkResult result);
} // namespace ze

#define ZE_VK_CHECK(expr)                                                                   \
    do {                                                                                    \
        VkResult ze_vk_result_ = (expr);                                                    \
        if (ze_vk_result_ != VK_SUCCESS)                                                    \
            ::ze::Log::Fatal("{} failed: {} ({}:{})", #expr, ::ze::VkResultString(ze_vk_result_), \
                             __FILE__, __LINE__);                                           \
    } while (0)

namespace ze {

struct AllocatedBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    void* mapped = nullptr; // non-null for host-visible buffers
    VkDeviceSize size = 0;
};

struct AllocatedImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
};

// Records a layout transition with synchronization2 (coarse but correct stage masks).
void TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                     VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

} // namespace ze
