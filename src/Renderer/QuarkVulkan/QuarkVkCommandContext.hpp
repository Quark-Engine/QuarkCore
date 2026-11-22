#ifndef __QUARK_VK_COMMAND_CONTEXT__
#define __QUARK_VK_COMMAND_CONTEXT__

#include "QuarkVkCommon.hpp"
#include <array>
#include <vector>

namespace qci
{
    class QuarkVkCommandContext
    {
    public:
        void Initialize(VkDevice device, uint32_t queueFamilyIndex, uint32_t workerCount = 1);
        void Shutdown();

        void ResetFrameWorkerPools(uint32_t frameIndex);
        VkCommandBuffer AcquireSecondaryCommandBuffer(uint32_t frameIndex, uint32_t workerIndex);

        VkCommandPool Pool() const
        {
            return m_CommandPool;
        }

        uint32_t WorkerCount() const
        {
            return m_WorkerCount;
        }

        bool IsInitialized() const
        {
            return m_Device != VK_NULL_HANDLE;
        }

    private:
        struct WorkerCommandArena
        {
            VkCommandPool m_Pool = VK_NULL_HANDLE;
            std::vector<VkCommandBuffer> m_vSecondaryBuffers;
            uint32_t m_UsedCount = 0;
        };

        VkDevice m_Device = VK_NULL_HANDLE;
        uint32_t m_QueueFamilyIndex = 0;
        uint32_t m_WorkerCount = 1;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        std::array<std::vector<WorkerCommandArena>, VK_MAX_FRAMES_IN_FLIGHT> m_aFrameWorkerArenas{};
    };
} // qci
#endif // __QUARK_VK_COMMAND_CONTEXT__
