#ifndef __QUARK_VK_RENDER_PASS__
#define __QUARK_VK_RENDER_PASS__

#include "QuarkVkCommon.hpp"

namespace qci
{
    class QuarkVkRenderPass
    {
    public:
        void Initialize(VkDevice device, VkFormat colorFormat, VkFormat depthFormat, bool multisampled = false, bool offscreen = false);
        void Shutdown();

        VkRenderPass Get() const
        {
            return m_RenderPass;
        }

    private:
        VkDevice m_Device = VK_NULL_HANDLE;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
    };
} // qci
#endif // __QUARK_VK_RENDER_PASS__
