#pragma once

#include "ZEngine/Renderer/VulkanCommon.h"

#include <functional>

namespace ze {

class Window;

// Owns the Vulkan instance, device, queues and memory allocator.
class VulkanContext {
public:
    explicit VulkanContext(Window& window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkInstance Instance() const { return m_Instance; }
    VkPhysicalDevice PhysicalDevice() const { return m_PhysicalDevice; }
    VkDevice Device() const { return m_Device; }
    VkSurfaceKHR Surface() const { return m_Surface; }
    VkQueue GraphicsQueue() const { return m_GraphicsQueue; }
    uint32_t GraphicsQueueFamily() const { return m_GraphicsFamily; }
    VmaAllocator Allocator() const { return m_Allocator; }
    const VkPhysicalDeviceProperties& Properties() const { return m_Properties; }

    // Runs a one-off command buffer and waits for it (uploads, layout setup).
    void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& record);

    AllocatedBuffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
    void DestroyBuffer(AllocatedBuffer& buffer);
    // Creates a device-local buffer and fills it through a staging buffer.
    AllocatedBuffer CreateBufferWithData(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);

    AllocatedImage CreateImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                               VkImageAspectFlags aspect, uint32_t mipLevels = 1);
    void DestroyImage(AllocatedImage& image);

private:
    void CreateInstance();
    void PickPhysicalDevice();
    void CreateDevice();
    void CreateAllocator();

    VkInstance m_Instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_Properties{};
    VkDevice m_Device = VK_NULL_HANDLE;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    uint32_t m_GraphicsFamily = 0;
    VmaAllocator m_Allocator = nullptr;

    VkCommandPool m_ImmediatePool = VK_NULL_HANDLE;
    VkCommandBuffer m_ImmediateCmd = VK_NULL_HANDLE;
    VkFence m_ImmediateFence = VK_NULL_HANDLE;
    bool m_ValidationEnabled = false;
};

} // namespace ze
