#include "Renderer/Vulkan/VulkanRenderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <vector>

#include <SDL.h>
#include <SDL_vulkan.h>
#include <png.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include "Core/Logger.h"
#include "Platform/Window.h"

namespace vulkor {

VulkanRenderer::~VulkanRenderer() {
    Shutdown();
}

bool VulkanRenderer::Initialize(Window& window) {
    Logger::Info("VulkanRenderer initialization started", "vulkan");
    m_window = &window;
    
    m_nextTextureId = 1;
    m_nextFontId = 1;
    m_nextRenderTargetId = 1;
    m_whiteTextureId = 0;

    if (!CreateInstance()) return false;
    if (!CreateSurface()) return false;
    if (!PickPhysicalDevice()) return false;
    if (!CreateDeviceAndQueues()) return false;
    if (!CreateAllocator()) return false;
    if (!CreateSwapchainAndViews()) return false;
    if (!CreateRenderPass()) return false;
    if (!CreateOffscreenRenderPass()) return false;
    if (!CreateDescriptorObjects()) return false;
    if (!CreateGraphicsPipeline()) return false;
    if (!CreateFramebuffers()) return false;
    if (!CreateCommandPoolAndBuffers()) return false;
    if (!CreateFrameResources()) return false;
    if (!CreateSyncObjects()) return false;
    if (!CreateWhiteTexture()) return false;

    Logger::Info("VulkanRenderer initialization completed", "vulkan");
    return true;
}

void VulkanRenderer::Shutdown() {
    if (m_device == VK_NULL_HANDLE && m_instance == VK_NULL_HANDLE && m_surface == VK_NULL_HANDLE && m_window == nullptr) {
        return;
    }
    Logger::Info("VulkanRenderer shutdown", "vulkan");
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }

    std::vector<uint32_t> fontIds;
    fontIds.reserve(m_fonts.size());
    for (const auto& [id, _] : m_fonts) {
        (void)_;
        fontIds.push_back(id);
    }
    for (uint32_t id : fontIds) {
        UnloadFont(Font{id});
    }

    std::vector<uint32_t> renderTargetIds;
    renderTargetIds.reserve(m_renderTargets.size());
    for (const auto& [id, _] : m_renderTargets) {
        (void)_;
        renderTargetIds.push_back(id);
    }
    for (uint32_t id : renderTargetIds) {
        DestroyRenderTarget(RenderTarget{id});
    }

    std::vector<uint32_t> textureIds;
    textureIds.reserve(m_textures.size());
    for (const auto& [id, _] : m_textures) {
        (void)_;
        textureIds.push_back(id);
    }
    for (uint32_t id : textureIds) {
        DestroyTexture(id);
    }

    DestroySwapchainObjects();
    DestroyCoreObjects();

    m_vertices.clear();
    m_indices.clear();
    m_drawItems.clear();
    m_frameActive = false;
    if (m_ftLibrary) {
        FT_Done_FreeType(reinterpret_cast<FT_Library>(m_ftLibrary));
        m_ftLibrary = nullptr;
    }
    m_window = nullptr;
    
    m_nextTextureId = 1;
    m_nextFontId = 1;
    m_nextRenderTargetId = 1;
    m_whiteTextureId = 0;
}

void VulkanRenderer::Resize(uint32_t, uint32_t) {
    m_framebufferResized = true;
}

void VulkanRenderer::BeginFrame(float) {
    if (m_device == VK_NULL_HANDLE || m_swapchain == VK_NULL_HANDLE || m_frameActive) {
        return;
    }

    m_vertices.clear();
    m_indices.clear();
    m_drawItems.clear();
    m_framePasses.clear();
    m_activeRenderTargetId = 0;
    for (auto& [id, rt] : m_renderTargets) {
        (void)id;
        rt.vertices.clear();
        rt.indices.clear();
        rt.drawItems.clear();
    }

    vkWaitForFences(m_device, 1, &m_inFlightFences[m_frameIndex], VK_TRUE, UINT64_MAX);

    VkResult acquireResult = vkAcquireNextImageKHR(
        m_device,
        m_swapchain,
        UINT64_MAX,
        m_imageAvailableSemaphores[m_frameIndex],
        VK_NULL_HANDLE,
        &m_acquiredImageIndex
    );

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        std::cerr << "vkAcquireNextImageKHR failed\n";
        Logger::Error("vkAcquireNextImageKHR failed", "vulkan");
        return;
    }

    m_frameActive = true;
}

void VulkanRenderer::EndFrame() {
    if (!m_frameActive || m_device == VK_NULL_HANDLE) {
        return;
    }

    vkResetFences(m_device, 1, &m_inFlightFences[m_frameIndex]);
    vkResetCommandBuffer(m_commandBuffers[m_frameIndex], 0);

    BuildCombinedFrameGeometry();
    if (!UploadFrameGeometry(m_frameIndex)) {
        m_frameActive = false;
        return;
    }

    if (!RecordCommandBuffer(m_commandBuffers[m_frameIndex], m_acquiredImageIndex)) {
        m_frameActive = false;
        return;
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_imageAvailableSemaphores[m_frameIndex];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_frameIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_renderFinishedSemaphores[m_frameIndex];

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_frameIndex]) != VK_SUCCESS) {
        std::cerr << "vkQueueSubmit failed\n";
        Logger::Error("vkQueueSubmit failed", "vulkan");
        m_frameActive = false;
        return;
    }

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[m_frameIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &m_acquiredImageIndex;

    VkResult presentResult = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        Logger::Warn("Vulkan present requested swapchain recreation", "vulkan");
        m_framebufferResized = false;
        RecreateSwapchain();
    } else if (presentResult != VK_SUCCESS) {
        std::cerr << "vkQueuePresentKHR failed\n";
        Logger::Error("vkQueuePresentKHR failed", "vulkan");
    }

    m_frameIndex = (m_frameIndex + 1) % kMaxFramesInFlight;
    m_frameActive = false;
}

void VulkanRenderer::ClearBackground(const math::Color& color) {
    m_clearColor = static_cast<math::Vec4>(color);
}

void VulkanRenderer::SetCamera2D(const math::Camera2D& camera) {
    m_camera = camera;
    if (m_camera.zoom < 0.01f) {
        m_camera.zoom = 0.01f;
    }
}

Texture VulkanRenderer::LoadTexturePNG(const std::string& path) {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    if (!DecodePNG(path, pixels, width, height)) {
        Logger::Error("Vulkan texture decode failed: " + path, "asset");
        return {};
    }

    uint32_t id = 0;
    if (!CreateTextureFromRGBA(pixels.data(), static_cast<uint32_t>(width), static_cast<uint32_t>(height), id)) {
        Logger::Error("Vulkan texture upload failed: " + path, "asset");
        return {};
    }
    Logger::Info("Vulkan texture loaded: " + path, "asset");
    return Texture{id};
}

