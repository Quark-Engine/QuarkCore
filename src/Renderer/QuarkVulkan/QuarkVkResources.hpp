#ifndef __QUARK_VK_RESOURCES__
#define __QUARK_VK_RESOURCES__

#include "QuarkVkCommon.hpp"
#include "QuarkVkGpuAllocator.hpp"

#include <functional>
#include <unordered_map>

namespace qc {

struct VkTextureData {
    VkImage         image         = VK_NULL_HANDLE;
    VkDeviceMemory  memory        = VK_NULL_HANDLE;
    VmaAllocation   allocation    = VK_NULL_HANDLE;
    VkImageView     view          = VK_NULL_HANDLE;
    VkSampler       sampler       = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    uint32_t width  = 0;
    uint32_t height = 0;
    bool      isRenderTarget = false;
};

class QuarkVkResources {
public:
    using DescriptorAllocator = std::function<bool(VkDescriptorSet& outSet)>;

    void Initialize(VkDevice device, QuarkVkGpuAllocator& allocator,
                    VkCommandPool commandPool, VkQueue graphicsQueue,
                    DescriptorAllocator descriptorAllocator = nullptr);
    void Shutdown();
    bool IsInitialized() const { return m_device != VK_NULL_HANDLE; }

    uint32_t CreateTextureFromRGBA(const unsigned char* rgba, uint32_t width, uint32_t height);
    void     SetTextureSamplingMode(TextureFilterMode filterMode, int wrapMode);
    void     DestroyTexture(uint32_t textureId);
    bool     Contains(uint32_t textureId) const { return m_textures.find(textureId) != m_textures.end(); }
    const VkTextureData* Get(uint32_t textureId) const;
    VkDescriptorSet      DescriptorSet(uint32_t textureId) const;
    bool     UpdateTextureFromRGBA(uint32_t textureId, const unsigned char* rgba, uint32_t width, uint32_t height);
    bool     UpdateTextureRegionRGBA(uint32_t textureId, const unsigned char* rgba,
                                     uint32_t offsetX, uint32_t offsetY, uint32_t width, uint32_t height);

    uint32_t Import(VkTextureData tex);
    bool     TransitionImageLayout(VkImage image, VkFormat format,
                                   VkImageLayout oldLayout, VkImageLayout newLayout);
    bool     ReadImageToRGBA(VkImage image, VkFormat format, uint32_t width, uint32_t height,
                             VkImageLayout sourceLayout, void* outPixels);

private:
    bool AllocateDescriptorSet(VkDescriptorSet& outSet);
    bool WriteTextureDescriptorSet(VkTextureData& tex);
    VkCommandBuffer BeginSingleTimeCommands();
    void            EndSingleTimeCommands(VkCommandBuffer cmd);
    bool CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    VkDevice             m_device = VK_NULL_HANDLE;
    QuarkVkGpuAllocator* m_allocator = nullptr;
    VkCommandPool        m_commandPool = VK_NULL_HANDLE;
    VkQueue              m_graphicsQueue = VK_NULL_HANDLE;
    DescriptorAllocator  m_descriptorAllocator;

    std::unordered_map<uint32_t, VkTextureData> m_textures;
    uint32_t m_nextTextureId = 1;
};

} // namespace qc

#endif // __QUARK_VK_RESOURCES__