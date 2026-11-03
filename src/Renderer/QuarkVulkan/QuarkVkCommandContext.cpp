#include "QuarkVkCommandContext.hpp"

#include "../../QuarkInternal.hpp"

#include <algorithm>
#include <stdexcept>
namespace qci {
void QuarkVkCommandContext::Initialize(VkDevice device, uint32_t queueFamilyIndex, uint32_t workerCount) {
    m_device = device;
    m_queueFamilyIndex = queueFamilyIndex;
    m_workerCount = std::max(1u, workerCount);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        m_device = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to create Vulkan command pool.");
    }

    VkCommandPoolCreateInfo workerPoolInfo{};
    workerPoolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    workerPoolInfo.queueFamilyIndex = queueFamilyIndex;
    workerPoolInfo.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                                      VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    for (uint32_t frameIdx = 0; frameIdx < static_cast<uint32_t>(kVkMaxFramesInFlight); ++frameIdx) {
        auto& arenas = m_frameWorkerArenas[frameIdx];
        arenas.resize(m_workerCount);
        for (uint32_t workerIdx = 0; workerIdx < m_workerCount; ++workerIdx) {
            if (vkCreateCommandPool(device, &workerPoolInfo, nullptr, &arenas[workerIdx].pool) != VK_SUCCESS) {
                Shutdown();
                throw std::runtime_error("Failed to create Vulkan worker command pool.");
            }
        }
    }

    TraceLog(LogLevel::Info, "VULKAN",
             TextFormat("Command pools created (Primary + %u worker pools x %u frames).",
                        m_workerCount, static_cast<uint32_t>(kVkMaxFramesInFlight)));
}

void QuarkVkCommandContext::ResetFrameWorkerPools(uint32_t frameIndex) {
    if (m_device == VK_NULL_HANDLE || frameIndex >= static_cast<uint32_t>(kVkMaxFramesInFlight)) {
        return;
    }

    auto& arenas = m_frameWorkerArenas[frameIndex];
    for (WorkerCommandArena& arena : arenas) {
        if (arena.pool != VK_NULL_HANDLE && arena.usedCount > 0) {
            vkResetCommandPool(m_device, arena.pool, 0);
        }
        arena.usedCount = 0;
    }
}

VkCommandBuffer QuarkVkCommandContext::AcquireSecondaryCommandBuffer(uint32_t frameIndex, uint32_t workerIndex) {
    if (m_device == VK_NULL_HANDLE || frameIndex >= static_cast<uint32_t>(kVkMaxFramesInFlight)) {
        return VK_NULL_HANDLE;
    }

    auto& arenas = m_frameWorkerArenas[frameIndex];
    if (arenas.empty()) {
        return VK_NULL_HANDLE;
    }

    const uint32_t safeWorkerIdx = std::min(workerIndex, static_cast<uint32_t>(arenas.size() - 1));
    WorkerCommandArena& arena = arenas[safeWorkerIdx];
    if (arena.pool == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    if (arena.usedCount < arena.secondaryBuffers.size()) {
        return arena.secondaryBuffers[arena.usedCount++];
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = arena.pool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &cmd) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    arena.secondaryBuffers.push_back(cmd);
    arena.usedCount++;
    return cmd;
}

void QuarkVkCommandContext::Shutdown() {
    if (m_device != VK_NULL_HANDLE) {
        for (auto& arenas : m_frameWorkerArenas) {
            for (WorkerCommandArena& arena : arenas) {
                arena.secondaryBuffers.clear();
                arena.usedCount = 0;
                if (arena.pool != VK_NULL_HANDLE) {
                    vkDestroyCommandPool(m_device, arena.pool, nullptr);
                    arena.pool = VK_NULL_HANDLE;
                }
            }
            arenas.clear();
        }
        if (m_commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device, m_commandPool, nullptr);
            m_commandPool = VK_NULL_HANDLE;
        }
    }
    m_device = VK_NULL_HANDLE;
    m_workerCount = 1;
}
} // namespace qci
