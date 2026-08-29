#ifndef __QUARK_VK_DEVICE__
#define __QUARK_VK_DEVICE__

#include "QuarkVkCommon.hpp"

namespace qc {

class QuarkVkDevice {
public:
    void Initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
    void Shutdown();

    VkDevice Get() const { return m_device; }
    VkQueue GraphicsQueue() const { return m_graphicsQueue; }
    VkQueue PresentQueue() const { return m_presentQueue; }
    uint32_t GraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    bool IsInitialized() const { return m_device != VK_NULL_HANDLE; }

    void CreateLogicalDevice(const VkQueueFamilyIndices& indices,
                             const std::vector<const char*>& enabledExtensions,
                             const VkPhysicalDeviceFeatures* features = nullptr);

private:
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamily = UINT32_MAX;
};

} // namespace qc

#endif // __QUARK_VK_DEVICE__