void VulkanRenderer::UnloadTexture(Texture texture) {
    if (texture.id == m_whiteTextureId) {
        return;
    }
    for (auto it = m_renderTargets.begin(); it != m_renderTargets.end(); ++it) {
        if (it->second.textureId == texture.id) {
            DestroyRenderTarget(RenderTarget{it->first});
            return;
        }
    }
    DestroyTexture(texture.id);
}

Font VulkanRenderer::LoadFont(const std::string& path, uint32_t pixelSize) {
    if (pixelSize == 0) {
        pixelSize = 32;
    }

    if (!m_ftLibrary) {
        FT_Library library = nullptr;
        if (FT_Init_FreeType(&library) != 0) {
            Logger::Error("FT_Init_FreeType failed", "font");
            return {};
        }
        m_ftLibrary = library;
    }

    FT_Face face = nullptr;
    if (FT_New_Face(reinterpret_cast<FT_Library>(m_ftLibrary), path.c_str(), 0, &face) != 0) {
        Logger::Error("FT_New_Face failed for font: " + path, "font");
        return {};
    }

    FT_Set_Pixel_Sizes(face, 0, pixelSize);

    FontData fontData = {};
    fontData.pixelSize = pixelSize;
    fontData.ascent = static_cast<float>(face->ascender) / 64.0f;
    fontData.descent = static_cast<float>(face->descender) / 64.0f;
    fontData.lineHeight = static_cast<float>(face->height) / 64.0f;

    for (uint32_t c = 32; c < 127; ++c) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) != 0) {
            continue;
        }

        const FT_Bitmap& bitmap = face->glyph->bitmap;
        if (bitmap.width == 0 || bitmap.rows == 0) {
            continue;
        }

        std::vector<unsigned char> rgba(static_cast<size_t>(bitmap.width) * static_cast<size_t>(bitmap.rows) * 4u, 0);
        const bool mono = bitmap.pixel_mode == FT_PIXEL_MODE_MONO;
        for (int y = 0; y < bitmap.rows; ++y) {
            for (int x = 0; x < bitmap.width; ++x) {
                unsigned char alpha = 0;
                if (mono) {
                    const size_t byteIndex = static_cast<size_t>(y) * static_cast<size_t>(bitmap.pitch) + static_cast<size_t>(x / 8);
                    const unsigned char bitMask = static_cast<unsigned char>(0x80u >> (x % 8));
                    alpha = (bitmap.buffer[byteIndex] & bitMask) ? 255u : 0u;
                } else {
                    alpha = bitmap.buffer[static_cast<size_t>(y) * static_cast<size_t>(bitmap.pitch) + static_cast<size_t>(x)];
                }
                const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(bitmap.width) + static_cast<size_t>(x)) * 4u;
                rgba[idx + 0] = 255;
                rgba[idx + 1] = 255;
                rgba[idx + 2] = 255;
                rgba[idx + 3] = alpha;
            }
        }

        uint32_t textureId = 0;
        if (!CreateTextureFromRGBA(rgba.data(), static_cast<uint32_t>(bitmap.width), static_cast<uint32_t>(bitmap.rows), textureId)) {
            continue;
        }

        FontGlyph glyph = {};
        glyph.textureId = textureId;
        glyph.width = bitmap.width;
        glyph.height = bitmap.rows;
        glyph.bearingX = face->glyph->bitmap_left;
        glyph.bearingY = face->glyph->bitmap_top;
        glyph.advance = static_cast<int>(face->glyph->advance.x >> 6);
        fontData.glyphs[c] = glyph;
    }

    FT_Done_Face(face);

    if (fontData.glyphs.empty()) {
        Logger::Error("No glyphs were generated for font: " + path, "font");
        return {};
    }

    const uint32_t fontId = m_nextFontId++;
    m_fonts[fontId] = std::move(fontData);
    Logger::Info("Vulkan font loaded: " + path, "font");
    return Font{fontId};
}

void VulkanRenderer::UnloadFont(Font font) {
    auto it = m_fonts.find(font.id);
    if (it == m_fonts.end()) {
        return;
    }
    for (const auto& [codepoint, glyph] : it->second.glyphs) {
        (void)codepoint;
        DestroyTexture(glyph.textureId);
    }
    m_fonts.erase(it);
}

RenderTarget VulkanRenderer::CreateRenderTarget(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || m_device == VK_NULL_HANDLE || m_allocator == VK_NULL_HANDLE || m_offscreenRenderPass == VK_NULL_HANDLE) {
        return {};
    }

    TextureData tex = {};
    tex.width = width;
    tex.height = height;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_swapchainFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo imageAllocInfo = {};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(m_allocator, &imageInfo, &imageAllocInfo, &tex.image, &tex.allocation, nullptr) != VK_SUCCESS) {
        return {};
    }

    if (!TransitionImageLayout(tex.image, imageInfo.format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        vmaDestroyImage(m_allocator, tex.image, tex.allocation);
        return {};
    }

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tex.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &tex.imageView) != VK_SUCCESS) {
        vmaDestroyImage(m_allocator, tex.image, tex.allocation);
        return {};
    }

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 1.0f;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &tex.sampler) != VK_SUCCESS) {
        vkDestroyImageView(m_device, tex.imageView, nullptr);
        vmaDestroyImage(m_allocator, tex.image, tex.allocation);
        return {};
    }

    if (!AllocateTextureDescriptorSet(tex.descriptorSet)) {
        vkDestroySampler(m_device, tex.sampler, nullptr);
        vkDestroyImageView(m_device, tex.imageView, nullptr);
        vmaDestroyImage(m_allocator, tex.image, tex.allocation);
        return {};
    }

    VkDescriptorImageInfo imageDescriptor = {};
    imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageDescriptor.imageView = tex.imageView;
    imageDescriptor.sampler = tex.sampler;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = tex.descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageDescriptor;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkImageView attachments[] = {tex.imageView};
    VkFramebufferCreateInfo fbInfo = {};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = m_offscreenRenderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = attachments;
    fbInfo.width = width;
    fbInfo.height = height;
    fbInfo.layers = 1;
    if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        vkDestroySampler(m_device, tex.sampler, nullptr);
        vkDestroyImageView(m_device, tex.imageView, nullptr);
        vmaDestroyImage(m_allocator, tex.image, tex.allocation);
        return {};
    }

    const uint32_t textureId = m_nextTextureId++;
    m_textures[textureId] = tex;

    const uint32_t renderTargetId = m_nextRenderTargetId++;
    RenderTargetData rt = {};
    rt.textureId = textureId;
    rt.width = width;
    rt.height = height;
    rt.framebuffer = framebuffer;
    rt.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    m_renderTargets[renderTargetId] = rt;

    Logger::Info("Vulkan render target created", "vulkan");
    return RenderTarget{renderTargetId, Texture{textureId}, width, height};
}

