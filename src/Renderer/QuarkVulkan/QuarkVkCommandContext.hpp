#ifndef __QUARK_VK_COMMAND_CONTEXT__
#define __QUARK_VK_COMMAND_CONTEXT__

#include "QuarkVkCommon.hpp"

namespace qc {

class QuarkVkCommandContext {
public:
    void Initialize(VkDevice device, uint32_t queueFamilyIndex);
    void Shutdown();

    VkCommandPool Pool() const { return m_commandPool; }
    bool IsInitialized() const { return m_device != VK_NULL_HANDLE; }

private:
    VkDevice      m_device      = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
};

} // namespace qc

#endif // __QUARK_VK_COMMAND_CONTEXT__