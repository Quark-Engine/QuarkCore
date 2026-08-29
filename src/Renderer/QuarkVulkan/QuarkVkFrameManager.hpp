#ifndef __QUARK_VK_FRAME_MANAGER__
#define __QUARK_VK_FRAME_MANAGER__

#include "QuarkVkCommon.hpp"

#include <vector>

namespace qc {

struct VkFrameDataExt {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
};

class QuarkVkFrameManager {
public:
    void Initialize(VkDevice device, VkCommandPool commandPool, uint32_t frameCount);
    void Shutdown();

    uint32_t FrameCount() const { return static_cast<uint32_t>(m_frames.size()); }
    bool IsInitialized() const { return !m_frames.empty(); }

    const VkFrameDataExt& GetFrame(uint32_t index) const { return m_frames[index]; }
    VkFrameDataExt& GetFrame(uint32_t index) { return m_frames[index]; }
    VkCommandBuffer GetCommandBuffer(uint32_t index) const { return m_frames[index].commandBuffer; }

    void     BeginFrame(uint32_t frameIndex) const;
    VkResult AcquireNextImage(VkSwapchainKHR swapchain, uint32_t frameIndex,
                              uint32_t& outImageIndex, uint64_t timeout = UINT64_MAX) const;
    void     ResetFrame(uint32_t frameIndex);
    bool     Submit(uint32_t frameIndex, VkQueue graphicsQueue);
    VkResult Present(VkSwapchainKHR swapchain, uint32_t frameIndex, uint32_t imageIndex,
                     VkQueue presentQueue);

private:
    VkDevice                  m_device      = VK_NULL_HANDLE;
    VkCommandPool             m_commandPool = VK_NULL_HANDLE;
    std::vector<VkFrameDataExt> m_frames;
};

} // namespace qc

#endif // __QUARK_VK_FRAME_MANAGER__