#ifndef __QUARK_VK_COMMON__
#define __QUARK_VK_COMMON__

#include "QuarkCore/QuarkCore.hpp"
#include "QuarkVkGpuAllocator.hpp"

#include <vulkan/vulkan.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace qc {

struct VkQueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct VkSwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

inline bool HasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

inline void ThrowIfVulkanFailed(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        std::string message = std::string(operation) + " failed with VkResult " + std::to_string(static_cast<int>(result));
        TraceLog(LogLevel::Error, "VULKAN", message.c_str());
        throw std::runtime_error(message);
    }
}

} // namespace qc

#endif // __QUARK_VK_COMMON__
