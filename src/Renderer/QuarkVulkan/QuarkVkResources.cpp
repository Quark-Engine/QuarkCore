#include "QuarkVkResources.hpp"

#include "../../QuarkInternal.hpp"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace qc {

void QuarkVkResources::Initialize(VkDevice device, QuarkVkGpuAllocator& allocator,
                                  VkCommandPool commandPool, VkQueue graphicsQueue,
                                  DescriptorAllocator descriptorAllocator) {
    m_device              = device;
    m_allocator           = &allocator;
    m_commandPool         = commandPool;
    m_graphicsQueue       = graphicsQueue;
    m_descriptorAllocator = std::move(descriptorAllocator);
}

void QuarkVkResources::Shutdown() {
    for (const auto& [id, tex] : m_textures) {
        if (tex.sampler != VK_NULL_HANDLE) vkDestroySampler(m_device, tex.sampler, nullptr);
        if (tex.view != VK_NULL_HANDLE) vkDestroyImageView(m_device, tex.view, nullptr);
        if (tex.image != VK_NULL_HANDLE) {
            if (tex.allocation != VK_NULL_HANDLE && m_allocator != nullptr) {
                m_allocator->DestroyImage(tex.image, tex.allocation);
            } else {
                vkDestroyImage(m_device, tex.image, nullptr);
            }
        }
    }
    m_textures.clear();
    m_device = VK_NULL_HANDLE;
}

const VkTextureData* QuarkVkResources::Get(uint32_t textureId) const {
    const auto it = m_textures.find(textureId);
    return it == m_textures.end() ? nullptr : &it->second;
}

VkDescriptorSet QuarkVkResources::DescriptorSet(uint32_t textureId) const {
    const auto it = m_textures.find(textureId);
    return it == m_textures.end() ? VK_NULL_HANDLE : it->second.descriptorSet;
}

uint32_t QuarkVkResources::Import(VkTextureData tex) {
    if (m_device == VK_NULL_HANDLE) {
        return 0u;
    }
    const uint32_t id = m_nextTextureId++;
    m_textures[id] = tex;
    return id;
}

bool QuarkVkResources::AllocateDescriptorSet(VkDescriptorSet& outSet) {
    if (!m_descriptorAllocator) {
        return false;
    }
    return m_descriptorAllocator(outSet);
}

