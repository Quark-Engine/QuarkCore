#include "QuarkVkRenderer.hpp"

#include "../../QuarkInternal.hpp"

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

namespace qc {

static float NormalizeColorComponent(std::uint8_t value) {
    return static_cast<float>(value) / 255.0f;
}

void QuarkVkRenderer::DrawTexture(const ITexture& texture, float x, float y, Color tint) {
    const VkTextureData* tex = m_vkResources.Get(texture.id);
    if (tex == nullptr) {
        return;
    }

    const float width  = texture.width  > 0 ? static_cast<float>(texture.width)  : static_cast<float>(tex->width);
    const float height = texture.height > 0 ? static_cast<float>(texture.height) : static_cast<float>(tex->height);
    DrawTexturePro(texture, Rectangle{ 0.f, 0.f, width, height },
                   Rectangle{ x, y, width, height },
                   Vec2{ 0.f, 0.f }, 0.f, tint);
}

void QuarkVkRenderer::DrawTextureV(const ITexture& texture, Vec2 position, Color tint) {
    DrawTexture(texture, position.x, position.y, tint);
}

void QuarkVkRenderer::DrawTextureEx(const ITexture& texture, Vec2 position, float rotation, float scale, Color tint) {
    const VkTextureData* tex = m_vkResources.Get(texture.id);
    if (tex == nullptr) {
        return;
    }

    const float width  = texture.width  > 0 ? static_cast<float>(texture.width)  : static_cast<float>(tex->width);
    const float height = texture.height > 0 ? static_cast<float>(texture.height) : static_cast<float>(tex->height);
    DrawTexturePro(texture,
                   Rectangle{ 0.f, 0.f, width, height },
                   Rectangle{ position.x, position.y, width * scale, height * scale },
                   Vec2{ (width * scale) * 0.5f, (height * scale) * 0.5f },
                   rotation,
                   tint);
}

void QuarkVkRenderer::DrawTextureRec(const ITexture& texture, Rectangle source, Vec2 position, Color tint) {
    const VkTextureData* tex = m_vkResources.Get(texture.id);
    if (tex == nullptr) {
        return;
    }

    if (source.width == 0.f || source.height == 0.f) {
        source.x = 0.f;
        source.y = 0.f;
        source.width = static_cast<float>(texture.width > 0 ? texture.width : tex->width);
        source.height = static_cast<float>(texture.height > 0 ? texture.height : tex->height);
    }

    DrawTexturePro(texture,
                   source,
                   Rectangle{ position.x, position.y, source.width, source.height },
                   Vec2{ 0.f, 0.f },
                   0.f,
                   tint);
}

void QuarkVkRenderer::DrawTexturePro(ITexture texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint) {
    const VkTextureData* tex = m_vkResources.Get(texture.id);
    if (tex == nullptr) {
        return;
    }

    const float texW = texture.width  > 0 ? static_cast<float>(texture.width)  : static_cast<float>(tex->width);
    const float texH = texture.height > 0 ? static_cast<float>(texture.height) : static_cast<float>(tex->height);
    if (texW <= 0.f || texH <= 0.f) {
        return;
    }

    const float u0 = source.x / texW;
    float v0 = source.y / texH;
    const float u1 = (source.x + source.width) / texW;
    float v1 = (source.y + source.height) / texH;
    if (tex->isRenderTarget) {
        std::swap(v0, v1);
    }

    std::array<Vec2, 4> corners = {
        Vec2{ -origin.x,             -origin.y },
        Vec2{ dest.width - origin.x, -origin.y },
        Vec2{ dest.width - origin.x, dest.height - origin.y },
        Vec2{ -origin.x,             dest.height - origin.y }
    };

    if (rotation != 0.f) {
        constexpr float kPi = 3.14159265358979323846f;
        const float radians = rotation * (kPi / 180.f);
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        for (Vec2& corner : corners) {
            const float rx = corner.x * c - corner.y * s;
            const float ry = corner.x * s + corner.y * c;
            corner.x = rx;
            corner.y = ry;
        }
    }

    for (Vec2& corner : corners) {
        corner.x += dest.x;
        corner.y += dest.y;
    }

    const VkDescriptorSet ds = tex->descriptorSet;
    const float r = NormalizeColorComponent(tint.r);
    const float g = NormalizeColorComponent(tint.g);
    const float b = NormalizeColorComponent(tint.b);
    const float a = NormalizeColorComponent(tint.a);

    AppendQuad(ds,
               corners[0].x, corners[0].y,
               corners[1].x, corners[1].y,
               corners[2].x, corners[2].y,
               corners[3].x, corners[3].y,
               r, g, b, a,
               u0, v0, u1, v1);
}

void QuarkVkRenderer::DrawTextureTiled(ITexture texture, float scale, Vec2 offset, Color tint) {
    const VkTextureData* tex = m_vkResources.Get(texture.id);
    if (tex == nullptr || scale <= 0.f) {
        return;
    }

    const float texW = texture.width  > 0 ? static_cast<float>(texture.width)  : static_cast<float>(tex->width);
    const float texH = texture.height > 0 ? static_cast<float>(texture.height) : static_cast<float>(tex->height);
    if (texW <= 0.f || texH <= 0.f) {
        return;
    }

    const float tileW = texW * scale;
    const float tileH = texH * scale;

    uint32_t areaW = m_width > 0 ? static_cast<uint32_t>(m_width) : 0;
    uint32_t areaH = m_height > 0 ? static_cast<uint32_t>(m_height) : 0;
    if (m_activeRenderTargetId != 0) {
        auto rt = m_renderTargets.find(m_activeRenderTargetId);
        if (rt != m_renderTargets.end()) {
            areaW = rt->second.width;
            areaH = rt->second.height;
        }
    } else if (m_swapChainExtent.width != 0 && m_swapChainExtent.height != 0) {
        areaW = m_swapChainExtent.width;
        areaH = m_swapChainExtent.height;
    }

    const int tilesX = static_cast<int>(std::ceil(areaW / tileW)) + 2;
    const int tilesY = static_cast<int>(std::ceil(areaH / tileH)) + 2;

    for (int y = -1; y < tilesY; ++y) {
        for (int x = -1; x < tilesX; ++x) {
            DrawTexture(texture,
                        offset.x + x * tileW,
                        offset.y + y * tileH,
                        tint);
        }
    }
}

void QuarkVkRenderer::DrawTextureNPatch(ITexture texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint) {
    DrawTexturePro(texture, source, dest, origin, rotation, tint);
}

ITexture QuarkVkRenderer::LoadTexture(const char* filePath) {
    TraceLog(LogLevel::Trace, "TEXTURE", TextFormat("[Vulkan] Loading texture from: %s", filePath ? filePath : "<null>"));
    if (!filePath) {
        return ITexture{};
    }

    std::error_code pathError;
    std::string cacheKey = std::filesystem::weakly_canonical(filePath, pathError).string();
    if (pathError || cacheKey.empty()) {
        cacheKey = std::filesystem::path(filePath).lexically_normal().string();
    }

    const auto cached = m_textureCache.find(cacheKey);
    if (cached != m_textureCache.end()) {
        cached->second.references++;
        TraceLog(LogLevel::Trace, "TEXTURE", TextFormat("[Vulkan] Reusing cached texture: %s (ID: %u, References: %d)",
            filePath, cached->second.texture.id, cached->second.references));
        return cached->second.texture;
    }

    ImageFileData img;
    if (!LoadImageFile(filePath, img, 4)) {
        TraceLog(LogLevel::Error, "TEXTURE", TextFormat("[Vulkan] Failed to load texture image: %s", filePath ? filePath : "<null>"));
        return ITexture{};
    }

    const uint32_t textureId = m_vkResources.CreateTextureFromRGBA(img.pixels.data(),
                                                              static_cast<uint32_t>(img.width),
                                                              static_cast<uint32_t>(img.height));
    if (textureId == 0) {
        TraceLog(LogLevel::Error, "TEXTURE", TextFormat("[Vulkan] Failed to upload texture to GPU: %s", filePath ? filePath : "<null>"));
        return ITexture{};
    }

    const ITexture texture{textureId, img.width, img.height, true};
    m_textureCache.emplace(cacheKey, VkCachedTexture{texture, 1});
    m_textureCacheKeys.emplace(textureId, cacheKey);

    TraceLog(LogLevel::Info, "TEXTURE", TextFormat("[Vulkan] Texture loaded successfully: %s (%dx%d, %zu bytes, ID: %u)",
        filePath ? filePath : "<null>", img.width, img.height, img.pixels.size(), textureId));

    return texture;
}

ITexture QuarkVkRenderer::GetRenderTextureTexture(IRenderTexture target) {
    auto itRt = m_renderTargets.find(target.id);
    if (itRt == m_renderTargets.end()) {
        return ITexture{};
    }

    const VkTextureData* tex = m_vkResources.Get(itRt->second.textureId);
    if (tex == nullptr) {
        return ITexture{};
    }

    return ITexture{
        itRt->second.textureId,
        static_cast<int>(tex->width),
        static_cast<int>(tex->height),
        true
    };
}

void QuarkVkRenderer::UnloadTexture(ITexture& texture) {
    if (texture.id == 0 || texture.id == m_whiteTextureId) {
        return;
    }

    const auto cacheKey = m_textureCacheKeys.find(texture.id);
    if (cacheKey != m_textureCacheKeys.end()) {
        auto cached = m_textureCache.find(cacheKey->second);
        if (cached != m_textureCache.end()) {
            cached->second.references--;
            if (cached->second.references > 0) {
                TraceLog(LogLevel::Trace, "TEXTURE", TextFormat("[Vulkan] Released cached texture (ID: %u, References: %d)",
                    texture.id, cached->second.references));
                texture = ITexture{};
                return;
            }
            m_textureCache.erase(cached);
        }
        m_textureCacheKeys.erase(cacheKey);
    }

    TraceLog(LogLevel::Info, "TEXTURE", TextFormat("[Vulkan] Texture unloaded (ID: %u, %dx%d)", texture.id, texture.width, texture.height));
    m_vkResources.DestroyTexture(texture.id);
    texture = ITexture{};
}

bool QuarkVkRenderer::isTextureValid(ITexture& texture) {
    return texture.id != 0 && texture.valid && m_vkResources.Contains(texture.id);
}

IRenderTexture QuarkVkRenderer::LoadRenderTexture(int width, int height) {
    IRenderTexture target = CreateRenderTargetInternal(width, height);
    if (target.id != 0) {
        target.texture = GetRenderTextureTexture(target);
    }
    return target;
}

void QuarkVkRenderer::UnloadRenderTexture(IRenderTexture target) {
    if (target.id == 0) {
        return;
    }
    DestroyRenderTargetInternal(target.id);
}

bool QuarkVkRenderer::isRenderTextureValid(IRenderTexture& target) {
    return target.id != 0 && m_renderTargets.find(target.id) != m_renderTargets.end();
}

ITexture QuarkVkRenderer::GenCheckerTexture(int width, int height, int cellSize, Color colorA, Color colorB) {
    if (width <= 0 || height <= 0) {
        return ITexture{};
    }

    if (cellSize <= 0) {
        cellSize = 1;
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool useA = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            const Color c = useA ? colorA : colorB;
            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
            pixels[idx + 0] = c.r;
            pixels[idx + 1] = c.g;
            pixels[idx + 2] = c.b;
            pixels[idx + 3] = c.a;
        }
    }

    const uint32_t textureId = m_vkResources.CreateTextureFromRGBA(pixels.data(),
                                                              static_cast<uint32_t>(width),
                                                              static_cast<uint32_t>(height));
    if (textureId == 0) {
        return ITexture{};
    }

    TraceLog(LogLevel::Info, "TEXTURE", TextFormat("[Vulkan] Generated checker texture: %dx%d (Cell: %dpx, ID: %u)", width, height, cellSize, textureId));

    return ITexture{
        textureId,
        width,
        height,
        true
    };
}

void QuarkVkRenderer::BeginTextureMode(IRenderTexture target) {
    if (target.id != 0 && m_renderTargets.find(target.id) != m_renderTargets.end()) {
        m_activeRenderTargetId = target.id;
        m_frameGeometryPending = true;
    }
}

void QuarkVkRenderer::EndTextureMode() {
    m_activeRenderTargetId = 0;
}

}; // namespace qc