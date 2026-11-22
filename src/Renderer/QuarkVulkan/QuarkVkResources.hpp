#ifndef __QUARK_VK_RESOURCES__
#define __QUARK_VK_RESOURCES__

#include "QuarkVkCommon.hpp"
#include "QuarkVkGpuAllocator.hpp"

#include <functional>
#include <unordered_map>

namespace qci
{
    struct VkTextureData
    {
        VkImage m_Image = VK_NULL_HANDLE;
        VkDeviceMemory m_Memory = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VkImageView m_View = VK_NULL_HANDLE;
        VkSampler m_Sampler = VK_NULL_HANDLE;
        VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;

        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        bool m_IsRenderTarget = false;
    };

    class QuarkVkResources
    {
    public:
        using DescriptorAllocator = std::function<bool(VkDescriptorSet& outSet)>;

        void Initialize(VkDevice device, QuarkVkGpuAllocator& allocator,
                        VkCommandPool commandPool, VkQueue graphicsQueue,
                        DescriptorAllocator descriptorAllocator = nullptr);
        void Shutdown();

        bool IsInitialized() const
        {
            return m_Device != VK_NULL_HANDLE;
        }

        uint32_t CreateTextureFromRGBA(const unsigned char* pRgba, uint32_t width, uint32_t height);
        uint32_t CreateCubemapFromRGBA(const unsigned char* pRgbaFaces, uint32_t faceSize);
        void SetTextureSamplingMode(TextureFilter filterMode, int wrapMode);
        void DestroyTexture(uint32_t textureId);

        bool Contains(uint32_t textureId) const
        {
            return m_Textures.find(textureId) != m_Textures.end();
        }

        const VkTextureData* Get(uint32_t textureId) const;
        VkDescriptorSet DescriptorSet(uint32_t textureId) const;
        bool UpdateTextureFromRGBA(uint32_t textureId, const unsigned char* pRgba, uint32_t width, uint32_t height);
        bool UpdateTextureRegionRGBA(uint32_t textureId, const unsigned char* pRgba,
                                     uint32_t offsetX, uint32_t offsetY, uint32_t width, uint32_t height);

        uint32_t Import(VkTextureData tex);
        bool TransitionImageLayout(VkImage image, VkFormat format,
                                   VkImageLayout oldLayout, VkImageLayout newLayout,
                                   uint32_t layerCount = 1);
        bool ReadImageToRGBA(VkImage image, VkFormat format, uint32_t width, uint32_t height,
                             VkImageLayout sourceLayout, void* pOutPixels);

    private:
        bool AllocateDescriptorSet(VkDescriptorSet& outSet);
        bool WriteTextureDescriptorSet(VkTextureData& tex);
        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer cmd);
        uint32_t CreateTextureFromRGBAImpl(const unsigned char* pRgba, uint32_t width,
                                           uint32_t height, uint32_t layers, bool cubemap);
        bool CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height,
                               uint32_t layerCount = 1);

        VkDevice m_Device = VK_NULL_HANDLE;
        QuarkVkGpuAllocator* m_pAllocator = nullptr;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
        DescriptorAllocator m_DescriptorAllocator;

        std::unordered_map<uint32_t, VkTextureData> m_Textures;
        uint32_t m_NextTextureId = 1;
    };
} // qci
#endif // __QUARK_VK_RESOURCES__