void VulkanRenderer::DestroyRenderTarget(RenderTarget renderTarget) {
    auto it = m_renderTargets.find(renderTarget.id);
    if (it == m_renderTargets.end()) {
        return;
    }

    const uint32_t textureId = it->second.textureId;
    if (it->second.framebuffer != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, it->second.framebuffer, nullptr);
    }
    m_renderTargets.erase(it);
    if (m_activeRenderTargetId == renderTarget.id) {
        m_activeRenderTargetId = 0;
    }
    DestroyTexture(textureId);
}

bool VulkanRenderer::BeginRenderTarget(RenderTarget renderTarget) {
    if (!m_frameActive) {
        return false;
    }
    auto it = m_renderTargets.find(renderTarget.id);
    if (it == m_renderTargets.end()) {
        return false;
    }
    m_activeRenderTargetId = renderTarget.id;
    return true;
}

void VulkanRenderer::EndRenderTarget() {
    m_activeRenderTargetId = 0;
}

void VulkanRenderer::DrawLine(const math::Vec2& a, const math::Vec2& b, const math::Color& color, float thickness) {
    float drawWidth = static_cast<float>(m_swapchainExtent.width > 0 ? m_swapchainExtent.width : 1);
    float drawHeight = static_cast<float>(m_swapchainExtent.height > 0 ? m_swapchainExtent.height : 1);
    if (m_activeRenderTargetId != 0) {
        auto itRt = m_renderTargets.find(m_activeRenderTargetId);
        if (itRt != m_renderTargets.end()) {
            drawWidth = static_cast<float>(itRt->second.width);
            drawHeight = static_cast<float>(itRt->second.height);
        }
    }

    const math::Vec2 sa = WorldToScreen(a);
    const math::Vec2 sb = WorldToScreen(b);
    const math::Vec2 d = {sb.x - sa.x, sb.y - sa.y};
    const float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len < 0.0001f) {
        DrawRect({sa.x, sa.y, thickness, thickness}, color);
        return;
    }

    const float half = std::max(thickness, 1.0f) * 0.5f;
    const math::Vec2 n = {-d.y / len, d.x / len};

    const math::Vec2 p0 = {sa.x + n.x * half, sa.y + n.y * half};
    const math::Vec2 p1 = {sb.x + n.x * half, sb.y + n.y * half};
    const math::Vec2 p2 = {sb.x - n.x * half, sb.y - n.y * half};
    const math::Vec2 p3 = {sa.x - n.x * half, sa.y - n.y * half};
    const math::Vec4 colorVec4 = static_cast<math::Vec4>(color);
    AppendQuad(m_whiteTextureId, ToNdc(p0, drawWidth, drawHeight), ToNdc(p1, drawWidth, drawHeight), ToNdc(p2, drawWidth, drawHeight), ToNdc(p3, drawWidth, drawHeight), colorVec4);
}

void VulkanRenderer::DrawRect(const math::Rect& rect, const math::Color& color) {
    float drawWidth = static_cast<float>(m_swapchainExtent.width > 0 ? m_swapchainExtent.width : 1);
    float drawHeight = static_cast<float>(m_swapchainExtent.height > 0 ? m_swapchainExtent.height : 1);
    if (m_activeRenderTargetId != 0) {
        auto itRt = m_renderTargets.find(m_activeRenderTargetId);
        if (itRt != m_renderTargets.end()) {
            drawWidth = static_cast<float>(itRt->second.width);
            drawHeight = static_cast<float>(itRt->second.height);
        }
    }

    const math::Vec2 position{rect.x, rect.y};
    const math::Vec2 size{rect.width, rect.height};
    const math::Vec4 colorVec4 = static_cast<math::Vec4>(color);
    
    const math::Vec2 p = WorldToScreen(position);
    const math::Vec2 s = {size.x * m_camera.zoom, size.y * m_camera.zoom};
    const math::Vec2 p0 = p;
    const math::Vec2 p1 = {p.x + s.x, p.y};
    const math::Vec2 p2 = {p.x + s.x, p.y + s.y};
    const math::Vec2 p3 = {p.x, p.y + s.y};
    AppendQuad(m_whiteTextureId, ToNdc(p0, drawWidth, drawHeight), ToNdc(p1, drawWidth, drawHeight), ToNdc(p2, drawWidth, drawHeight), ToNdc(p3, drawWidth, drawHeight), colorVec4);
}

void VulkanRenderer::DrawTexture(
    Texture texture,
    const math::Vec2& position,
    const math::Vec2& size,
    const math::Color& tint
) {
    if (m_textures.find(texture.id) == m_textures.end()) {
        return;
    }

    float drawWidth = static_cast<float>(m_swapchainExtent.width > 0 ? m_swapchainExtent.width : 1);
    float drawHeight = static_cast<float>(m_swapchainExtent.height > 0 ? m_swapchainExtent.height : 1);
    if (m_activeRenderTargetId != 0) {
        auto itRt = m_renderTargets.find(m_activeRenderTargetId);
        if (itRt != m_renderTargets.end()) {
            drawWidth = static_cast<float>(itRt->second.width);
            drawHeight = static_cast<float>(itRt->second.height);
        }
    }

    const math::Vec2 p = WorldToScreen(position);
    const math::Vec2 s = {size.x * m_camera.zoom, size.y * m_camera.zoom};
    const math::Vec2 p0 = p;
    const math::Vec2 p1 = {p.x + s.x, p.y};
    const math::Vec2 p2 = {p.x + s.x, p.y + s.y};
    const math::Vec2 p3 = {p.x, p.y + s.y};
    const math::Vec4 tintVec4 = static_cast<math::Vec4>(tint);
    AppendQuad(texture.id, ToNdc(p0, drawWidth, drawHeight), ToNdc(p1, drawWidth, drawHeight), ToNdc(p2, drawWidth, drawHeight), ToNdc(p3, drawWidth, drawHeight), tintVec4);
}

void VulkanRenderer::DrawText(
    Font font,
    const std::string& text,
    const math::Vec2& position,
    const math::Color& color
) {
    auto itFont = m_fonts.find(font.id);
    if (itFont == m_fonts.end()) {
        return;
    }

    float penX = position.x;
    float penY = position.y + static_cast<float>(itFont->second.pixelSize);
    for (char ch : text) {
        if (ch == '\n') {
            penX = position.x;
            penY += static_cast<float>(itFont->second.pixelSize);
            continue;
        }

        const uint32_t code = static_cast<uint32_t>(static_cast<unsigned char>(ch));
        auto itGlyph = itFont->second.glyphs.find(code);
        if (itGlyph == itFont->second.glyphs.end()) {
            penX += static_cast<float>(itFont->second.pixelSize) * 0.4f;
            continue;
        }

        const FontGlyph& glyph = itGlyph->second;
        const float x = penX + static_cast<float>(glyph.bearingX);
        const float y = penY - static_cast<float>(glyph.bearingY);
        DrawTexture(Texture{glyph.textureId}, {x, y}, {static_cast<float>(glyph.width), static_cast<float>(glyph.height)}, color);
        penX += static_cast<float>(glyph.advance);
    }
}

