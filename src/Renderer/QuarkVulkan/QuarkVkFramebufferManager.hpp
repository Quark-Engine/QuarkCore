#ifndef __QUARK_VK_FRAMEBUFFER_MANAGER__
#define __QUARK_VK_FRAMEBUFFER_MANAGER__

#include "QuarkVkCommon.hpp"

#include <vector>

namespace qc {

class QuarkVkFramebufferManager {
public:
    void Initialize(VkDevice device);
    void Shutdown(VkDevice device);

    void CreateSwapChainFramebuffers(VkDevice device,
                                     VkRenderPass renderPass,
                                     const std::vector<VkImageView>& imageViews,
                                     const VkExtent2D& extent,
                                     VkImageView msaaColorView,
                                     VkSampleCountFlagBits msaaSamples,
                                     const std::vector<VkImageView>& depthViews);

    const std::vector<VkFramebuffer>& Framebuffers() const { return m_framebuffers; }
    std::vector<VkFramebuffer>& Framebuffers() { return m_framebuffers; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;
};

} // namespace qc

#endif // __QUARK_VK_FRAMEBUFFER_MANAGER__
