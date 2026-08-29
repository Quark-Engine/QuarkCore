#include "QuarkVkSwapChain.hpp"

#include "QuarkVkGpuAllocator.hpp"

#include <array>
#include <stdexcept>

namespace qc {

void QuarkVkSwapChain::Initialize(VkDevice device,
                                 VkPhysicalDevice physicalDevice,
                                 VkSurfaceKHR surface,
                                 int width,
                                 int height,
                                 const VkSwapChainSupportDetails& details,
                                 const VkQueueFamilyIndices& indices,
                                 const VkSurfaceFormatKHR& surfaceFormat,
                                 const VkPresentModeKHR& presentMode) {
    (void)physicalDevice;

    uint32_t imageCount = details.capabilities.minImageCount + 1;
    if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount) {
        imageCount = details.capabilities.maxImageCount;
    }
    m_minImageCount = details.capabilities.minImageCount;

    VkExtent2D extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
    if (details.capabilities.currentExtent.width != UINT32_MAX) {
        extent = details.capabilities.currentExtent;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    std::array<uint32_t, 2> queueFamilyIndices = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndices.size());
        createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = details.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &m_swapChain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan swapchain.");
    }

    vkGetSwapchainImagesKHR(device, m_swapChain, &imageCount, nullptr);
    m_images.resize(imageCount);
    vkGetSwapchainImagesKHR(device, m_swapChain, &imageCount, m_images.data());

    m_imageFormat = surfaceFormat.format;
    m_extent = extent;

    m_imageViews.resize(m_images.size());
    for (size_t i = 0; i < m_images.size(); ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_imageFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan swapchain image view.");
        }
    }
}

void QuarkVkSwapChain::Recreate(VkDevice device,
                                VkPhysicalDevice physicalDevice,
                                VkSurfaceKHR surface,
                                int width,
                                int height,
                                const VkSwapChainSupportDetails& details,
                                const VkQueueFamilyIndices& indices,
                                const VkSurfaceFormatKHR& surfaceFormat,
                                const VkPresentModeKHR& presentMode) {
    (void)physicalDevice;

    Shutdown(device);
    Initialize(device, physicalDevice, surface, width, height, details, indices, surfaceFormat, presentMode);
}

void QuarkVkSwapChain::CreateAttachmentResources(VkDevice device, QuarkVkGpuAllocator& allocator,
                                                 VkFormat depthFormat, VkSampleCountFlagBits samples) {
    DestroyAttachmentResources(device);

    m_allocator = &allocator;

    if (samples > VK_SAMPLE_COUNT_1_BIT) {
        CreateMSAAColorResources(device, allocator, m_imageFormat, samples);
    }

    m_depthImages.assign(m_images.size(), VK_NULL_HANDLE);
    m_depthMemories.assign(m_images.size(), VK_NULL_HANDLE);
    m_depthAllocations.assign(m_images.size(), VK_NULL_HANDLE);
    m_depthImageViews.assign(m_images.size(), VK_NULL_HANDLE);

    for (size_t i = 0; i < m_images.size(); ++i) {
        if (!CreateDepthResources(device, allocator, depthFormat, samples, static_cast<uint32_t>(i))) {
            DestroyAttachmentResources(device);
            throw std::runtime_error("Failed to create Vulkan swapchain depth resources.");
        }
    }
}

void QuarkVkSwapChain::DestroyAttachmentResources(VkDevice device) {
    if (m_allocator == nullptr) {
        return;
    }

    for (size_t i = 0; i < m_depthImageViews.size(); ++i) {
        DestroyDepthResources(device, *m_allocator, static_cast<uint32_t>(i));
    }
    m_depthImageViews.clear();
    m_depthImages.clear();
    m_depthMemories.clear();
    m_depthAllocations.clear();

    DestroyMSAAColorResources(device, *m_allocator);

    m_allocator = nullptr;
}

