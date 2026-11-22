#ifndef __QUARK_VK_DEVICE__
#define __QUARK_VK_DEVICE__

#include "QuarkVkCommon.hpp"

namespace qci
{
    class QuarkVkDevice
    {
    public:
        void Initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
        void Shutdown();

        VkDevice Get() const
        {
            return m_Device;
        }

        VkQueue GraphicsQueue() const
        {
            return m_GraphicsQueue;
        }

        VkQueue PresentQueue() const
        {
            return m_PresentQueue;
        }

        uint32_t GraphicsQueueFamily() const
        {
            return m_GraphicsQueueFamily;
        }

        bool IsInitialized() const
        {
            return m_Device != VK_NULL_HANDLE;
        }

        void CreateLogicalDevice(const VkQueueFamilyIndices& indices,
                                 const std::vector<const char*>& vpEnabledExtensions,
                                 const VkPhysicalDeviceFeatures* pFeatures = nullptr);

    private:
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
        VkQueue m_PresentQueue = VK_NULL_HANDLE;
        uint32_t m_GraphicsQueueFamily = UINT32_MAX;
    };
} // qci
#endif // __QUARK_VK_DEVICE__