float VulkanRenderer::GetTextWidth(Font font, const std::string& text) {
    auto itFont = m_fonts.find(font.id);
    if (itFont == m_fonts.end()) {
        return 0.0f;
    }

    float width = 0.0f;
    for (char ch : text) {
        if (ch == '\n') {
            continue;
        }

        const uint32_t code = static_cast<uint32_t>(static_cast<unsigned char>(ch));
        auto itGlyph = itFont->second.glyphs.find(code);
        if (itGlyph == itFont->second.glyphs.end()) {
            width += static_cast<float>(itFont->second.pixelSize) * 0.4f;
            continue;
        }

        width += static_cast<float>(itGlyph->second.advance);
    }
    return width;
}

float VulkanRenderer::GetTextHeight(Font font) {
    auto itFont = m_fonts.find(font.id);
    if (itFont == m_fonts.end()) {
        return 0.0f;
    }
    return static_cast<float>(itFont->second.pixelSize);
}

FontMetrics VulkanRenderer::GetFontMetrics(Font font) {
    auto itFont = m_fonts.find(font.id);
    if (itFont == m_fonts.end()) {
        return {};
    }
    const FontData& data = itFont->second;
    return {data.ascent, data.descent, data.lineHeight, static_cast<float>(data.pixelSize)};
}

float VulkanRenderer::GetLineWidth(Font font, const std::string& text) {
    auto itFont = m_fonts.find(font.id);
    if (itFont == m_fonts.end()) {
        return 0.0f;
    }

    float maxWidth = 0.0f;
    float currentWidth = 0.0f;
    for (char ch : text) {
        if (ch == '\n') {
            maxWidth = std::max(maxWidth, currentWidth);
            currentWidth = 0.0f;
            continue;
        }

        const uint32_t code = static_cast<uint32_t>(static_cast<unsigned char>(ch));
        auto itGlyph = itFont->second.glyphs.find(code);
        if (itGlyph == itFont->second.glyphs.end()) {
            currentWidth += static_cast<float>(itFont->second.pixelSize) * 0.4f;
            continue;
        }

        currentWidth += static_cast<float>(itGlyph->second.advance);
    }
    maxWidth = std::max(maxWidth, currentWidth);
    return maxWidth;
}

float VulkanRenderer::GetLineHeight(Font font) {
    auto itFont = m_fonts.find(font.id);
    if (itFont == m_fonts.end()) {
        return 0.0f;
    }
    return itFont->second.lineHeight;
}

void VulkanRenderer::MeasureText(Font font, const std::string& text, float& width, float& height) {
    auto itFont = m_fonts.find(font.id);
    if (itFont == m_fonts.end()) {
        width = 0.0f;
        height = 0.0f;
        return;
    }

    width = GetLineWidth(font, text);
    size_t lineCount = 1;
    for (char ch : text) {
        if (ch == '\n') {
            ++lineCount;
        }
    }
    height = static_cast<float>(lineCount) * GetLineHeight(font);
}

bool VulkanRenderer::CreateInstance() {
    unsigned int extCount = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(m_window->GetNativeHandle(), &extCount, nullptr)) {
        std::cerr << "SDL_Vulkan_GetInstanceExtensions count failed: " << SDL_GetError() << '\n';
        return false;
    }

    std::vector<const char*> extensions(extCount);
    if (!SDL_Vulkan_GetInstanceExtensions(m_window->GetNativeHandle(), &extCount, extensions.data())) {
        std::cerr << "SDL_Vulkan_GetInstanceExtensions data failed: " << SDL_GetError() << '\n';
        return false;
    }

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkor";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "VulkorFramework";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
        std::cerr << "vkCreateInstance failed\n";
        Logger::Error("vkCreateInstance failed", "vulkan");
        return false;
    }
    return true;
}

bool VulkanRenderer::CreateSurface() {
    if (!SDL_Vulkan_CreateSurface(m_window->GetNativeHandle(), m_instance, &m_surface)) {
        std::cerr << "SDL_Vulkan_CreateSurface failed: " << SDL_GetError() << '\n';
        return false;
    }
    return true;
}

bool VulkanRenderer::PickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) {
        std::cerr << "No Vulkan physical devices\n";
        return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    for (VkPhysicalDevice device : devices) {
        QueueFamilyIndices indices = FindQueueFamilies(device);
        if (!indices.IsComplete()) {
            continue;
        }

        SwapchainSupport support = QuerySwapchainSupport(device);
        if (!support.formats.empty() && !support.presentModes.empty()) {
            m_physicalDevice = device;
            return true;
        }
    }

    std::cerr << "No suitable Vulkan physical device found\n";
    return false;
}

bool VulkanRenderer::CreateDeviceAndQueues() {
    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily, indices.presentFamily};

    const float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t family : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueInfo = {};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        queueInfos.push_back(queueInfo);
    }

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        std::cerr << "vkCreateDevice failed\n";
        Logger::Error("vkCreateDevice failed", "vulkan");
        return false;
    }

    vkGetDeviceQueue(m_device, indices.graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily, 0, &m_presentQueue);
    return true;
}

bool VulkanRenderer::CreateAllocator() {
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;

    if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
        std::cerr << "vmaCreateAllocator failed\n";
        Logger::Error("vmaCreateAllocator failed", "vulkan");
        return false;
    }
    return true;
}

bool VulkanRenderer::CreateSwapchainAndViews() {
    SwapchainSupport support = QuerySwapchainSupport(m_physicalDevice);
    VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = ChoosePresentMode(support.presentModes);
    VkExtent2D extent = ChooseExtent(support.capabilities);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    uint32_t queueFamilies[] = {indices.graphicsFamily, indices.presentFamily};

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilies;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        std::cerr << "vkCreateSwapchainKHR failed\n";
        return false;
    }

    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());

    m_swapchainFormat = surfaceFormat.format;
    m_swapchainExtent = extent;

    m_swapchainImageViews.resize(m_swapchainImages.size());
    for (size_t i = 0; i < m_swapchainImages.size(); ++i) {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_swapchainFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS) {
            std::cerr << "vkCreateImageView failed\n";
            return false;
        }
    }
    return true;
}

bool VulkanRenderer::CreateRenderPass() {
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = m_swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        std::cerr << "vkCreateRenderPass failed\n";
        return false;
    }
    return true;
}