void QuarkVkResources::SetTextureSamplingMode(TextureFilterMode filterMode, int wrapMode) {
    if (m_device == VK_NULL_HANDLE) {
        return;
    }

    const VkFilter filter = (filterMode == TextureFilterMode::Nearest)
        ? VK_FILTER_NEAREST
        : VK_FILTER_LINEAR;

    const VkSamplerAddressMode addressMode = [wrapMode]() {
        switch (wrapMode) {
            case TEXTURE_WRAP_CLAMP:
                return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case TEXTURE_WRAP_MIRROR_REPEAT:
                return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case TEXTURE_WRAP_MIRROR_CLAMP:
                return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
            case TEXTURE_WRAP_REPEAT:
            default:
                return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    }();

    for (auto& [id, tex] : m_textures) {
        if (tex.sampler == VK_NULL_HANDLE || tex.view == VK_NULL_HANDLE) {
            continue;
        }

        VkSampler newSampler = VK_NULL_HANDLE;
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = filter;
        samplerInfo.minFilter = filter;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = addressMode;
        samplerInfo.addressModeV = addressMode;
        samplerInfo.addressModeW = addressMode;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxLod = 1.0f;

        if (vkCreateSampler(m_device, &samplerInfo, nullptr, &newSampler) != VK_SUCCESS) {
            continue;
        }

        vkDestroySampler(m_device, tex.sampler, nullptr);
        tex.sampler = newSampler;
        if (tex.descriptorSet != VK_NULL_HANDLE) {
            WriteTextureDescriptorSet(tex);
        }
    }
}

bool QuarkVkResources::WriteTextureDescriptorSet(VkTextureData& tex) {
    if (tex.view == VK_NULL_HANDLE || tex.sampler == VK_NULL_HANDLE) {
        return false;
    }

    VkDescriptorImageInfo imageDescriptor{};
    imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageDescriptor.imageView   = tex.view;
    imageDescriptor.sampler     = tex.sampler;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = tex.descriptorSet;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imageDescriptor;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    return true;
}

uint32_t QuarkVkResources::CreateTextureFromRGBA(const unsigned char* rgba,
                                                 uint32_t width, uint32_t height) {
    if (!rgba || width == 0 || height == 0 || m_device == VK_NULL_HANDLE || m_allocator == nullptr) {
        TraceLog(LogLevel::Warn, "TEXTURE", "[Vulkan] Cannot create texture: invalid parameters (null data or zero size)");
        return 0u;
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4u;
    TraceLog(LogLevel::Trace, "TEXTURE", TextFormat("[Vulkan] Creating GPU texture: %ux%u (%llu bytes RGBA8)",
        width, height, static_cast<unsigned long long>(imageSize)));

    VkBuffer        stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation   stagingAllocation = VK_NULL_HANDLE;
    if (!m_allocator->CreateBuffer(imageSize,
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VMA_MEMORY_USAGE_AUTO,
                                   stagingBuffer,
                                   stagingAllocation,
                                   VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                   VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed to allocate staging buffer for texture upload");
        return 0u;
    }

    void* mapped = nullptr;
    if (vmaMapMemory(m_allocator->GetAllocator(), stagingAllocation, &mapped) != VK_SUCCESS) {
        m_allocator->DestroyBuffer(stagingBuffer, stagingAllocation);
        return 0u;
    }
    std::memcpy(mapped, rgba, static_cast<size_t>(imageSize));
    vmaUnmapMemory(m_allocator->GetAllocator(), stagingAllocation);

    VkTextureData tex{};
    tex.width  = width;
    tex.height = height;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width  = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    tex.allocation = VK_NULL_HANDLE;
    if (!m_allocator->CreateImage(imageInfo,
                                  VMA_MEMORY_USAGE_AUTO,
                                  tex.image,
                                  tex.allocation,
                                  VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT)) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed to create VkImage with VMA");
        m_allocator->DestroyBuffer(stagingBuffer, stagingAllocation);
        return 0u;
    }

    VmaAllocationInfo imageAllocInfo{};
    vmaGetAllocationInfo(m_allocator->GetAllocator(), tex.allocation, &imageAllocInfo);
    tex.memory = imageAllocInfo.deviceMemory;

    if (!TransitionImageLayout(tex.image, imageInfo.format,
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) ||
        !CopyBufferToImage(stagingBuffer, tex.image, width, height) ||
        !TransitionImageLayout(tex.image, imageInfo.format,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed image transitions or buffer copy for texture");
        m_allocator->DestroyImage(tex.image, tex.allocation);
        tex.image = VK_NULL_HANDLE;
        tex.allocation = VK_NULL_HANDLE;
        tex.memory = VK_NULL_HANDLE;
        m_allocator->DestroyBuffer(stagingBuffer, stagingAllocation);
        return 0u;
    }

    m_allocator->DestroyBuffer(stagingBuffer, stagingAllocation);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = tex.image;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &tex.view) != VK_SUCCESS) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed to create VkImageView");
        m_allocator->DestroyImage(tex.image, tex.allocation);
        tex.image = VK_NULL_HANDLE;
        tex.allocation = VK_NULL_HANDLE;
        tex.memory = VK_NULL_HANDLE;
        return 0u;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter    = (gTextureFilterMode == TextureFilterMode::Nearest) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    samplerInfo.minFilter    = (gTextureFilterMode == TextureFilterMode::Nearest) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    samplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod       = 1.0f;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &tex.sampler) != VK_SUCCESS) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed to create VkSampler");
        vkDestroyImageView(m_device, tex.view, nullptr);
        m_allocator->DestroyImage(tex.image, tex.allocation);
        tex.image = VK_NULL_HANDLE;
        tex.allocation = VK_NULL_HANDLE;
        tex.memory = VK_NULL_HANDLE;
        return 0u;
    }

    if (!AllocateDescriptorSet(tex.descriptorSet)) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed to allocate texture descriptor set");
        vkDestroySampler(m_device, tex.sampler, nullptr);
        vkDestroyImageView(m_device, tex.view, nullptr);
        m_allocator->DestroyImage(tex.image, tex.allocation);
        tex.image = VK_NULL_HANDLE;
        tex.allocation = VK_NULL_HANDLE;
        tex.memory = VK_NULL_HANDLE;
        return 0u;
    }
    WriteTextureDescriptorSet(tex);

    const uint32_t outId = m_nextTextureId++;
    m_textures[outId] = tex;

    TraceLog(LogLevel::Trace, "TEXTURE", TextFormat("[Vulkan] Texture uploaded to GPU (ID: %u, %ux%u, Mem: %llu bytes, DS: %p)",
        outId, width, height, static_cast<unsigned long long>(imageSize), (void*)tex.descriptorSet));
    return outId;
}

