#ifndef __QUARK_VK_RENDER_TARGET__
#define __QUARK_VK_RENDER_TARGET__

#include "QuarkVkCommon.hpp"
#include "QuarkVkGpuAllocator.hpp"

#include <unordered_map>

namespace qc {

class QuarkVkRenderTarget {
public:
    void Initialize(VkDevice device, QuarkVkGpuAllocator& allocator,
                    VkFormat colorFormat, VkFormat depthFormat);
    void Shutdown();

    VkRenderPass RenderPass() const { return m_renderPass; }

    bool CreateRenderPass();
    void DestroyRenderPass();

    bool CreateTarget(uint32_t renderTargetId, VkImageView colorView,
                      uint32_t width, uint32_t height);
    void DestroyTarget(uint32_t renderTargetId);
    void DestroyFramebuffers();

    VkFramebuffer Framebuffer(uint32_t renderTargetId) const;

private:
    struct TargetData {
        VkImageView    colorView = VK_NULL_HANDLE;
        uint32_t       width  = 0;
        uint32_t       height = 0;
        VkFramebuffer  framebuffer = VK_NULL_HANDLE;
        VkImage        depthImage = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory = VK_NULL_HANDLE;
        VmaAllocation  depthAllocation = VK_NULL_HANDLE;
        VkImageView    depthView = VK_NULL_HANDLE;
    };

    bool CreateDepthResources(uint32_t width, uint32_t height, TargetData& out);
    void DestroyDepthResources(TargetData& data);

    VkDevice             m_device = VK_NULL_HANDLE;
    QuarkVkGpuAllocator* m_allocator = nullptr;
    VkFormat             m_colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat             m_depthFormat = VK_FORMAT_UNDEFINED;
    VkRenderPass         m_renderPass = VK_NULL_HANDLE;
    std::unordered_map<uint32_t, TargetData> m_targets;
};

} // namespace qc

#endif // __QUARK_VK_RENDER_TARGET__