bool VulkanRenderer::CreateOffscreenRenderPass() {
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = m_swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_offscreenRenderPass) != VK_SUCCESS) {
        std::cerr << "vkCreateRenderPass (offscreen) failed\n";
        return false;
    }
    return true;
}

bool VulkanRenderer::CreateDescriptorObjects() {
    VkDescriptorSetLayoutBinding samplerBinding = {};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "vkCreateDescriptorSetLayout failed\n";
        return false;
    }

    VkDescriptorPool firstPool = VK_NULL_HANDLE;
    if (!CreateDescriptorPool(m_descriptorPoolMaxSets, firstPool)) {
        std::cerr << "vkCreateDescriptorPool failed\n";
        return false;
    }
    m_descriptorPools.push_back(firstPool);
    return true;
}

bool VulkanRenderer::CreateDescriptorPool(uint32_t maxSets, VkDescriptorPool& outPool) {
    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = maxSets;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = maxSets;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    outPool = VK_NULL_HANDLE;
    return vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &outPool) == VK_SUCCESS;
}

bool VulkanRenderer::AllocateTextureDescriptorSet(VkDescriptorSet& outSet) {
    if (m_descriptorSetLayout == VK_NULL_HANDLE) {
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    for (auto it = m_descriptorPools.rbegin(); it != m_descriptorPools.rend(); ++it) {
        allocInfo.descriptorPool = *it;
        const VkResult result = vkAllocateDescriptorSets(m_device, &allocInfo, &outSet);
        if (result == VK_SUCCESS) {
            return true;
        }
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
            return false;
        }
    }

    VkDescriptorPool newPool = VK_NULL_HANDLE;
    if (!CreateDescriptorPool(m_descriptorPoolMaxSets, newPool)) {
        return false;
    }
    m_descriptorPools.push_back(newPool);
    allocInfo.descriptorPool = newPool;
    return vkAllocateDescriptorSets(m_device, &allocInfo, &outSet) == VK_SUCCESS;
}

bool VulkanRenderer::CreateGraphicsPipeline() {
    if (!CreatePipeline(m_renderPass, &m_pipeline, true)) {
        return false;
    }
    if (!CreatePipeline(m_offscreenRenderPass, &m_offscreenPipeline, false)) {
        return false;
    }
    return true;
}

bool VulkanRenderer::CreatePipeline(VkRenderPass renderPass, VkPipeline* outPipeline, bool createPipelineLayout) {
    if (!outPipeline) {
        return false;
    }

    VkShaderModule vertShader = VK_NULL_HANDLE;
    VkShaderModule fragShader = VK_NULL_HANDLE;
    if (!EnsureShaderModules(vertShader, fragShader)) {
        return false;
    }

    VkPipelineShaderStageCreateInfo vertStage = {};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertShader;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage = {};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragShader;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertStage, fragStage};

    VkVertexInputBindingDescription bindingDesc = {};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(Vertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attrDesc = {};
    attrDesc[0].binding = 0;
    attrDesc[0].location = 0;
    attrDesc[0].format = VK_FORMAT_R32G32_SFLOAT;
    attrDesc[0].offset = offsetof(Vertex, pos);
    attrDesc[1].binding = 0;
    attrDesc[1].location = 1;
    attrDesc[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrDesc[1].offset = offsetof(Vertex, uv);
    attrDesc[2].binding = 0;
    attrDesc[2].location = 2;
    attrDesc[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrDesc[2].offset = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDesc.size());
    vertexInput.pVertexAttributeDescriptions = attrDesc.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster = {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa = {};
    msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment = {};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlend = {};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    std::array<VkDynamicState, 2> dynamics = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamics.size());
    dynamicState.pDynamicStates = dynamics.data();

    if (createPipelineLayout) {
        VkPipelineLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_descriptorSetLayout;
        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(m_device, vertShader, nullptr);
            vkDestroyShaderModule(m_device, fragShader, nullptr);
            return false;
        }
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &msaa;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, outPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(m_device, vertShader, nullptr);
        vkDestroyShaderModule(m_device, fragShader, nullptr);
        return false;
    }

    vkDestroyShaderModule(m_device, vertShader, nullptr);
    vkDestroyShaderModule(m_device, fragShader, nullptr);
    return true;
}

bool VulkanRenderer::CreateFramebuffers() {
    m_swapchainFramebuffers.resize(m_swapchainImageViews.size());
    for (size_t i = 0; i < m_swapchainImageViews.size(); ++i) {
        VkImageView attachments[] = {m_swapchainImageViews[i]};

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapchainExtent.width;
        framebufferInfo.height = m_swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapchainFramebuffers[i]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

bool VulkanRenderer::CreateCommandPoolAndBuffers() {
    QueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = indices.graphicsFamily;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        return false;
    }

    m_commandBuffers.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

    return vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) == VK_SUCCESS;
}

