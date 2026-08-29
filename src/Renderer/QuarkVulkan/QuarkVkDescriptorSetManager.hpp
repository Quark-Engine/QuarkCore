#ifndef __QUARK_VK_DESCRIPTOR_SET_MANAGER__
#define __QUARK_VK_DESCRIPTOR_SET_MANAGER__

#include "QuarkVkCommon.hpp"

#include <array>
#include <vector>

namespace qc {

class QuarkVkDescriptorSetManager {
public:
    static constexpr uint32_t kDescriptorPoolSlabSize = 256;

    void Initialize(VkDevice device);
    void Shutdown(VkDevice device);

    void CreateLayouts(VkDevice device);
    bool CreateDescriptorPoolSlab(uint32_t maxSets, VkDescriptorPool& outPool);
    bool AllocateTextureDescriptorSet(VkDevice device, VkDescriptorSet& outSet);
    bool Allocate3DDescriptorSet(VkDevice device, VkDescriptorSet& outSet, VkDescriptorPool& outPool);
    void FreeDescriptorSet(VkDevice device, VkDescriptorPool pool, VkDescriptorSet set) const;

    VkDescriptorSetLayout TextureSetLayout() const { return m_descriptorSetLayout; }
    VkDescriptorSetLayout MaterialSetLayout() const { return m_descriptorSetLayout3D; }
    VkDescriptorPool ImGuiDescriptorPool() const { return m_imguiDescriptorPool; }
    const std::vector<VkDescriptorPool>& DescriptorPools() const { return m_descriptorPools; }
    std::vector<VkDescriptorPool>& DescriptorPools() { return m_descriptorPools; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout3D = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> m_descriptorPools;
    VkDescriptorPool m_imguiDescriptorPool = VK_NULL_HANDLE;
};

} // namespace qc

#endif // __QUARK_VK_DESCRIPTOR_SET_MANAGER__
