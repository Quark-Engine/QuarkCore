#include "QuarkVkFramebufferManager.hpp"

namespace qc {

void QuarkVkFramebufferManager::Initialize(VkDevice device) {
    m_device = device;
    m_framebuffers.clear();
}

void QuarkVkFramebufferManager::Shutdown(VkDevice device) {
    for (auto framebuffer : m_framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
    }
    m_framebuffers.clear();
    m_device = VK_NULL_HANDLE;
}

void QuarkVkFramebufferManager::CreateSwapChainFramebuffers(VkDevice device,
                                                            VkRenderPass renderPass,
                                                            const std::vector<VkImageView>& imageViews,
                                                            const VkExtent2D& extent,
                                                            VkImageView msaaColorView,
                                                            VkSampleCountFlagBits msaaSamples,
                                                            const std::vector<VkImageView>& depthViews) {
    Shutdown(device);
    Initialize(device);

    m_framebuffers.resize(imageViews.size());
    for (size_t i = 0; i < imageViews.size(); ++i) {
        std::vector<VkImageView> attachments;
        if (msaaSamples > VK_SAMPLE_COUNT_1_BIT) {
            attachments = { msaaColorView, depthViews[i], imageViews[i] };
        } else {
            attachments = { imageViews[i], depthViews[i] };
        }

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan framebuffer.");
        }
    }
}

} // namespace qc