bool VulkanRenderer::CreateFrameResources() {
    m_frameResources.resize(kMaxFramesInFlight);
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        FrameResources& fr = m_frameResources[i];

        VkBufferCreateInfo vbInfo = {};
        vbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vbInfo.size = sizeof(Vertex) * kMaxVerticesPerFrame;
        vbInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        if (vmaCreateBuffer(m_allocator, &vbInfo, &allocInfo, &fr.vertexBuffer, &fr.vertexAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }
        vmaMapMemory(m_allocator, fr.vertexAllocation, &fr.vertexMapped);

        VkBufferCreateInfo ibInfo = {};
        ibInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ibInfo.size = sizeof(uint32_t) * kMaxIndicesPerFrame;
        ibInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        if (vmaCreateBuffer(m_allocator, &ibInfo, &allocInfo, &fr.indexBuffer, &fr.indexAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }
        vmaMapMemory(m_allocator, fr.indexAllocation, &fr.indexMapped);
    }
    return true;
}

bool VulkanRenderer::CreateSyncObjects() {
    m_imageAvailableSemaphores.resize(kMaxFramesInFlight);
    m_renderFinishedSemaphores.resize(kMaxFramesInFlight);
    m_inFlightFences.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (vkCreateSemaphore(m_device, &semInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

bool VulkanRenderer::CreateWhiteTexture() {
    const unsigned char white[4] = {255, 255, 255, 255};
    return CreateTextureFromRGBA(white, 1, 1, m_whiteTextureId);
}

void VulkanRenderer::DestroySwapchainObjects() {
    if (m_device == VK_NULL_HANDLE) return;

    for (auto& [id, rt] : m_renderTargets) {
        (void)id;
        if (rt.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, rt.framebuffer, nullptr);
            rt.framebuffer = VK_NULL_HANDLE;
        }
    }

    for (VkFramebuffer framebuffer : m_swapchainFramebuffers) {
        vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }
    m_swapchainFramebuffers.clear();

    if (m_offscreenPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_offscreenPipeline, nullptr);
        m_offscreenPipeline = VK_NULL_HANDLE;
    }
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
    if (m_offscreenRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_offscreenRenderPass, nullptr);
        m_offscreenRenderPass = VK_NULL_HANDLE;
    }

    for (VkImageView imageView : m_swapchainImageViews) {
        vkDestroyImageView(m_device, imageView, nullptr);
    }
    m_swapchainImageViews.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
    m_swapchainImages.clear();
}

bool VulkanRenderer::RecreateRenderTargetFramebuffers() {
    for (auto& [id, rt] : m_renderTargets) {
        (void)id;
        auto itTex = m_textures.find(rt.textureId);
        if (itTex == m_textures.end()) {
            continue;
        }

        if (rt.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, rt.framebuffer, nullptr);
            rt.framebuffer = VK_NULL_HANDLE;
        }

        VkImageView attachments[] = {itTex->second.imageView};
        VkFramebufferCreateInfo fbInfo = {};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_offscreenRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = attachments;
        fbInfo.width = rt.width;
        fbInfo.height = rt.height;
        fbInfo.layers = 1;
        if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &rt.framebuffer) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

void VulkanRenderer::DestroyCoreObjects() {
    if (m_device != VK_NULL_HANDLE) {
        for (FrameResources& fr : m_frameResources) {
            if (fr.vertexMapped && fr.vertexAllocation) vmaUnmapMemory(m_allocator, fr.vertexAllocation);
            if (fr.indexMapped && fr.indexAllocation) vmaUnmapMemory(m_allocator, fr.indexAllocation);
            if (fr.vertexBuffer && fr.vertexAllocation) vmaDestroyBuffer(m_allocator, fr.vertexBuffer, fr.vertexAllocation);
            if (fr.indexBuffer && fr.indexAllocation) vmaDestroyBuffer(m_allocator, fr.indexBuffer, fr.indexAllocation);
        }
    }
    m_frameResources.clear();
    m_renderTargets.clear();
    m_fonts.clear();
    m_framePasses.clear();
    m_activeRenderTargetId = 0;

    if (m_device != VK_NULL_HANDLE) {
        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
            if (i < m_imageAvailableSemaphores.size() && m_imageAvailableSemaphores[i]) {
                vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
            }
            if (i < m_renderFinishedSemaphores.size() && m_renderFinishedSemaphores[i]) {
                vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
            }
            if (i < m_inFlightFences.size() && m_inFlightFences[i]) {
                vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
            }
        }
    }
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();

    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    m_commandBuffers.clear();

    for (VkDescriptorPool pool : m_descriptorPools) {
        if (pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, pool, nullptr);
        }
    }
    m_descriptorPools.clear();
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }

    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_surface != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::RecreateSwapchain() {
    if (!m_window || m_device == VK_NULL_HANDLE) {
        return;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(m_window->GetNativeHandle(), &width, &height);
    if (width == 0 || height == 0) {
        return;
    }

    vkDeviceWaitIdle(m_device);
    Logger::Info("Recreating Vulkan swapchain", "vulkan");
    DestroySwapchainObjects();
    if (!CreateSwapchainAndViews()) return;
    if (!CreateRenderPass()) return;
    if (!CreateOffscreenRenderPass()) return;
    if (!CreateGraphicsPipeline()) return;
    if (!CreateFramebuffers()) return;
    RecreateRenderTargetFramebuffers();
}

VulkanRenderer::QueueFamilyIndices VulkanRenderer::FindQueueFamilies(VkPhysicalDevice device) const {
    QueueFamilyIndices out = {};
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());

    for (uint32_t i = 0; i < familyCount; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            out.graphicsFamily = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport == VK_TRUE) {
            out.presentFamily = i;
        }
        if (out.IsComplete()) {
            break;
        }
    }
    return out;
}

VulkanRenderer::SwapchainSupport VulkanRenderer::QuerySwapchainSupport(VkPhysicalDevice device) const {
    SwapchainSupport out = {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &out.capabilities);

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &count, nullptr);
    if (count > 0) {
        out.formats.resize(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &count, out.formats.data());
    }

    count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &count, nullptr);
    if (count > 0) {
        out.presentModes.resize(count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &count, out.presentModes.data());
    }
    return out;
}

