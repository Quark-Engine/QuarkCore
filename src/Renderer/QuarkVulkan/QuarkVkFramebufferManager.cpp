#include "QuarkVkFramebufferManager.hpp"

namespace qci
{
    void QuarkVkFramebufferManager::Initialize(VkDevice device)
    {
        m_Device = device;
        m_vFramebuffers.clear();
    }

    void QuarkVkFramebufferManager::Shutdown(VkDevice device)
    {
        for (auto framebuffer : m_vFramebuffers)
        {
            if (framebuffer != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
        }
        m_vFramebuffers.clear();
        m_Device = VK_NULL_HANDLE;
    }

    void QuarkVkFramebufferManager::CreateSwapChainFramebuffers(VkDevice device,
                                                                VkRenderPass renderPass,
                                                                const std::vector<VkImageView>& vImageViews,
                                                                const VkExtent2D& extent,
                                                                VkImageView msaaColorView,
                                                                VkSampleCountFlagBits msaaSamples,
                                                                const std::vector<VkImageView>& vDepthViews)
    {
        Shutdown(device);
        Initialize(device);

        m_vFramebuffers.resize(vImageViews.size());
        for (size_t i = 0; i < vImageViews.size(); ++i)
        {
            std::vector<VkImageView> vAttachments;
            if (msaaSamples > VK_SAMPLE_COUNT_1_BIT)
            {
                vAttachments =
                    {
                        msaaColorView, vDepthViews[i], vImageViews[i]};
            }
            else
            {
                vAttachments =
                    {
                        vImageViews[i], vDepthViews[i]};
            }

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(vAttachments.size());
            framebufferInfo.pAttachments = vAttachments.data();
            framebufferInfo.width = extent.width;
            framebufferInfo.height = extent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_vFramebuffers[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create Vulkan framebuffer.");
            }
        }
    }
} // qci