void QuarkVkResources::DestroyTexture(uint32_t textureId) {
    const auto it = m_textures.find(textureId);
    if (it == m_textures.end()) return;

    VkTextureData& tex = it->second;
    TraceLog(LogLevel::Info, "TEXTURE", TextFormat("[Vulkan] Texture destroyed (ID: %u, %ux%u)", textureId, tex.width, tex.height));

    if (tex.sampler  != VK_NULL_HANDLE) vkDestroySampler   (m_device, tex.sampler,  nullptr);
    if (tex.view     != VK_NULL_HANDLE) vkDestroyImageView (m_device, tex.view,     nullptr);
    if (tex.image    != VK_NULL_HANDLE) {
        if (tex.allocation != VK_NULL_HANDLE && m_allocator != nullptr) {
            m_allocator->DestroyImage(tex.image, tex.allocation);
        } else {
            vkDestroyImage(m_device, tex.image, nullptr);
        }
        tex.image = VK_NULL_HANDLE;
    }
    tex.memory = VK_NULL_HANDLE;
    tex.allocation = VK_NULL_HANDLE;
    m_textures.erase(it);
}

VkCommandBuffer QuarkVkResources::BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &cmd) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

void QuarkVkResources::EndSingleTimeCommands(VkCommandBuffer cmd) {
    if (cmd == VK_NULL_HANDLE) return;
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
}

bool QuarkVkResources::TransitionImageLayout(VkImage image, VkFormat /*format*/,
                                             VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) return false;

    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout                       = oldLayout;
    barrier.newLayout                       = newLayout;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndSingleTimeCommands(cmd);
    return true;
}

bool QuarkVkResources::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) return false;

    VkBufferImageCopy region{};
    region.bufferOffset                    = 0;
    region.bufferRowLength                 = 0;
    region.bufferImageHeight               = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset                     = {0, 0, 0};
    region.imageExtent                     = {width, height, 1};

    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndSingleTimeCommands(cmd);
    return true;
}