VkSurfaceFormatKHR VulkanRenderer::ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const {
    for (const VkSurfaceFormatKHR& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats.front();
}

VkPresentModeKHR VulkanRenderer::ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes) const {
    for (VkPresentModeKHR mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRenderer::ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(m_window->GetNativeHandle(), &width, &height);
    VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return actualExtent;
}

bool VulkanRenderer::RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        return false;
    }

    const FrameResources& fr = m_frameResources[m_frameIndex];
    VkDeviceSize offsets[] = {0};

    auto transitionImage = [&](VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    };

    for (const FramePass& pass : m_framePasses) {
        if (pass.renderTargetId == 0 || pass.drawItemCount == 0) {
            continue;
        }
        auto itRt = m_renderTargets.find(pass.renderTargetId);
        if (itRt == m_renderTargets.end()) {
            continue;
        }
        auto itTex = m_textures.find(itRt->second.textureId);
        if (itTex == m_textures.end()) {
            continue;
        }

        transitionImage(itTex->second.image, itRt->second.imageLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        itRt->second.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkClearValue rtClearValue = {};
        rtClearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

        VkRenderPassBeginInfo rtPassInfo = {};
        rtPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rtPassInfo.renderPass = m_offscreenRenderPass;
        rtPassInfo.framebuffer = itRt->second.framebuffer;
        rtPassInfo.renderArea.extent = {pass.width, pass.height};
        rtPassInfo.clearValueCount = 1;
        rtPassInfo.pClearValues = &rtClearValue;

        vkCmdBeginRenderPass(cmd, &rtPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = {};
        viewport.width = static_cast<float>(pass.width);
        viewport.height = static_cast<float>(pass.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = {};
        scissor.extent = {pass.width, pass.height};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_offscreenPipeline);
        vkCmdBindVertexBuffers(cmd, 0, 1, &fr.vertexBuffer, offsets);
        vkCmdBindIndexBuffer(cmd, fr.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        for (uint32_t i = 0; i < pass.drawItemCount; ++i) {
            const DrawItem& item = m_drawItems[pass.firstDrawItem + i];
            auto itTexDraw = m_textures.find(item.textureId);
            if (itTexDraw == m_textures.end()) {
                continue;
            }
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipelineLayout,
                0,
                1,
                &itTexDraw->second.descriptorSet,
                0,
                nullptr
            );
            vkCmdDrawIndexed(cmd, item.indexCount, 1, item.firstIndex, 0, 0);
        }

        vkCmdEndRenderPass(cmd);
        transitionImage(itTex->second.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        itRt->second.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkClearValue clearValue = {};
    clearValue.color = {{m_clearColor.x, m_clearColor.y, m_clearColor.z, m_clearColor.w}};
    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.extent = m_swapchainExtent;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.width = static_cast<float>(m_swapchainExtent.width);
    viewport.height = static_cast<float>(m_swapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.extent = m_swapchainExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindVertexBuffers(cmd, 0, 1, &fr.vertexBuffer, offsets);
    vkCmdBindIndexBuffer(cmd, fr.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    for (const FramePass& pass : m_framePasses) {
        if (pass.renderTargetId != 0) {
            continue;
        }
        for (uint32_t i = 0; i < pass.drawItemCount; ++i) {
            const DrawItem& item = m_drawItems[pass.firstDrawItem + i];
            auto it = m_textures.find(item.textureId);
            if (it == m_textures.end()) {
                continue;
            }
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipelineLayout,
                0,
                1,
                &it->second.descriptorSet,
                0,
                nullptr
            );
            vkCmdDrawIndexed(cmd, item.indexCount, 1, item.firstIndex, 0, 0);
        }
    }

    vkCmdEndRenderPass(cmd);
    return vkEndCommandBuffer(cmd) == VK_SUCCESS;
}

bool VulkanRenderer::CreateTextureFromRGBA(const unsigned char* rgba, uint32_t width, uint32_t height, uint32_t& outId) {
    if (!rgba || width == 0 || height == 0) {
        return false;
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    void* mapped = nullptr;

    VkBufferCreateInfo stagingInfo = {};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = imageSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo = {};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (vmaCreateBuffer(m_allocator, &stagingInfo, &stagingAllocInfo, &stagingBuffer, &stagingAlloc, nullptr) != VK_SUCCESS) {
        return false;
    }
    vmaMapMemory(m_allocator, stagingAlloc, &mapped);
    std::memcpy(mapped, rgba, static_cast<size_t>(imageSize));
    vmaUnmapMemory(m_allocator, stagingAlloc);

    TextureData tex = {};
    tex.width = width;
    tex.height = height;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo imageAllocInfo = {};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(m_allocator, &imageInfo, &imageAllocInfo, &tex.image, &tex.allocation, nullptr) != VK_SUCCESS) {
        vmaDestroyBuffer(m_allocator, stagingBuffer, stagingAlloc);
        return false;
    }

    if (!TransitionImageLayout(tex.image, imageInfo.format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) ||
        !CopyBufferToImage(stagingBuffer, tex.image, width, height) ||
        !TransitionImageLayout(tex.image, imageInfo.format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        vmaDestroyImage(m_allocator, tex.image, tex.allocation);
        vmaDestroyBuffer(m_allocator, stagingBuffer, stagingAlloc);
        return false;
    }

    vmaDestroyBuffer(m_allocator, stagingBuffer, stagingAlloc);

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tex.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &tex.imageView) != VK_SUCCESS) {
        vmaDestroyImage(m_allocator, tex.image, tex.allocation);
        return false;
    }

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 1.0f;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &tex.sampler) != VK_SUCCESS) {
        vkDestroyImageView(m_device, tex.imageView, nullptr);
        vmaDestroyImage(m_allocator, tex.image, tex.allocation);
        return false;
    }

    if (!AllocateTextureDescriptorSet(tex.descriptorSet)) {
        vkDestroySampler(m_device, tex.sampler, nullptr);
        vkDestroyImageView(m_device, tex.imageView, nullptr);
        vmaDestroyImage(m_allocator, tex.image, tex.allocation);
        return false;
    }

    VkDescriptorImageInfo imageDescriptor = {};
    imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageDescriptor.imageView = tex.imageView;
    imageDescriptor.sampler = tex.sampler;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = tex.descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageDescriptor;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

    outId = m_nextTextureId++;
    m_textures[outId] = tex;
    return true;
}

void VulkanRenderer::DestroyTexture(uint32_t textureId) {
    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) {
        return;
    }

    TextureData& tex = it->second;
    if (tex.sampler) vkDestroySampler(m_device, tex.sampler, nullptr);
    if (tex.imageView) vkDestroyImageView(m_device, tex.imageView, nullptr);
    if (tex.image && tex.allocation) vmaDestroyImage(m_allocator, tex.image, tex.allocation);
    m_textures.erase(it);
}

