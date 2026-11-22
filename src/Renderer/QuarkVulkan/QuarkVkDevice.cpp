#include "QuarkVkDevice.hpp"

#include <set>
#include <stdexcept>
#include <vector>

namespace qci
{
    void QuarkVkDevice::Initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
    {
        m_Instance = instance;
        m_PhysicalDevice = physicalDevice;
        m_Surface = surface;
        m_Device = VK_NULL_HANDLE;
        m_GraphicsQueue = VK_NULL_HANDLE;
        m_PresentQueue = VK_NULL_HANDLE;
        m_GraphicsQueueFamily = UINT32_MAX;
    }

    void QuarkVkDevice::CreateLogicalDevice(const VkQueueFamilyIndices& indices,
                                            const std::vector<const char*>& vpEnabledExtensions,
                                            const VkPhysicalDeviceFeatures* pFeatures)
    {
        if (m_Device != VK_NULL_HANDLE)
        {
            return;
        }

        std::vector<VkDeviceQueueCreateInfo> vQueueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies =
            {
                indices.m_GraphicsFamily.value(),
                indices.m_PresentFamily.value()};

        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies)
        {
            VkDeviceQueueCreateInfo queueCreateInfo{};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            vQueueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};
        if (pFeatures != nullptr)
        {
            deviceFeatures = *pFeatures;
        }

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(vQueueCreateInfos.size());
        createInfo.pQueueCreateInfos = vQueueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(vpEnabledExtensions.size());
        createInfo.ppEnabledExtensionNames = vpEnabledExtensions.empty() ? nullptr : vpEnabledExtensions.data();
        createInfo.enabledLayerCount = 0;

        if (vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan logical device.");
        }

        m_GraphicsQueueFamily = indices.m_GraphicsFamily.value();
        vkGetDeviceQueue(m_Device, indices.m_GraphicsFamily.value(), 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_Device, indices.m_PresentFamily.value(), 0, &m_PresentQueue);
    }

    void QuarkVkDevice::Shutdown()
    {
        if (m_Device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_Device);
            vkDestroyDevice(m_Device, nullptr);
        }
        m_Instance = VK_NULL_HANDLE;
        m_PhysicalDevice = VK_NULL_HANDLE;
        m_Surface = VK_NULL_HANDLE;
        m_Device = VK_NULL_HANDLE;
        m_GraphicsQueue = VK_NULL_HANDLE;
        m_PresentQueue = VK_NULL_HANDLE;
        m_GraphicsQueueFamily = UINT32_MAX;
    }
} // qci
