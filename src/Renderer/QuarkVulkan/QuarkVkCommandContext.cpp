#include "QuarkVkCommandContext.hpp"

#include "../../QuarkInternal.hpp"

#include <stdexcept>

namespace qc {

void QuarkVkCommandContext::Initialize(VkDevice device, uint32_t queueFamilyIndex) {
    m_device = device;

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        m_device = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to create Vulkan command pool.");
    }
    TraceLog(LogLevel::Trace, "VULKAN", "Command pool created.");
}

void QuarkVkCommandContext::Shutdown() {
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    m_device = VK_NULL_HANDLE;
}

} // namespace qc