bool VulkanRenderer::EnsureShaderModules(VkShaderModule& outVert, VkShaderModule& outFrag) {
    std::vector<char> vertCode;
    std::vector<char> fragCode;
#ifdef VULKOR_SHADER_DIR
    const std::string vertPath = std::string(VULKOR_SHADER_DIR) + "/vulkan_2d.vert.spv";
    const std::string fragPath = std::string(VULKOR_SHADER_DIR) + "/vulkan_2d.frag.spv";
#else
    const std::string vertPath = "vulkan_2d.vert.spv";
    const std::string fragPath = "vulkan_2d.frag.spv";
#endif
    if (!ReadBinaryFile(vertPath, vertCode) || !ReadBinaryFile(fragPath, fragCode)) {
        std::cerr << "Failed to read Vulkan shaders. Expected: " << vertPath << " and " << fragPath << '\n';
        return false;
    }

    VkShaderModuleCreateInfo vertInfo = {};
    vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertInfo.codeSize = vertCode.size();
    vertInfo.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
    if (vkCreateShaderModule(m_device, &vertInfo, nullptr, &outVert) != VK_SUCCESS) {
        return false;
    }

    VkShaderModuleCreateInfo fragInfo = {};
    fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragInfo.codeSize = fragCode.size();
    fragInfo.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
    if (vkCreateShaderModule(m_device, &fragInfo, nullptr, &outFrag) != VK_SUCCESS) {
        vkDestroyShaderModule(m_device, outVert, nullptr);
        outVert = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool VulkanRenderer::ReadBinaryFile(const std::string& path, std::vector<char>& outData) const {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    const std::streamsize fileSize = file.tellg();
    outData.resize(static_cast<size_t>(fileSize));
    file.seekg(0);
    file.read(outData.data(), fileSize);
    return true;
}

bool VulkanRenderer::UploadFrameGeometry(uint32_t frameIndex) {
    if (m_vertices.size() > kMaxVerticesPerFrame || m_indices.size() > kMaxIndicesPerFrame) {
        std::cerr << "Geometry overflow: increase frame buffer limits\n";
        return false;
    }

    FrameResources& fr = m_frameResources[frameIndex];
    if (!m_vertices.empty()) {
        std::memcpy(fr.vertexMapped, m_vertices.data(), m_vertices.size() * sizeof(Vertex));
    }
    if (!m_indices.empty()) {
        std::memcpy(fr.indexMapped, m_indices.data(), m_indices.size() * sizeof(uint32_t));
    }
    return true;
}

void VulkanRenderer::BuildCombinedFrameGeometry() {
    std::vector<Vertex> screenVertices = std::move(m_vertices);
    std::vector<uint32_t> screenIndices = std::move(m_indices);
    std::vector<DrawItem> screenDrawItems = std::move(m_drawItems);

    m_vertices.clear();
    m_indices.clear();
    m_drawItems.clear();
    m_framePasses.clear();

    for (auto& [id, rt] : m_renderTargets) {
        (void)id;
        if (rt.drawItems.empty()) {
            continue;
        }

        const uint32_t baseVertex = static_cast<uint32_t>(m_vertices.size());
        const uint32_t baseIndex = static_cast<uint32_t>(m_indices.size());
        const uint32_t firstDraw = static_cast<uint32_t>(m_drawItems.size());

        m_vertices.insert(m_vertices.end(), rt.vertices.begin(), rt.vertices.end());
        for (uint32_t idx : rt.indices) {
            m_indices.push_back(baseVertex + idx);
        }
        for (DrawItem item : rt.drawItems) {
            item.firstIndex += baseIndex;
            m_drawItems.push_back(item);
        }

        m_framePasses.push_back(FramePass{
            id,
            firstDraw,
            static_cast<uint32_t>(m_drawItems.size() - firstDraw),
            rt.width,
            rt.height
        });
    }

    {
        const uint32_t baseVertex = static_cast<uint32_t>(m_vertices.size());
        const uint32_t baseIndex = static_cast<uint32_t>(m_indices.size());
        const uint32_t firstDraw = static_cast<uint32_t>(m_drawItems.size());

        m_vertices.insert(m_vertices.end(), screenVertices.begin(), screenVertices.end());
        for (uint32_t idx : screenIndices) {
            m_indices.push_back(baseVertex + idx);
        }
        for (DrawItem item : screenDrawItems) {
            item.firstIndex += baseIndex;
            m_drawItems.push_back(item);
        }

        m_framePasses.push_back(FramePass{
            0,
            firstDraw,
            static_cast<uint32_t>(m_drawItems.size() - firstDraw),
            m_swapchainExtent.width,
            m_swapchainExtent.height
        });
    }
}

void VulkanRenderer::AppendQuadToBatch(
    std::vector<Vertex>& vertices,
    std::vector<uint32_t>& indices,
    std::vector<DrawItem>& drawItems,
    uint32_t textureId,
    const math::Vec2& p0,
    const math::Vec2& p1,
    const math::Vec2& p2,
    const math::Vec2& p3,
    const math::Vec4& color
) {
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.push_back({{p0.x, p0.y}, {0.0f, 0.0f}, {color.x, color.y, color.z, color.w}});
    vertices.push_back({{p1.x, p1.y}, {1.0f, 0.0f}, {color.x, color.y, color.z, color.w}});
    vertices.push_back({{p2.x, p2.y}, {1.0f, 1.0f}, {color.x, color.y, color.z, color.w}});
    vertices.push_back({{p3.x, p3.y}, {0.0f, 1.0f}, {color.x, color.y, color.z, color.w}});

    const uint32_t firstIndex = static_cast<uint32_t>(indices.size());
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
    indices.push_back(base + 0);

    if (!drawItems.empty() && drawItems.back().textureId == textureId &&
        drawItems.back().firstIndex + drawItems.back().indexCount == firstIndex) {
        drawItems.back().indexCount += 6;
    } else {
        drawItems.push_back({textureId, firstIndex, 6});
    }
}

void VulkanRenderer::AppendQuad(
    uint32_t textureId,
    const math::Vec2& p0,
    const math::Vec2& p1,
    const math::Vec2& p2,
    const math::Vec2& p3,
    const math::Vec4& color
) {
    if (m_activeRenderTargetId != 0) {
        auto itRt = m_renderTargets.find(m_activeRenderTargetId);
        if (itRt != m_renderTargets.end()) {
            AppendQuadToBatch(itRt->second.vertices, itRt->second.indices, itRt->second.drawItems, textureId, p0, p1, p2, p3, color);
            return;
        }
    }
    AppendQuadToBatch(m_vertices, m_indices, m_drawItems, textureId, p0, p1, p2, p3, color);
}

math::Vec2 VulkanRenderer::ToNdc(const math::Vec2& p, float width, float height) const {
    const float w = width > 0.0f ? width : 1.0f;
    const float h = height > 0.0f ? height : 1.0f;
    return {(p.x / w) * 2.0f - 1.0f, (p.y / h) * 2.0f - 1.0f};
}

bool VulkanRenderer::DecodePNG(const std::string& path, std::vector<unsigned char>& outPixels, int& outWidth, int& outHeight) const {
    FILE* file = nullptr;
#if defined(_MSC_VER)
    fopen_s(&file, path.c_str(), "rb");
#else
    file = std::fopen(path.c_str(), "rb");
#endif
    if (!file) {
        return false;
    }

    png_byte header[8];
    if (std::fread(header, 1, sizeof(header), file) != sizeof(header) || png_sig_cmp(header, 0, sizeof(header)) != 0) {
        std::fclose(file);
        return false;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        std::fclose(file);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        std::fclose(file);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(file);
        return false;
    }

    png_init_io(png, file);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    outWidth = static_cast<int>(png_get_image_width(png, info));
    outHeight = static_cast<int>(png_get_image_height(png, info));
    png_byte colorType = png_get_color_type(png, info);
    png_byte bitDepth = png_get_bit_depth(png, info);

    if (bitDepth == 16) png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }

    png_read_update_info(png, info);
    outPixels.resize(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4u);
    std::vector<png_bytep> rows(static_cast<size_t>(outHeight));
    for (int y = 0; y < outHeight; ++y) {
        rows[static_cast<size_t>(y)] = outPixels.data() + static_cast<size_t>(y) * static_cast<size_t>(outWidth) * 4u;
    }
    png_read_image(png, rows.data());
    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(file);
    return true;
}

math::Vec2 VulkanRenderer::WorldToScreen(const math::Vec2& p) const {
    const math::Vec2 local = {p.x - m_camera.position.x, p.y - m_camera.position.y};
    const float c = std::cos(-m_camera.rotationRadians);
    const float s = std::sin(-m_camera.rotationRadians);
    return {(local.x * c - local.y * s) * m_camera.zoom, (local.x * s + local.y * c) * m_camera.zoom};
}

bool VulkanRenderer::TransitionImageLayout(VkImage image, VkFormat, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &cmd) != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    return true;
}

bool VulkanRenderer::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &cmd) != VK_SUCCESS) {
        return false;
    }

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    return true;
}

} // namespace vulkor
