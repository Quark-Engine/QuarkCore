#ifndef __QUARK_VK_GPU_ALLOCATOR__
#define __QUARK_VK_GPU_ALLOCATOR__

#include <vma/vk_mem_alloc.h>

#include <cstdint>
#include <vector>

namespace qc {

struct VkGpuBufferAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo info{};
};

class QuarkVkGpuAllocator {
public:
    QuarkVkGpuAllocator() = default;
    ~QuarkVkGpuAllocator();

    bool Initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);
    void Shutdown();

    bool IsInitialized() const { return m_allocator != VK_NULL_HANDLE; }

    bool CreateBuffer(VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VmaMemoryUsage memoryUsage,
                      VkBuffer& outBuffer,
                      VmaAllocation& outAllocation,
                      VmaAllocationCreateFlags flags = 0) const;

    bool CreateImage(const VkImageCreateInfo& imageCreateInfo,
                     VmaMemoryUsage memoryUsage,
                     VkImage& outImage,
                     VmaAllocation& outAllocation,
                     VmaAllocationCreateFlags flags = 0) const;

    void DestroyBuffer(VkBuffer buffer, VmaAllocation allocation) const;
    void DestroyImage(VkImage image, VmaAllocation allocation) const;

    VmaAllocator GetAllocator() const { return m_allocator; }
    VmaAllocatorInfo GetInfo() const;

private:
    VmaAllocator m_allocator = VK_NULL_HANDLE;
};

} // namespace qc

#endif // __QUARK_VK_GPU_ALLOCATOR__