bool QuarkVkResources::ReadImageToRGBA(VkImage image, VkFormat format, uint32_t width,
                                       uint32_t height, VkImageLayout sourceLayout, void* outPixels) {
    if (image == VK_NULL_HANDLE || !outPixels || width == 0 || height == 0 ||
        m_device == VK_NULL_HANDLE || m_allocator == nullptr || m_commandPool == VK_NULL_HANDLE) {
        return false;
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4u;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    if (!m_allocator->CreateBuffer(imageSize,
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VMA_MEMORY_USAGE_AUTO,
                                   stagingBuffer,
                                   stagingAllocation,
                                   VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                                   VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        TraceLog(LogLevel::Error, "IMAGE", "[Vulkan] Failed to allocate readback staging buffer");
        return false;
    }

    VkCommandBuffer cmd = BeginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) {
        m_allocator->DestroyBuffer(stagingBuffer, stagingAllocation);
        return false;
    }

    VkImageSubresourceRange subresource{};
    subresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    subresource.baseMipLevel   = 0;
    subresource.levelCount     = 1;
    subresource.baseArrayLayer = 0;
    subresource.layerCount     = 1;

    VkImageMemoryBarrier toTransfer{};
    toTransfer.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.oldLayout           = sourceLayout;
    toTransfer.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image               = image;
    toTransfer.subresourceRange    = subresource;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    toTransfer.srcAccessMask      = 0;
    if (sourceLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        toTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (sourceLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy region{};
    region.bufferOffset                    = 0;
    region.bufferRowLength                 = 0;
    region.bufferImageHeight               = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset                     = {0, 0, 0};
    region.imageExtent                     = {width, height, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);

    VkImageMemoryBarrier back{};
    back.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    back.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.newLayout           = sourceLayout;
    back.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    back.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    back.image               = image;
    back.subresourceRange    = subresource;
    back.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    back.dstAccessMask       = 0;
    VkPipelineStageFlags backDstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    if (sourceLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        back.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        backDstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (sourceLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        back.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        backDstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, backDstStage, 0, 0, nullptr, 0, nullptr, 1, &back);

    EndSingleTimeCommands(cmd);

    void* mapped = nullptr;
    if (vmaMapMemory(m_allocator->GetAllocator(), stagingAllocation, &mapped) != VK_SUCCESS) {
        TraceLog(LogLevel::Error, "IMAGE", "[Vulkan] Failed to map readback staging buffer");
        m_allocator->DestroyBuffer(stagingBuffer, stagingAllocation);
        return false;
    }

    const bool bgra = (format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB);
    const uint8_t* src = static_cast<const uint8_t*>(mapped);
    uint8_t* dst = static_cast<uint8_t*>(outPixels);
    const size_t pixelCount = static_cast<size_t>(width) * height;
    if (bgra) {
        for (size_t i = 0; i < pixelCount; ++i) {
            dst[i * 4 + 0] = src[i * 4 + 2];
            dst[i * 4 + 1] = src[i * 4 + 1];
            dst[i * 4 + 2] = src[i * 4 + 0];
            dst[i * 4 + 3] = src[i * 4 + 3];
        }
    } else {
        std::memcpy(dst, src, static_cast<size_t>(width) * height * 4u);
    }
    vmaUnmapMemory(m_allocator->GetAllocator(), stagingAllocation);
    m_allocator->DestroyBuffer(stagingBuffer, stagingAllocation);
    return true;
}

bool QuarkVkResources::UpdateTextureFromRGBA(uint32_t textureId, const unsigned char* rgba,
                                             uint32_t width, uint32_t height) {
    if (!rgba || width == 0 || height == 0 || m_device == VK_NULL_HANDLE || m_allocator == nullptr) {
        TraceLog(LogLevel::Warn, "TEXTURE", "[Vulkan] Cannot update texture: invalid parameters");
        return false;
    }

    const auto it = m_textures.find(textureId);
    if (it == m_textures.end()) {
        TraceLog(LogLevel::Warn, "TEXTURE", TextFormat("[Vulkan] Texture not found for update (ID: %u)", textureId));
        return false;
    }

    VkTextureData& tex = it->second;
    if (tex.width != static_cast<uint32_t>(width) || tex.height != static_cast<uint32_t>(height)) {
        TraceLog(LogLevel::Warn, "TEXTURE", TextFormat("[Vulkan] Texture size mismatch: expected %ux%u, got %ux%u",
            tex.width, tex.height, width, height));
        return false;
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4u;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    if (!m_allocator->CreateBuffer(imageSize,
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VMA_MEMORY_USAGE_AUTO,
                                   stagingBuffer,
                                   stagingAllocation,
                                   VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                   VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed to allocate staging buffer for texture update");
        return false;
    }

    void* mapped = nullptr;
    if (vmaMapMemory(m_allocator->GetAllocator(), stagingAllocation, &mapped) != VK_SUCCESS) {
        m_allocator->DestroyBuffer(stagingBuffer, stagingAllocation);
        return false;
    }
    std::memcpy(mapped, rgba, static_cast<size_t>(imageSize));
    vmaUnmapMemory(m_allocator->GetAllocator(), stagingAllocation);

    if (!TransitionImageLayout(tex.image, VK_FORMAT_R8G8B8A8_UNORM,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) ||
        !CopyBufferToImage(stagingBuffer, tex.image, width, height) ||
        !TransitionImageLayout(tex.image, VK_FORMAT_R8G8B8A8_UNORM,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed to update texture");
        m_allocator->DestroyBuffer(stagingBuffer, stagingAllocation);
        return false;
    }

    m_allocator->DestroyBuffer(stagingBuffer, stagingAllocation);
    TraceLog(LogLevel::Trace, "TEXTURE", TextFormat("[Vulkan] Texture updated (ID: %u, %ux%u)", textureId, width, height));
    return true;
}

bool QuarkVkResources::UpdateTextureRegionRGBA(uint32_t textureId, const unsigned char* rgba,
                                               uint32_t offsetX, uint32_t offsetY,
                                               uint32_t width, uint32_t height) {
    if (!rgba || width == 0 || height == 0 || m_device == VK_NULL_HANDLE || m_allocator == nullptr) {
        TraceLog(LogLevel::Warn, "TEXTURE", "[Vulkan] Cannot update texture region: invalid parameters");
        return false;
    }

    const auto it = m_textures.find(textureId);
    if (it == m_textures.end()) {
        TraceLog(LogLevel::Warn, "TEXTURE", TextFormat("[Vulkan] Texture not found for region update (ID: %u)", textureId));
        return false;
    }

    VkTextureData& tex = it->second;
    if (offsetX + width > tex.width || offsetY + height > tex.height) {
        TraceLog(LogLevel::Warn, "TEXTURE", TextFormat("[Vulkan] Region out of bounds: texture %ux%u, region (%u,%u) %ux%u",
            tex.width, tex.height, offsetX, offsetY, width, height));
        return false;
    }

    const VkDeviceSize regionSize = static_cast<VkDeviceSize>(width) * height * 4u;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    if (!m_allocator->CreateBuffer(regionSize,
                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VMA_MEMORY_USAGE_AUTO,
                                   stagingBuffer,
                                   stagingAllocation,
                                   VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                   VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed to allocate staging buffer for texture region update");
        return false;
    }

    void* mapped = nullptr;
    if (vmaMapMemory(m_allocator->GetAllocator(), stagingAllocation, &mapped) != VK_SUCCESS) {
        m_allocator->DestroyBuffer(stagingBuffer, stagingAllocation);
        return false;
    }
    std::memcpy(mapped, rgba, static_cast<size_t>(regionSize));
    vmaUnmapMemory(m_allocator->GetAllocator(), stagingAllocation);

    VkCommandBuffer cmd = BeginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) {
        m_allocator->DestroyBuffer(stagingBuffer, stagingAllocation);
        return false;
    }

    if (!TransitionImageLayout(tex.image, VK_FORMAT_R8G8B8A8_UNORM,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        EndSingleTimeCommands(cmd);
        m_allocator->DestroyBuffer(stagingBuffer, stagingAllocation);
        return false;
    }

    VkBufferImageCopy region{};
    region.bufferOffset                    = 0;
    region.bufferRowLength                 = 0;
    region.bufferImageHeight               = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset                     = { static_cast<int32_t>(offsetX), static_cast<int32_t>(offsetY), 0 };
    region.imageExtent                     = { width, height, 1 };

    vkCmdCopyBufferToImage(cmd, stagingBuffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndSingleTimeCommands(cmd);

    if (!TransitionImageLayout(tex.image, VK_FORMAT_R8G8B8A8_UNORM,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        m_allocator->DestroyBuffer(stagingBuffer, stagingAllocation);
        return false;
    }

    m_allocator->DestroyBuffer(stagingBuffer, stagingAllocation);
    TraceLog(LogLevel::Trace, "TEXTURE", TextFormat("[Vulkan] Texture region updated (ID: %u, offset (%u,%u) size %ux%u)",
        textureId, offsetX, offsetY, width, height));
    return true;
}

} // namespace qc