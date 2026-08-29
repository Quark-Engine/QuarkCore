#define VMA_IMPLEMENTATION
#include "QuarkVkGpuAllocator.hpp"

#include "QuarkCore/QuarkCore.hpp"

#include <stdexcept>

namespace qc {

QuarkVkGpuAllocator::~QuarkVkGpuAllocator() {
    Shutdown();
}

bool QuarkVkGpuAllocator::Initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) {
    if (m_allocator != VK_NULL_HANDLE) {
        return true;
    }

    VmaVulkanFunctions vulkanFunctions{};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    vulkanFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
    vulkanFunctions.vkAllocateMemory = vkAllocateMemory;
    vulkanFunctions.vkFreeMemory = vkFreeMemory;
    vulkanFunctions.vkMapMemory = vkMapMemory;
    vulkanFunctions.vkUnmapMemory = vkUnmapMemory;
    vulkanFunctions.vkBindBufferMemory = vkBindBufferMemory;
    vulkanFunctions.vkBindImageMemory = vkBindImageMemory;
    vulkanFunctions.vkCreateBuffer = vkCreateBuffer;
    vulkanFunctions.vkDestroyBuffer = vkDestroyBuffer;
    vulkanFunctions.vkCreateImage = vkCreateImage;
    vulkanFunctions.vkDestroyImage = vkDestroyImage;
    vulkanFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;
    vulkanFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
    vulkanFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    const VkResult result = vmaCreateAllocator(&allocatorInfo, &m_allocator);
    if (result != VK_SUCCESS || m_allocator == VK_NULL_HANDLE) {
        TraceLog(LogLevel::Error, "VULKAN", "Failed to create VMA allocator.");
        return false;
    }

    TraceLog(LogLevel::Info, "VULKAN", "VMA allocator initialized successfully.");
    return true;
}

void QuarkVkGpuAllocator::Shutdown() {
    if (m_allocator == VK_NULL_HANDLE) {
        return;
    }

    vmaDestroyAllocator(m_allocator);
    m_allocator = VK_NULL_HANDLE;
    TraceLog(LogLevel::Info, "VULKAN", "VMA allocator destroyed.");
}

bool QuarkVkGpuAllocator::CreateBuffer(VkDeviceSize size,
                                      VkBufferUsageFlags usage,
                                      VmaMemoryUsage memoryUsage,
                                      VkBuffer& outBuffer,
                                      VmaAllocation& outAllocation,
                                      VmaAllocationCreateFlags flags) const {
    if (!IsInitialized()) {
        return false;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = memoryUsage;
    allocationInfo.flags = flags;

    outBuffer = VK_NULL_HANDLE;
    outAllocation = VK_NULL_HANDLE;

    const VkResult result = vmaCreateBuffer(m_allocator, &bufferInfo, &allocationInfo, &outBuffer, &outAllocation, nullptr);
    if (result != VK_SUCCESS) {
        TraceLog(LogLevel::Warn, "VULKAN", "VMA buffer allocation failed.");
        return false;
    }

    return true;
}

bool QuarkVkGpuAllocator::CreateImage(const VkImageCreateInfo& imageCreateInfo,
                                     VmaMemoryUsage memoryUsage,
                                     VkImage& outImage,
                                     VmaAllocation& outAllocation,
                                     VmaAllocationCreateFlags flags) const {
    if (!IsInitialized()) {
        return false;
    }

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = memoryUsage;
    allocationInfo.flags = flags;

    outImage = VK_NULL_HANDLE;
    outAllocation = VK_NULL_HANDLE;

    const VkResult result = vmaCreateImage(m_allocator,
                                          &imageCreateInfo,
                                          &allocationInfo,
                                          &outImage,
                                          &outAllocation,
                                          nullptr);
    if (result != VK_SUCCESS) {
        TraceLog(LogLevel::Warn, "VULKAN", "VMA image allocation failed.");
        return false;
    }

    return true;
}

void QuarkVkGpuAllocator::DestroyBuffer(VkBuffer buffer, VmaAllocation allocation) const {
    if (!IsInitialized() || buffer == VK_NULL_HANDLE || allocation == VK_NULL_HANDLE) {
        return;
    }

    vmaDestroyBuffer(m_allocator, buffer, allocation);
}

void QuarkVkGpuAllocator::DestroyImage(VkImage image, VmaAllocation allocation) const {
    if (!IsInitialized() || image == VK_NULL_HANDLE || allocation == VK_NULL_HANDLE) {
        return;
    }

    vmaDestroyImage(m_allocator, image, allocation);
}

VmaAllocatorInfo QuarkVkGpuAllocator::GetInfo() const {
    VmaAllocatorInfo info{};
    if (IsInitialized()) {
        vmaGetAllocatorInfo(m_allocator, &info);
    }
    return info;
}

} // namespace qc
