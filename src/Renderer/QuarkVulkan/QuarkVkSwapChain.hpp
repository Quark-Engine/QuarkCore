#ifndef __QUARK_VK_SWAPCHAIN__
#define __QUARK_VK_SWAPCHAIN__

#include "QuarkVkCommon.hpp"

namespace qc {

class QuarkVkGpuAllocator;

class QuarkVkSwapChain {
public:
    void Initialize(VkDevice device,
                    VkPhysicalDevice physicalDevice,
                    VkSurfaceKHR surface,
                    int width,
                    int height,
                    const VkSwapChainSupportDetails& details,
                    const VkQueueFamilyIndices& indices,
                    const VkSurfaceFormatKHR& surfaceFormat,
                    const VkPresentModeKHR& presentMode);
    void Recreate(VkDevice device,
                  VkPhysicalDevice physicalDevice,
                  VkSurfaceKHR surface,
                  int width,
                  int height,
                  const VkSwapChainSupportDetails& details,
                  const VkQueueFamilyIndices& indices,
                  const VkSurfaceFormatKHR& surfaceFormat,
                  const VkPresentModeKHR& presentMode);
    void CreateAttachmentResources(VkDevice device, QuarkVkGpuAllocator& allocator,
                                   VkFormat depthFormat, VkSampleCountFlagBits samples);
    void Shutdown(VkDevice device);

    VkSwapchainKHR Get() const { return m_swapChain; }
    VkFormat GetImageFormat() const { return m_imageFormat; }
    VkExtent2D GetExtent() const { return m_extent; }
    uint32_t GetImageCount() const { return static_cast<uint32_t>(m_images.size()); }
    uint32_t GetMinImageCount() const { return m_minImageCount; }
    const std::vector<VkImage>& Images() const { return m_images; }
    const std::vector<VkImageView>& ImageViews() const { return m_imageViews; }
    const std::vector<VkImageView>& DepthImageViews() const { return m_depthImageViews; }
    const std::vector<VkImage>& DepthImages() const { return m_depthImages; }
    const std::vector<VmaAllocation>& DepthAllocations() const { return m_depthAllocations; }
    VkImageView MsaaColorImageView() const { return m_msaaColorImageView; }

private:
    void DestroyAttachmentResources(VkDevice device);

    bool CreateDepthResources(VkDevice device, QuarkVkGpuAllocator& allocator, VkFormat depthFormat,
                              VkSampleCountFlagBits samples, uint32_t index);
    void DestroyDepthResources(VkDevice device, QuarkVkGpuAllocator& allocator, uint32_t index);
    void CreateMSAAColorResources(VkDevice device, QuarkVkGpuAllocator& allocator, VkFormat colorFormat,
                                  VkSampleCountFlagBits samples);
    void DestroyMSAAColorResources(VkDevice device, QuarkVkGpuAllocator& allocator);

    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    VkFormat m_imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{0, 0};
    uint32_t m_minImageCount = 0;
    QuarkVkGpuAllocator* m_allocator = nullptr;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    std::vector<VkImage> m_depthImages;
    std::vector<VkDeviceMemory> m_depthMemories;
    std::vector<VmaAllocation> m_depthAllocations;
    std::vector<VkImageView> m_depthImageViews;
    VkImage m_msaaColorImage = VK_NULL_HANDLE;
    VkDeviceMemory m_msaaColorMemory = VK_NULL_HANDLE;
    VmaAllocation m_msaaColorAllocation = VK_NULL_HANDLE;
    VkImageView m_msaaColorImageView = VK_NULL_HANDLE;
};

} // namespace qc

#endif // __QUARK_VK_SWAPCHAIN__