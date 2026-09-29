#include "ZEngine/Renderer/VulkanContext.h"

#include "ZEngine/Core/Window.h"

#include <GLFW/glfw3.h>

#include <cstring>
#include <optional>
#include <vector>

namespace ze {
namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        Log::Error("Vulkan: {}", data->pMessage);
    else
        Log::Warn("Vulkan: {}", data->pMessage);
    return VK_FALSE;
}

bool HasLayer(const char* name)
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& layer : layers)
        if (std::strcmp(layer.layerName, name) == 0)
            return true;
    return false;
}

bool HasDeviceExtension(VkPhysicalDevice device, const char* name)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    for (const auto& ext : extensions)
        if (std::strcmp(ext.extensionName, name) == 0)
            return true;
    return false;
}

std::optional<uint32_t> FindGraphicsPresentFamily(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
    for (uint32_t i = 0; i < count; ++i) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present);
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
            return i;
    }
    return std::nullopt;
}

} // namespace

const char* VkResultString(VkResult result)
{
    switch (result) {
#define ZE_CASE(x) case x: return #x
        ZE_CASE(VK_SUCCESS);
        ZE_CASE(VK_NOT_READY);
        ZE_CASE(VK_TIMEOUT);
        ZE_CASE(VK_INCOMPLETE);
        ZE_CASE(VK_SUBOPTIMAL_KHR);
        ZE_CASE(VK_ERROR_OUT_OF_HOST_MEMORY);
        ZE_CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY);
        ZE_CASE(VK_ERROR_INITIALIZATION_FAILED);
        ZE_CASE(VK_ERROR_DEVICE_LOST);
        ZE_CASE(VK_ERROR_MEMORY_MAP_FAILED);
        ZE_CASE(VK_ERROR_LAYER_NOT_PRESENT);
        ZE_CASE(VK_ERROR_EXTENSION_NOT_PRESENT);
        ZE_CASE(VK_ERROR_FEATURE_NOT_PRESENT);
        ZE_CASE(VK_ERROR_INCOMPATIBLE_DRIVER);
        ZE_CASE(VK_ERROR_TOO_MANY_OBJECTS);
        ZE_CASE(VK_ERROR_FORMAT_NOT_SUPPORTED);
        ZE_CASE(VK_ERROR_SURFACE_LOST_KHR);
        ZE_CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
        ZE_CASE(VK_ERROR_OUT_OF_DATE_KHR);
        ZE_CASE(VK_ERROR_UNKNOWN);
#undef ZE_CASE
    default: return "VK_ERROR_(other)";
    }
}

void TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                     VkImageAspectFlags aspect)
{
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

VulkanContext::VulkanContext(Window& window)
{
    ZE_VK_CHECK(volkInitialize());
    CreateInstance();
    ZE_VK_CHECK(glfwCreateWindowSurface(m_Instance, window.Handle(), nullptr, &m_Surface));
    PickPhysicalDevice();
    CreateDevice();
    CreateAllocator();

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_GraphicsFamily;
    ZE_VK_CHECK(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_ImmediatePool));

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = m_ImmediatePool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    ZE_VK_CHECK(vkAllocateCommandBuffers(m_Device, &allocInfo, &m_ImmediateCmd));

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    ZE_VK_CHECK(vkCreateFence(m_Device, &fenceInfo, nullptr, &m_ImmediateFence));
}

VulkanContext::~VulkanContext()
{
    vkDeviceWaitIdle(m_Device);
    vkDestroyFence(m_Device, m_ImmediateFence, nullptr);
    vkDestroyCommandPool(m_Device, m_ImmediatePool, nullptr);
    vmaDestroyAllocator(m_Allocator);
    vkDestroyDevice(m_Device, nullptr);
    vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    if (m_DebugMessenger)
        vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
    vkDestroyInstance(m_Instance, nullptr);
}

void VulkanContext::CreateInstance()
{
    uint32_t loaderVersion = volkGetInstanceVersion();
    if (loaderVersion < VK_API_VERSION_1_3)
        Log::Fatal("Vulkan 1.3 is required (loader reports {}.{}). Update your GPU driver.",
                   VK_API_VERSION_MAJOR(loaderVersion), VK_API_VERSION_MINOR(loaderVersion));

#ifdef ZE_DEBUG
    m_ValidationEnabled = HasLayer(kValidationLayer);
    if (!m_ValidationEnabled)
        Log::Warn("Validation layers not found (install the Vulkan SDK to enable them)");
#endif

    uint32_t glfwCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwCount);
    if (m_ValidationEnabled)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "ZEngine Application";
    appInfo.pEngineName = "ZEngine";
    appInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkDebugUtilsMessengerCreateInfoEXT debugInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = DebugCallback;

    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    if (m_ValidationEnabled) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &kValidationLayer;
        createInfo.pNext = &debugInfo;
    }
    ZE_VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_Instance));
    volkLoadInstance(m_Instance);

    if (m_ValidationEnabled)
        ZE_VK_CHECK(vkCreateDebugUtilsMessengerEXT(m_Instance, &debugInfo, nullptr, &m_DebugMessenger));
}