bool QuarkVkSwapChain::CreateDepthResources(VkDevice device, QuarkVkGpuAllocator& allocator,
                                            VkFormat depthFormat, VkSampleCountFlagBits samples,
                                            uint32_t index) {
    m_depthImages[index] = VK_NULL_HANDLE;
    m_depthMemories[index] = VK_NULL_HANDLE;
    m_depthAllocations[index] = VK_NULL_HANDLE;
    m_depthImageViews[index] = VK_NULL_HANDLE;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width  = m_extent.width;
    imageInfo.extent.height = m_extent.height;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.format        = depthFormat;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples       = samples;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocation allocation = VK_NULL_HANDLE;
    if (!allocator.CreateImage(imageInfo,
                               VMA_MEMORY_USAGE_AUTO,
                               m_depthImages[index],
                               allocation,
                               VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT)) {
        return false;
    }

    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(allocator.GetAllocator(), allocation, &allocInfo);
    m_depthMemories[index] = allocInfo.deviceMemory;
    m_depthAllocations[index] = allocation;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = m_depthImages[index];
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = depthFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (HasStencilComponent(depthFormat)) {
        viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &m_depthImageViews[index]) != VK_SUCCESS) {
        allocator.DestroyImage(m_depthImages[index], m_depthAllocations[index]);
        m_depthImages[index] = VK_NULL_HANDLE;
        m_depthMemories[index] = VK_NULL_HANDLE;
        m_depthAllocations[index] = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

void QuarkVkSwapChain::DestroyDepthResources(VkDevice device, QuarkVkGpuAllocator& allocator, uint32_t index) {
    if (m_depthImageViews.size() > index && m_depthImageViews[index] != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_depthImageViews[index], nullptr);
        m_depthImageViews[index] = VK_NULL_HANDLE;
    }
    if (m_depthImages.size() > index && m_depthImages[index] != VK_NULL_HANDLE) {
        if (m_depthAllocations.size() > index && m_depthAllocations[index] != VK_NULL_HANDLE) {
            allocator.DestroyImage(m_depthImages[index], m_depthAllocations[index]);
            m_depthAllocations[index] = VK_NULL_HANDLE;
        } else {
            vkDestroyImage(device, m_depthImages[index], nullptr);
        }
        m_depthImages[index] = VK_NULL_HANDLE;
    }
    if (m_depthMemories.size() > index) {
        m_depthMemories[index] = VK_NULL_HANDLE;
    }
}

void QuarkVkSwapChain::CreateMSAAColorResources(VkDevice device, QuarkVkGpuAllocator& allocator,
                                                VkFormat colorFormat, VkSampleCountFlagBits samples) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width  = m_extent.width;
    imageInfo.extent.height = m_extent.height;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.format        = colorFormat;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.samples       = samples;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    m_msaaColorAllocation = VK_NULL_HANDLE;
    if (!allocator.CreateImage(imageInfo,
                               VMA_MEMORY_USAGE_AUTO,
                               m_msaaColorImage,
                               m_msaaColorAllocation,
                               VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT)) {
        throw std::runtime_error("Failed to create MSAA color image.");
    }

    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(allocator.GetAllocator(), m_msaaColorAllocation, &allocInfo);
    m_msaaColorMemory = allocInfo.deviceMemory;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = m_msaaColorImage;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = colorFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &m_msaaColorImageView) != VK_SUCCESS) {
        if (m_msaaColorAllocation != VK_NULL_HANDLE) {
            allocator.DestroyImage(m_msaaColorImage, m_msaaColorAllocation);
            m_msaaColorAllocation = VK_NULL_HANDLE;
        } else if (m_msaaColorImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, m_msaaColorImage, nullptr);
        }
        m_msaaColorMemory = VK_NULL_HANDLE;
        m_msaaColorImage = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to create MSAA color image view.");
    }
}

void QuarkVkSwapChain::DestroyMSAAColorResources(VkDevice device, QuarkVkGpuAllocator& allocator) {
    if (m_msaaColorImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_msaaColorImageView, nullptr);
        m_msaaColorImageView = VK_NULL_HANDLE;
    }
    if (m_msaaColorImage != VK_NULL_HANDLE) {
        if (m_msaaColorAllocation != VK_NULL_HANDLE) {
            allocator.DestroyImage(m_msaaColorImage, m_msaaColorAllocation);
        } else {
            vkDestroyImage(device, m_msaaColorImage, nullptr);
        }
        m_msaaColorImage = VK_NULL_HANDLE;
    }
    if (m_msaaColorMemory != VK_NULL_HANDLE) {
        m_msaaColorMemory = VK_NULL_HANDLE;
    }
    m_msaaColorAllocation = VK_NULL_HANDLE;
}

void QuarkVkSwapChain::Shutdown(VkDevice device) {
    DestroyAttachmentResources(device);

    for (auto& imageView : m_imageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, imageView, nullptr);
            imageView = VK_NULL_HANDLE;
        }
    }
    m_imageViews.clear();

    if (m_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }

    m_images.clear();
    m_imageFormat = VK_FORMAT_UNDEFINED;
    m_extent = {0, 0};
    m_minImageCount = 0;
}

} // namespace qc