#ifndef __QUARK_VK_RENDER_TARGET__
#define __QUARK_VK_RENDER_TARGET__

#include "QuarkVkCommon.hpp"
#include "QuarkVkGpuAllocator.hpp"

#include <unordered_map>

namespace qci
{
    class QuarkVkRenderTarget
    {
    public:
        void Initialize(VkDevice device, QuarkVkGpuAllocator& allocator,
                        VkFormat colorFormat, VkFormat depthFormat);
        void Shutdown();

        VkRenderPass RenderPass() const
        {
            return m_RenderPass;
        }

        bool CreateRenderPass();
        void DestroyRenderPass();

        bool CreateTarget(uint32_t renderTargetId, VkImageView colorView,
                          uint32_t width, uint32_t height);
        void DestroyTarget(uint32_t renderTargetId);
        void DestroyFramebuffers();

        VkFramebuffer Framebuffer(uint32_t renderTargetId) const;

    private:
        struct TargetData
        {
            VkImageView m_ColorView = VK_NULL_HANDLE;
            uint32_t m_Width = 0;
            uint32_t m_Height = 0;
            VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
            VkImage m_DepthImage = VK_NULL_HANDLE;
            VkDeviceMemory m_DepthMemory = VK_NULL_HANDLE;
            VmaAllocation m_DepthAllocation = VK_NULL_HANDLE;
            VkImageView m_DepthView = VK_NULL_HANDLE;
        };

        bool CreateDepthResources(uint32_t width, uint32_t height, TargetData& out);
        void DestroyDepthResources(TargetData& data);

        VkDevice m_Device = VK_NULL_HANDLE;
        QuarkVkGpuAllocator* m_pAllocator = nullptr;
        VkFormat m_ColorFormat = VK_FORMAT_UNDEFINED;
        VkFormat m_DepthFormat = VK_FORMAT_UNDEFINED;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        std::unordered_map<uint32_t, TargetData> m_Targets;
    };
} // qci
#endif // __QUARK_VK_RENDER_TARGET__
