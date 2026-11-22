#ifndef __QUARK_VK_SWAPCHAIN__
#define __QUARK_VK_SWAPCHAIN__

#include "QuarkVkCommon.hpp"

namespace qci
{
    class QuarkVkGpuAllocator;

    class QuarkVkSwapChain
    {
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

        VkSwapchainKHR Get() const
        {
            return m_SwapChain;
        }

        VkFormat GetImageFormat() const
        {
            return m_ImageFormat;
        }

        VkExtent2D GetExtent() const
        {
            return m_Extent;
        }

        uint32_t GetImageCount() const
        {
            return static_cast<uint32_t>(m_vImages.size());
        }

        uint32_t GetMinImageCount() const
        {
            return m_MinImageCount;
        }

        const std::vector<VkImage>& Images() const
        {
            return m_vImages;
        }

        const std::vector<VkImageView>& ImageViews() const
        {
            return m_vImageViews;
        }

        const std::vector<VkImageView>& DepthImageViews() const
        {
            return m_vDepthImageViews;
        }

        const std::vector<VkImage>& DepthImages() const
        {
            return m_vDepthImages;
        }

        const std::vector<VmaAllocation>& DepthAllocations() const
        {
            return m_vDepthAllocations;
        }

        VkImageView MsaaColorImageView() const
        {
            return m_MsaaColorImageView;
        }

    private:
        void DestroyAttachmentResources(VkDevice device);

        bool CreateDepthResources(VkDevice device, QuarkVkGpuAllocator& allocator, VkFormat depthFormat,
                                  VkSampleCountFlagBits samples, uint32_t index);
        void DestroyDepthResources(VkDevice device, QuarkVkGpuAllocator& allocator, uint32_t index);
        void CreateMSAAColorResources(VkDevice device, QuarkVkGpuAllocator& allocator, VkFormat colorFormat,
                                      VkSampleCountFlagBits samples);
        void DestroyMSAAColorResources(VkDevice device, QuarkVkGpuAllocator& allocator);

        VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
        VkFormat m_ImageFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D m_Extent{0, 0};
        uint32_t m_MinImageCount = 0;
        QuarkVkGpuAllocator* m_pAllocator = nullptr;
        std::vector<VkImage> m_vImages;
        std::vector<VkImageView> m_vImageViews;
        std::vector<VkImage> m_vDepthImages;
        std::vector<VkDeviceMemory> m_vDepthMemories;
        std::vector<VmaAllocation> m_vDepthAllocations;
        std::vector<VkImageView> m_vDepthImageViews;
        VkImage m_MsaaColorImage = VK_NULL_HANDLE;
        VkDeviceMemory m_MsaaColorMemory = VK_NULL_HANDLE;
        VmaAllocation m_MsaaColorAllocation = VK_NULL_HANDLE;
        VkImageView m_MsaaColorImageView = VK_NULL_HANDLE;
    };
} // qci
#endif // __QUARK_VK_SWAPCHAIN__