void VulkanContext::PickPhysicalDevice()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_Instance, &count, nullptr);
    if (count == 0)
        Log::Fatal("No Vulkan-capable GPU found");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_Instance, &count, devices.data());

    int bestScore = -1;
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features2.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(device, &features2);

        auto family = FindGraphicsPresentFamily(device, m_Surface);
        bool suitable = props.apiVersion >= VK_API_VERSION_1_3 && family &&
                        HasDeviceExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME) &&
                        features13.dynamicRendering && features13.synchronization2;
        if (!suitable)
            continue;

        int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                    : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                                                                  : 1;
        if (score > bestScore) {
            bestScore = score;
            m_PhysicalDevice = device;
            m_GraphicsFamily = *family;
            m_Properties = props;
        }
    }
    if (!m_PhysicalDevice)
        Log::Fatal("No GPU supports Vulkan 1.3 with dynamic rendering");

    Log::Info("GPU: {} (Vulkan {}.{}.{})", m_Properties.deviceName,
              VK_API_VERSION_MAJOR(m_Properties.apiVersion), VK_API_VERSION_MINOR(m_Properties.apiVersion),
              VK_API_VERSION_PATCH(m_Properties.apiVersion));
}

void VulkanContext::CreateDevice()
{
    VkPhysicalDeviceFeatures supported;
    vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &supported);

    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &features13;
    features2.features.samplerAnisotropy = supported.samplerAnisotropy;
    features2.features.fillModeNonSolid = supported.fillModeNonSolid;
    features2.features.wideLines = supported.wideLines;

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = m_GraphicsFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.pNext = &features2;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;
    ZE_VK_CHECK(vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device));
    volkLoadDevice(m_Device);
    vkGetDeviceQueue(m_Device, m_GraphicsFamily, 0, &m_GraphicsQueue);
}

void VulkanContext::CreateAllocator()
{
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo info{};
    info.physicalDevice = m_PhysicalDevice;
    info.device = m_Device;
    info.instance = m_Instance;
    info.vulkanApiVersion = VK_API_VERSION_1_3;
    info.pVulkanFunctions = &functions;
    ZE_VK_CHECK(vmaCreateAllocator(&info, &m_Allocator));
}

void VulkanContext::ImmediateSubmit(const std::function<void(VkCommandBuffer)>& record)
{
    ZE_VK_CHECK(vkResetCommandBuffer(m_ImmediateCmd, 0));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ZE_VK_CHECK(vkBeginCommandBuffer(m_ImmediateCmd, &begin));
    record(m_ImmediateCmd);
    ZE_VK_CHECK(vkEndCommandBuffer(m_ImmediateCmd));

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = m_ImmediateCmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    ZE_VK_CHECK(vkQueueSubmit2(m_GraphicsQueue, 1, &submit, m_ImmediateFence));
    ZE_VK_CHECK(vkWaitForFences(m_Device, 1, &m_ImmediateFence, VK_TRUE, UINT64_MAX));
    ZE_VK_CHECK(vkResetFences(m_Device, 1, &m_ImmediateFence));
}

AllocatedBuffer VulkanContext::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible)
{
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (hostVisible)
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    AllocatedBuffer buffer;
    buffer.size = size;
    VmaAllocationInfo info{};
    ZE_VK_CHECK(vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &buffer.buffer, &buffer.allocation, &info));
    buffer.mapped = info.pMappedData;
    return buffer;
}

void VulkanContext::DestroyBuffer(AllocatedBuffer& buffer)
{
    if (buffer.buffer)
        vmaDestroyBuffer(m_Allocator, buffer.buffer, buffer.allocation);
    buffer = {};
}

AllocatedBuffer VulkanContext::CreateBufferWithData(const void* data, VkDeviceSize size, VkBufferUsageFlags usage)
{
    AllocatedBuffer staging = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(staging.mapped, data, static_cast<size_t>(size));
    vmaFlushAllocation(m_Allocator, staging.allocation, 0, VK_WHOLE_SIZE);

    AllocatedBuffer buffer = CreateBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    ImmediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy region{0, 0, size};
        vkCmdCopyBuffer(cmd, staging.buffer, buffer.buffer, 1, &region);
    });
    DestroyBuffer(staging);
    return buffer;
}

AllocatedImage VulkanContext::CreateImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                                          VkImageAspectFlags aspect)
{
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {extent.width, extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    AllocatedImage image;
    image.format = format;
    image.extent = extent;
    ZE_VK_CHECK(vmaCreateImage(m_Allocator, &imageInfo, &allocInfo, &image.image, &image.allocation, nullptr));

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {aspect, 0, 1, 0, 1};
    ZE_VK_CHECK(vkCreateImageView(m_Device, &viewInfo, nullptr, &image.view));
    return image;
}

void VulkanContext::DestroyImage(AllocatedImage& image)
{
    if (image.view)
        vkDestroyImageView(m_Device, image.view, nullptr);
    if (image.image)
        vmaDestroyImage(m_Allocator, image.image, image.allocation);
    image = {};
}

} // namespace ze
