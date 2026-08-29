#include "QuarkVkDescriptorSetManager.hpp"

namespace qc {

void QuarkVkDescriptorSetManager::Initialize(VkDevice device) {
    m_device = device;
    if (m_descriptorSetLayout == VK_NULL_HANDLE || m_descriptorSetLayout3D == VK_NULL_HANDLE) {
        CreateLayouts(device);
    }
}

void QuarkVkDescriptorSetManager::Shutdown(VkDevice device) {
    for (VkDescriptorPool pool : m_descriptorPools) {
        if (pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, pool, nullptr);
        }
    }
    m_descriptorPools.clear();

    if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
    }

    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_descriptorSetLayout3D != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout3D, nullptr);
        m_descriptorSetLayout3D = VK_NULL_HANDLE;
    }

    m_device = VK_NULL_HANDLE;
}

void QuarkVkDescriptorSetManager::CreateLayouts(VkDevice device) {
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 7> material2DBindings{};
    material2DBindings[0] = samplerBinding;
    for (uint32_t binding = 5; binding <= 10; ++binding) {
        material2DBindings[binding - 4] = { binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                            VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(material2DBindings.size());
    layoutInfo.pBindings = material2DBindings.data();
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan descriptor set layout.");
    }

    std::array<VkDescriptorSetLayoutBinding, 11> bindings3D{};
    bindings3D[0] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr };
    bindings3D[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    bindings3D[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    bindings3D[3] = { 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    bindings3D[4] = { 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    for (uint32_t binding = 5; binding <= 10; ++binding) {
        bindings3D[binding] = { binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo3D{};
    layoutInfo3D.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo3D.bindingCount = static_cast<uint32_t>(bindings3D.size());
    layoutInfo3D.pBindings = bindings3D.data();
    if (vkCreateDescriptorSetLayout(device, &layoutInfo3D, nullptr, &m_descriptorSetLayout3D) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan 3D descriptor set layout.");
    }

    if (m_imguiDescriptorPool == VK_NULL_HANDLE) {
        std::array<VkDescriptorPoolSize, 11> poolSizes = {{
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        }};

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 1000 * static_cast<uint32_t>(poolSizes.size());
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_imguiDescriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan ImGui descriptor pool.");
        }
    }

    VkDescriptorPool firstSlab = VK_NULL_HANDLE;
    if (!CreateDescriptorPoolSlab(kDescriptorPoolSlabSize, firstSlab)) {
        throw std::runtime_error("Failed to create initial Vulkan descriptor pool.");
    }
    m_descriptorPools.push_back(firstSlab);
}

bool QuarkVkDescriptorSetManager::CreateDescriptorPoolSlab(uint32_t maxSets, VkDescriptorPool& outPool) {
    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 11 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets * 3 }
    }};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = maxSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    outPool = VK_NULL_HANDLE;
    return vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &outPool) == VK_SUCCESS;
}

bool QuarkVkDescriptorSetManager::AllocateTextureDescriptorSet(VkDevice device, VkDescriptorSet& outSet) {
    if (m_descriptorSetLayout == VK_NULL_HANDLE) return false;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    for (auto it = m_descriptorPools.rbegin(); it != m_descriptorPools.rend(); ++it) {
        allocInfo.descriptorPool = *it;
        VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &outSet);
        if (result == VK_SUCCESS) return true;
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
            return false;
        }
    }

    VkDescriptorPool newSlab = VK_NULL_HANDLE;
    if (!CreateDescriptorPoolSlab(kDescriptorPoolSlabSize, newSlab)) return false;
    m_descriptorPools.push_back(newSlab);

    allocInfo.descriptorPool = newSlab;
    return vkAllocateDescriptorSets(device, &allocInfo, &outSet) == VK_SUCCESS;
}

bool QuarkVkDescriptorSetManager::Allocate3DDescriptorSet(VkDevice device, VkDescriptorSet& outSet, VkDescriptorPool& outPool) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout3D;
    for (auto it = m_descriptorPools.rbegin(); it != m_descriptorPools.rend(); ++it) {
        allocInfo.descriptorPool = *it;
        VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &outSet);
        if (result == VK_SUCCESS) {
            outPool = *it;
            return true;
        }
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) return false;
    }
    VkDescriptorPool newSlab = VK_NULL_HANDLE;
    if (!CreateDescriptorPoolSlab(kDescriptorPoolSlabSize, newSlab)) return false;
    m_descriptorPools.push_back(newSlab);
    allocInfo.descriptorPool = newSlab;
    if (vkAllocateDescriptorSets(device, &allocInfo, &outSet) != VK_SUCCESS) return false;
    outPool = newSlab;
    return true;
}

void QuarkVkDescriptorSetManager::FreeDescriptorSet(VkDevice device, VkDescriptorPool pool, VkDescriptorSet set) const {
    if (set == VK_NULL_HANDLE || pool == VK_NULL_HANDLE) {
        return;
    }
    vkFreeDescriptorSets(device, pool, 1, &set);
}

} // namespace qc
