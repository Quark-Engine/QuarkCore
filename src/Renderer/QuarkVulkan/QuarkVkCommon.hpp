#ifndef __QUARK_VK_COMMON__
#define __QUARK_VK_COMMON__

#include "QuarkCore/QuarkCore.hpp"
#include "QuarkVkGpuAllocator.hpp"

#include <vulkan/vulkan.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace qci
{
    static constexpr int VK_MAX_FRAMES_IN_FLIGHT = 3;

    struct VkQueueFamilyIndices
    {
        std::optional<uint32_t> m_GraphicsFamily;
        std::optional<uint32_t> m_PresentFamily;

        bool isComplete() const
        {
            return m_GraphicsFamily.has_value() && m_PresentFamily.has_value();
        }
    };

    struct VkSwapChainSupportDetails
    {
        VkSurfaceCapabilitiesKHR m_Capabilities{};
        std::vector<VkSurfaceFormatKHR> m_vFormats;
        std::vector<VkPresentModeKHR> m_vPresentModes;
    };

    inline bool HasStencilComponent(VkFormat format)
    {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    inline void ThrowIfVulkanFailed(VkResult result, const char* pOperation)
    {
        if (result != VK_SUCCESS)
        {
            std::string message = std::string(pOperation) + " failed with VkResult " + std::to_string(static_cast<int>(result));
            TraceLog(LogLevel::Error, "VULKAN", message.c_str());
            throw std::runtime_error(message);
        }
    }
} // qci
#endif // __QUARK_VK_COMMON__
