#include "QuarkVkCommandContext.hpp"

#include "../../QuarkInternal.hpp"

#include <algorithm>
#include <stdexcept>

namespace qci
{
    void QuarkVkCommandContext::Initialize(VkDevice device, uint32_t queueFamilyIndex, uint32_t workerCount)
    {
        m_Device = device;
        m_QueueFamilyIndex = queueFamilyIndex;
        m_WorkerCount = std::max(1u, workerCount);

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
        {
            m_Device = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to create Vulkan command pool.");
        }

        VkCommandPoolCreateInfo workerPoolInfo{};
        workerPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        workerPoolInfo.queueFamilyIndex = queueFamilyIndex;
        workerPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                               VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        for (uint32_t frameIdx = 0; frameIdx < static_cast<uint32_t>(VK_MAX_FRAMES_IN_FLIGHT); ++frameIdx)
        {
            auto& vArenas = m_aFrameWorkerArenas[frameIdx];
            vArenas.resize(m_WorkerCount);
            for (uint32_t workerIdx = 0; workerIdx < m_WorkerCount; ++workerIdx)
            {
                if (vkCreateCommandPool(device, &workerPoolInfo, nullptr, &vArenas[workerIdx].m_Pool) != VK_SUCCESS)
                {
                    Shutdown();
                    throw std::runtime_error("Failed to create Vulkan worker command pool.");
                }
            }
        }

        TraceLog(LogLevel::Info, "VULKAN",
                 TextFormat("Command pools created (Primary + %u worker pools x %u frames).",
                            m_WorkerCount, static_cast<uint32_t>(VK_MAX_FRAMES_IN_FLIGHT)));
    }

    void QuarkVkCommandContext::ResetFrameWorkerPools(uint32_t frameIndex)
    {
        if (m_Device == VK_NULL_HANDLE || frameIndex >= static_cast<uint32_t>(VK_MAX_FRAMES_IN_FLIGHT))
        {
            return;
        }

        auto& vArenas = m_aFrameWorkerArenas[frameIndex];
        for (WorkerCommandArena& arena : vArenas)
        {
            if (arena.m_Pool != VK_NULL_HANDLE && arena.m_UsedCount > 0)
            {
                vkResetCommandPool(m_Device, arena.m_Pool, 0);
            }
            arena.m_UsedCount = 0;
        }
    }

    VkCommandBuffer QuarkVkCommandContext::AcquireSecondaryCommandBuffer(uint32_t frameIndex, uint32_t workerIndex)
    {
        if (m_Device == VK_NULL_HANDLE || frameIndex >= static_cast<uint32_t>(VK_MAX_FRAMES_IN_FLIGHT))
        {
            return VK_NULL_HANDLE;
        }

        auto& vArenas = m_aFrameWorkerArenas[frameIndex];
        if (vArenas.empty())
        {
            return VK_NULL_HANDLE;
        }

        const uint32_t safeWorkerIdx = std::min(workerIndex, static_cast<uint32_t>(vArenas.size() - 1));
        WorkerCommandArena& arena = vArenas[safeWorkerIdx];
        if (arena.m_Pool == VK_NULL_HANDLE)
        {
            return VK_NULL_HANDLE;
        }

        if (arena.m_UsedCount < arena.m_vSecondaryBuffers.size())
        {
            return arena.m_vSecondaryBuffers[arena.m_UsedCount++];
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = arena.m_Pool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(m_Device, &allocInfo, &cmd) != VK_SUCCESS)
        {
            return VK_NULL_HANDLE;
        }

        arena.m_vSecondaryBuffers.push_back(cmd);
        arena.m_UsedCount++;
        return cmd;
    }

    void QuarkVkCommandContext::Shutdown()
    {
        if (m_Device != VK_NULL_HANDLE)
        {
            for (auto& vArenas : m_aFrameWorkerArenas)
            {
                for (WorkerCommandArena& arena : vArenas)
                {
                    arena.m_vSecondaryBuffers.clear();
                    arena.m_UsedCount = 0;
                    if (arena.m_Pool != VK_NULL_HANDLE)
                    {
                        vkDestroyCommandPool(m_Device, arena.m_Pool, nullptr);
                        arena.m_Pool = VK_NULL_HANDLE;
                    }
                }
                vArenas.clear();
            }
            if (m_CommandPool != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
                m_CommandPool = VK_NULL_HANDLE;
            }
        }
        m_Device = VK_NULL_HANDLE;
        m_WorkerCount = 1;
    }
} // qci
