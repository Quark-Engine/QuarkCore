#ifndef __QUARK_VK_DESCRIPTOR_SET_MANAGER__
#define __QUARK_VK_DESCRIPTOR_SET_MANAGER__

#include "QuarkVkCommon.hpp"

#include <array>
#include <vector>

namespace qci
{
    class QuarkVkDescriptorSetManager
    {
    public:
        static constexpr uint32_t DESCRIPTOR_POOL_SLAB_SIZE = 256;

        void Initialize(VkDevice device);
        void Shutdown(VkDevice device);

        void CreateLayouts(VkDevice device);
        bool CreateDescriptorPoolSlab(uint32_t maxSets, VkDescriptorPool& outPool);
        bool AllocateTextureDescriptorSet(VkDevice device, VkDescriptorSet& outSet);
        bool Allocate3DDescriptorSet(VkDevice device, VkDescriptorSet& outSet, VkDescriptorPool& outPool);
        void FreeDescriptorSet(VkDevice device, VkDescriptorPool pool, VkDescriptorSet set) const;

        VkDescriptorSetLayout TextureSetLayout() const
        {
            return m_DescriptorSetLayout;
        }

        VkDescriptorSetLayout MaterialSetLayout() const
        {
            return m_DescriptorSetLayout3D;
        }

        VkDescriptorPool ImGuiDescriptorPool() const
        {
            return m_ImguiDescriptorPool;
        }

        const std::vector<VkDescriptorPool>& DescriptorPools() const
        {
            return m_vDescriptorPools;
        }

        std::vector<VkDescriptorPool>& DescriptorPools()
        {
            return m_vDescriptorPools;
        }

    private:
        VkDevice m_Device = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_DescriptorSetLayout3D = VK_NULL_HANDLE;
        std::vector<VkDescriptorPool> m_vDescriptorPools;
        VkDescriptorPool m_ImguiDescriptorPool = VK_NULL_HANDLE;
    };
} // qci
#endif // __QUARK_VK_DESCRIPTOR_SET_MANAGER__
