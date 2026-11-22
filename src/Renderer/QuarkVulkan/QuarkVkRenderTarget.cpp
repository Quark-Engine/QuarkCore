#include "QuarkVkRenderTarget.hpp"

#include <array>

namespace qci
{
    void QuarkVkRenderTarget::Initialize(VkDevice device, QuarkVkGpuAllocator& allocator,
                                         VkFormat colorFormat, VkFormat depthFormat)
    {
        m_Device = device;
        m_pAllocator = &allocator;
        m_ColorFormat = colorFormat;
        m_DepthFormat = depthFormat;
    }

    void QuarkVkRenderTarget::Shutdown()
    {
        for (auto& [id, target] : m_Targets)
        {
            (void)id;
            DestroyDepthResources(target);
        }
        m_Targets.clear();
        DestroyRenderPass();
    }

    bool QuarkVkRenderTarget::CreateRenderPass()
    {
        if (m_Device == VK_NULL_HANDLE || m_ColorFormat == VK_FORMAT_UNDEFINED || m_DepthFormat == VK_FORMAT_UNDEFINED)
        {
            return false;
        }

        DestroyRenderPass();

        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = m_ColorFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = m_DepthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        std::array<VkAttachmentDescription, 2> aAttachments =
            {
                colorAttachment, depthAttachment};

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(aAttachments.size());
        renderPassInfo.pAttachments = aAttachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan offscreen render pass.");
        }
        TraceLog(LogLevel::Trace, "VULKAN", "Offscreen render pass created.");
        return true;
    }

    void QuarkVkRenderTarget::DestroyRenderPass()
    {
        if (m_RenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
        }
    }

    bool QuarkVkRenderTarget::CreateDepthResources(uint32_t width, uint32_t height, TargetData& out)
    {
        if (m_Device == VK_NULL_HANDLE || m_pAllocator == nullptr)
        {
            return false;
        }

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_DepthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (!m_pAllocator->CreateImage(imageInfo,
                                       VMA_MEMORY_USAGE_AUTO,
                                       out.m_DepthImage,
                                       out.m_DepthAllocation,
                                       VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT))
        {
            return false;
        }

        VmaAllocationInfo allocInfo{};
        vmaGetAllocationInfo(m_pAllocator->GetAllocator(), out.m_DepthAllocation, &allocInfo);
        out.m_DepthMemory = allocInfo.deviceMemory;

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = out.m_DepthImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_DepthFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (HasStencilComponent(m_DepthFormat))
        {
            viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Device, &viewInfo, nullptr, &out.m_DepthView) != VK_SUCCESS)
        {
            m_pAllocator->DestroyImage(out.m_DepthImage, out.m_DepthAllocation);
            out.m_DepthImage = VK_NULL_HANDLE;
            out.m_DepthAllocation = VK_NULL_HANDLE;
            out.m_DepthMemory = VK_NULL_HANDLE;
            return false;
        }

        return true;
    }

    void QuarkVkRenderTarget::DestroyDepthResources(TargetData& data)
    {
        if (data.m_DepthView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(m_Device, data.m_DepthView, nullptr);
            data.m_DepthView = VK_NULL_HANDLE;
        }
        if (data.m_DepthImage != VK_NULL_HANDLE)
        {
            if (data.m_DepthAllocation != VK_NULL_HANDLE && m_pAllocator != nullptr)
            {
                m_pAllocator->DestroyImage(data.m_DepthImage, data.m_DepthAllocation);
            }
            else
            {
                vkDestroyImage(m_Device, data.m_DepthImage, nullptr);
            }
            data.m_DepthImage = VK_NULL_HANDLE;
        }
        if (data.m_DepthMemory != VK_NULL_HANDLE)
        {
            data.m_DepthMemory = VK_NULL_HANDLE;
        }
        data.m_DepthAllocation = VK_NULL_HANDLE;
    }

    bool QuarkVkRenderTarget::CreateTarget(uint32_t renderTargetId, VkImageView colorView,
                                           uint32_t width, uint32_t height)
    {
        if (m_Device == VK_NULL_HANDLE || m_RenderPass == VK_NULL_HANDLE || colorView == VK_NULL_HANDLE)
        {
            return false;
        }

        const bool isNew = m_Targets.find(renderTargetId) == m_Targets.end();
        auto& target = m_Targets[renderTargetId];
        target.m_ColorView = colorView;
        target.m_Width = width;
        target.m_Height = height;

        if (target.m_DepthView == VK_NULL_HANDLE && !CreateDepthResources(width, height, target))
        {
            if (isNew)
            {
                m_Targets.erase(renderTargetId);
            }
            return false;
        }

        if (target.m_Framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(m_Device, target.m_Framebuffer, nullptr);
            target.m_Framebuffer = VK_NULL_HANDLE;
        }

        std::array<VkImageView, 2> aAttachments =
            {
                colorView, target.m_DepthView};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_RenderPass;
        fbInfo.attachmentCount = static_cast<uint32_t>(aAttachments.size());
        fbInfo.pAttachments = aAttachments.data();
        fbInfo.width = width;
        fbInfo.height = height;
        fbInfo.layers = 1;
        if (vkCreateFramebuffer(m_Device, &fbInfo, nullptr, &target.m_Framebuffer) != VK_SUCCESS)
        {
            target.m_ColorView = VK_NULL_HANDLE;
            target.m_Width = 0;
            target.m_Height = 0;
            if (isNew)
            {
                DestroyDepthResources(target);
                m_Targets.erase(renderTargetId);
            }
            return false;
        }
        return true;
    }

    void QuarkVkRenderTarget::DestroyTarget(uint32_t renderTargetId)
    {
        auto it = m_Targets.find(renderTargetId);
        if (it == m_Targets.end())
        {
            return;
        }

        if (it->second.m_Framebuffer != VK_NULL_HANDLE && m_Device != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(m_Device, it->second.m_Framebuffer, nullptr);
        }
        if (m_Device != VK_NULL_HANDLE)
        {
            DestroyDepthResources(it->second);
        }
        m_Targets.erase(it);
    }

    void QuarkVkRenderTarget::DestroyFramebuffers()
    {
        for (auto& [id, target] : m_Targets)
        {
            (void)id;
            if (target.m_Framebuffer != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(m_Device, target.m_Framebuffer, nullptr);
                target.m_Framebuffer = VK_NULL_HANDLE;
            }
        }
    }

    VkFramebuffer QuarkVkRenderTarget::Framebuffer(uint32_t renderTargetId) const
    {
        auto it = m_Targets.find(renderTargetId);
        return it == m_Targets.end() ? VK_NULL_HANDLE : it->second.m_Framebuffer;
    }
} // qci
