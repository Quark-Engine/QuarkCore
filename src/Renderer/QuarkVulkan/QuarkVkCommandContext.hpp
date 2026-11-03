#ifndef __QUARK_VK_COMMAND_CONTEXT__
#define __QUARK_VK_COMMAND_CONTEXT__

#include "QuarkVkCommon.hpp"
#include <array>
#include <vector>

namespace qci {
class QuarkVkCommandContext {
public:
    void Initialize(VkDevice device, uint32_t queueFamilyIndex, uint32_t workerCount = 1);
    void Shutdown();

    void ResetFrameWorkerPools(uint32_t frameIndex);
    VkCommandBuffer AcquireSecondaryCommandBuffer(uint32_t frameIndex, uint32_t workerIndex);

    VkCommandPool Pool() const { return m_commandPool; }
    uint32_t WorkerCount() const { return m_workerCount; }
    bool IsInitialized() const { return m_device != VK_NULL_HANDLE; }

private:
    struct WorkerCommandArena {
        VkCommandPool pool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> secondaryBuffers;
        uint32_t usedCount = 0;
    };

    VkDevice      m_device           = VK_NULL_HANDLE;
    uint32_t      m_queueFamilyIndex = 0;
    uint32_t      m_workerCount      = 1;
    VkCommandPool m_commandPool      = VK_NULL_HANDLE;
    std::array<std::vector<WorkerCommandArena>, kVkMaxFramesInFlight> m_frameWorkerArenas{};
};
} // namespace qci
#endif // __QUARK_VK_COMMAND_CONTEXT__
