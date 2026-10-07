#ifndef __QUARK_VK_RENDER_PASS__
#define __QUARK_VK_RENDER_PASS__

#include "QuarkVkCommon.hpp"
namespace qci {
class QuarkVkRenderPass {
public:
    void Initialize(VkDevice device, VkFormat colorFormat, VkFormat depthFormat, bool multisampled = false, bool offscreen = false);
    void Shutdown();

    VkRenderPass Get() const { return m_renderPass; }

private:
    VkDevice     m_device = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
};
} // namespace qci
#endif // __QUARK_VK_RENDER_PASS__
