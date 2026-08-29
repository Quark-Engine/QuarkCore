#include "QuarkVkRenderTarget.hpp"

#include <array>

namespace qc {

void QuarkVkRenderTarget::Initialize(VkDevice device, QuarkVkGpuAllocator& allocator,
                                     VkFormat colorFormat, VkFormat depthFormat) {
    m_device = device;
    m_allocator = &allocator;
    m_colorFormat = colorFormat;
    m_depthFormat = depthFormat;
}

void QuarkVkRenderTarget::Shutdown() {
    for (auto& [id, target] : m_targets) {
        (void)id;
        DestroyDepthResources(target);
    }
    m_targets.clear();
    DestroyRenderPass();
}

bool QuarkVkRenderTarget::CreateRenderPass() {
    if (m_device == VK_NULL_HANDLE || m_colorFormat == VK_FORMAT_UNDEFINED || m_depthFormat == VK_FORMAT_UNDEFINED) {
        return false;
    }

    DestroyRenderPass();

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = m_colorFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format         = m_depthFormat;
    depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments    = attachments.data();
    renderPassInfo.subpassCount    = 1;
    renderPassInfo.pSubpasses      = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies   = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan offscreen render pass.");
    }
    TraceLog(LogLevel::Trace, "VULKAN", "Offscreen render pass created.");
    return true;
}

void QuarkVkRenderTarget::DestroyRenderPass() {
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

bool QuarkVkRenderTarget::CreateDepthResources(uint32_t width, uint32_t height, TargetData& out) {
    if (m_device == VK_NULL_HANDLE || m_allocator == nullptr) {
        return false;
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width  = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.format        = m_depthFormat;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (!m_allocator->CreateImage(imageInfo,
                                  VMA_MEMORY_USAGE_AUTO,
                                  out.depthImage,
                                  out.depthAllocation,
                                  VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT)) {
        return false;
    }

    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(m_allocator->GetAllocator(), out.depthAllocation, &allocInfo);
    out.depthMemory = allocInfo.deviceMemory;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = out.depthImage;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = m_depthFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (HasStencilComponent(m_depthFormat)) {
        viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &out.depthView) != VK_SUCCESS) {
        m_allocator->DestroyImage(out.depthImage, out.depthAllocation);
        out.depthImage = VK_NULL_HANDLE;
        out.depthAllocation = VK_NULL_HANDLE;
        out.depthMemory = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

void QuarkVkRenderTarget::DestroyDepthResources(TargetData& data) {
    if (data.depthView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, data.depthView, nullptr);
        data.depthView = VK_NULL_HANDLE;
    }
    if (data.depthImage != VK_NULL_HANDLE) {
        if (data.depthAllocation != VK_NULL_HANDLE && m_allocator != nullptr) {
            m_allocator->DestroyImage(data.depthImage, data.depthAllocation);
        } else {
            vkDestroyImage(m_device, data.depthImage, nullptr);
        }
        data.depthImage = VK_NULL_HANDLE;
    }
    if (data.depthMemory != VK_NULL_HANDLE) {
        data.depthMemory = VK_NULL_HANDLE;
    }
    data.depthAllocation = VK_NULL_HANDLE;
}

bool QuarkVkRenderTarget::CreateTarget(uint32_t renderTargetId, VkImageView colorView,
                                       uint32_t width, uint32_t height) {
    if (m_device == VK_NULL_HANDLE || m_renderPass == VK_NULL_HANDLE || colorView == VK_NULL_HANDLE) {
        return false;
    }

    const bool isNew = m_targets.find(renderTargetId) == m_targets.end();
    auto& target = m_targets[renderTargetId];
    target.colorView = colorView;
    target.width  = width;
    target.height = height;

    if (target.depthView == VK_NULL_HANDLE && !CreateDepthResources(width, height, target)) {
        if (isNew) {
            m_targets.erase(renderTargetId);
        }
        return false;
    }

    if (target.framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, target.framebuffer, nullptr);
        target.framebuffer = VK_NULL_HANDLE;
    }

    std::array<VkImageView, 2> attachments = { colorView, target.depthView };
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass      = m_renderPass;
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments    = attachments.data();
    fbInfo.width           = width;
    fbInfo.height          = height;
    fbInfo.layers          = 1;
    if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &target.framebuffer) != VK_SUCCESS) {
        target.colorView = VK_NULL_HANDLE;
        target.width  = 0;
        target.height = 0;
        if (isNew) {
            DestroyDepthResources(target);
            m_targets.erase(renderTargetId);
        }
        return false;
    }
    return true;
}

void QuarkVkRenderTarget::DestroyTarget(uint32_t renderTargetId) {
    auto it = m_targets.find(renderTargetId);
    if (it == m_targets.end()) return;

    if (it->second.framebuffer != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, it->second.framebuffer, nullptr);
    }
    if (m_device != VK_NULL_HANDLE) {
        DestroyDepthResources(it->second);
    }
    m_targets.erase(it);
}

void QuarkVkRenderTarget::DestroyFramebuffers() {
    for (auto& [id, target] : m_targets) {
        (void)id;
        if (target.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, target.framebuffer, nullptr);
            target.framebuffer = VK_NULL_HANDLE;
        }
    }
}

VkFramebuffer QuarkVkRenderTarget::Framebuffer(uint32_t renderTargetId) const {
    auto it = m_targets.find(renderTargetId);
    return it == m_targets.end() ? VK_NULL_HANDLE : it->second.framebuffer;
}

} // namespace qc