#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#include <vma/vk_mem_alloc.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

bool HasValidationLayer(const std::vector<VkLayerProperties>& availableLayers) {
    for (const auto& layer : availableLayers) {
        if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            return true;
        }
    }
    return false;
}

VkResult CreateValidationInstance(VkInstance& instance) {
    uint32_t layerCount = 0;
    if (vkEnumerateInstanceLayerProperties(&layerCount, nullptr) != VK_SUCCESS || layerCount == 0) {
        return vkCreateInstance(nullptr, nullptr, &instance);
    }

    std::vector<VkLayerProperties> availableLayers(layerCount);
    if (vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data()) != VK_SUCCESS) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<const char*> enabledLayers;
    if (HasValidationLayer(availableLayers)) {
        enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "QuarkCore VMA Validation";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "QuarkCore";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &appInfo;
    info.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
    info.ppEnabledLayerNames = enabledLayers.empty() ? nullptr : enabledLayers.data();

    return vkCreateInstance(&info, nullptr, &instance);
}

bool PickGraphicsQueueFamily(VkPhysicalDevice physicalDevice, uint32_t& queueFamilyIndex) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u) {
            queueFamilyIndex = i;
            return true;
        }
    }

    return false;
}

bool CreateLogicalDevice(VkPhysicalDevice physicalDevice, VkDevice& device) {
    uint32_t queueFamilyIndex = UINT32_MAX;
    if (!PickGraphicsQueueFamily(physicalDevice, queueFamilyIndex)) {
        return false;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamilyIndex;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkPhysicalDeviceFeatures features{};
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.pEnabledFeatures = &features;

    return vkCreateDevice(physicalDevice, &info, nullptr, &device) == VK_SUCCESS;
}

bool CreateAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VmaAllocator& allocator) {
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

    return vmaCreateAllocator(&allocatorInfo, &allocator) == VK_SUCCESS && allocator != VK_NULL_HANDLE;
}

bool TestMappedBufferRoundTrip(VmaAllocator allocator) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = 4096;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) {
        std::cerr << "Failed to allocate mapped VMA buffer" << std::endl;
        return false;
    }

    void* mapped = nullptr;
    if (vmaMapMemory(allocator, allocation, &mapped) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, buffer, allocation);
        std::cerr << "Failed to map VMA buffer" << std::endl;
        return false;
    }

    std::array<uint32_t, 1024> pattern{};
    for (size_t i = 0; i < pattern.size(); ++i) {
        pattern[i] = static_cast<uint32_t>(i * 17u + 11u);
    }

    std::memcpy(mapped, pattern.data(), pattern.size() * sizeof(uint32_t));
    vmaUnmapMemory(allocator, allocation);
    vmaDestroyBuffer(allocator, buffer, allocation);
    return true;
}

bool TestBufferRecreationLoop(VmaAllocator allocator) {
    for (int i = 0; i < 16; ++i) {
        const VkDeviceSize size = static_cast<VkDeviceSize>(256u + (i * 256u));
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) != VK_SUCCESS) {
            std::cerr << "Failed to allocate repeated VMA buffer in loop iteration " << i << std::endl;
            return false;
        }

        vmaDestroyBuffer(allocator, buffer, allocation);
    }

    return true;
}

bool TestImageLifecycle(VmaAllocator allocator) {
    for (int i = 0; i < 8; ++i) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = 64u + static_cast<uint32_t>(i * 8u);
        imageInfo.extent.height = 64u + static_cast<uint32_t>(i * 8u);
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
            std::cerr << "Failed to allocate VMA image in loop iteration " << i << std::endl;
            return false;
        }

        vmaDestroyImage(allocator, image, allocation);
    }

    return true;
}

bool TestAllocatorInfo(VmaAllocator allocator) {
    VmaAllocatorInfo info{};
    vmaGetAllocatorInfo(allocator, &info);
    if (info.instance == VK_NULL_HANDLE || info.device == VK_NULL_HANDLE || info.physicalDevice == VK_NULL_HANDLE) {
        std::cerr << "VMA allocator info was not initialized correctly" << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main() {
    try {
        VkInstance instance = VK_NULL_HANDLE;
        if (CreateValidationInstance(instance) != VK_SUCCESS || instance == VK_NULL_HANDLE) {
            std::cerr << "Failed to create Vulkan instance for validation tests" << std::endl;
            return 1;
        }

        uint32_t deviceCount = 0;
        if (vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr) != VK_SUCCESS || deviceCount == 0) {
            vkDestroyInstance(instance, nullptr);
            std::cerr << "No Vulkan devices found" << std::endl;
            return 2;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        if (vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()) != VK_SUCCESS) {
            vkDestroyInstance(instance, nullptr);
            std::cerr << "Failed to enumerate Vulkan devices" << std::endl;
            return 3;
        }

        VkPhysicalDevice physicalDevice = devices[0];
        VkDevice device = VK_NULL_HANDLE;
        if (!CreateLogicalDevice(physicalDevice, device)) {
            vkDestroyInstance(instance, nullptr);
            std::cerr << "Failed to create Vulkan device" << std::endl;
            return 4;
        }

        VmaAllocator allocator = VK_NULL_HANDLE;
        if (!CreateAllocator(instance, physicalDevice, device, allocator)) {
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            std::cerr << "Failed to create VMA allocator" << std::endl;
            return 5;
        }

        if (!TestMappedBufferRoundTrip(allocator)) {
            vmaDestroyAllocator(allocator);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 6;
        }

        if (!TestBufferRecreationLoop(allocator)) {
            vmaDestroyAllocator(allocator);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 7;
        }

        if (!TestImageLifecycle(allocator)) {
            vmaDestroyAllocator(allocator);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 8;
        }

        if (!TestAllocatorInfo(allocator)) {
            vmaDestroyAllocator(allocator);
            vkDestroyDevice(device, nullptr);
            vkDestroyInstance(instance, nullptr);
            return 9;
        }

        vmaDestroyAllocator(allocator);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);

        std::cout << "Vulkan VMA regression suite passed" << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Regression test exception: " << ex.what() << std::endl;
        return 10;
    }
}
