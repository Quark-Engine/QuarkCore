#ifndef __QUARK_VK_FRAME_MANAGER__
#define __QUARK_VK_FRAME_MANAGER__

#include "QuarkVkCommon.hpp"

#include <vector>

namespace qci
{
    struct VkFrameDataExt
    {
        VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
        VkSemaphore m_ImageAvailable = VK_NULL_HANDLE;
        VkFence m_InFlightFence = VK_NULL_HANDLE;
    };

    class QuarkVkFrameManager
    {
    public:
        void Initialize(VkDevice device, VkCommandPool commandPool, uint32_t frameCount);
        void SetSwapchainImageCount(uint32_t imageCount);
        void Shutdown();

        uint32_t FrameCount() const
        {
            return static_cast<uint32_t>(m_vFrames.size());
        }

        bool IsInitialized() const
        {
            return !m_vFrames.empty();
        }

        const VkFrameDataExt& GetFrame(uint32_t index) const
        {
            return m_vFrames[index];
        }

        VkFrameDataExt& GetFrame(uint32_t index)
        {
            return m_vFrames[index];
        }

        VkCommandBuffer GetCommandBuffer(uint32_t index) const
        {
            return m_vFrames[index].m_CommandBuffer;
        }

        void BeginFrame(uint32_t frameIndex) const;
        VkResult AcquireNextImage(VkSwapchainKHR swapchain, uint32_t frameIndex,
                                  uint32_t& outImageIndex, uint64_t timeout = UINT64_MAX) const;
        void ResetFrame(uint32_t frameIndex);
        bool Submit(uint32_t frameIndex, uint32_t imageIndex, VkQueue graphicsQueue);
        VkResult Present(VkSwapchainKHR swapchain, uint32_t frameIndex, uint32_t imageIndex,
                         VkQueue presentQueue);

    private:
        VkDevice m_Device = VK_NULL_HANDLE;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        std::vector<VkFrameDataExt> m_vFrames;
        std::vector<VkSemaphore> m_vRenderFinishedByImage;
    };
} // qci
#endif // __QUARK_VK_FRAME_MANAGER__
