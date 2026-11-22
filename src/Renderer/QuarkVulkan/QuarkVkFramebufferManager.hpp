#ifndef __QUARK_VK_FRAMEBUFFER_MANAGER__
#define __QUARK_VK_FRAMEBUFFER_MANAGER__

#include "QuarkVkCommon.hpp"

#include <vector>

namespace qci
{
    class QuarkVkFramebufferManager
    {
    public:
        void Initialize(VkDevice device);
        void Shutdown(VkDevice device);

        void CreateSwapChainFramebuffers(VkDevice device,
                                         VkRenderPass renderPass,
                                         const std::vector<VkImageView>& vImageViews,
                                         const VkExtent2D& extent,
                                         VkImageView msaaColorView,
                                         VkSampleCountFlagBits msaaSamples,
                                         const std::vector<VkImageView>& vDepthViews);

        const std::vector<VkFramebuffer>& Framebuffers() const
        {
            return m_vFramebuffers;
        }

        std::vector<VkFramebuffer>& Framebuffers()
        {
            return m_vFramebuffers;
        }

    private:
        VkDevice m_Device = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_vFramebuffers;
    };
} // qci
#endif // __QUARK_VK_FRAMEBUFFER_MANAGER__
