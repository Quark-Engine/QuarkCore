#include "QuarkGLTexture.hpp"
#include "../../QuarkInternal.hpp"

#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>

namespace qc {

GLuint QuarkGLTexture::CreateTextureFromRgba(const uint8_t* pixels, int width, int height) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);

    const GLint textureFilter = gTextureFilterMode == TextureFilterMode::Nearest ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, textureFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, textureFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

void QuarkGLTexture::FlipRowsRgba(void* pixels, int width, int height) {
    const int rowBytes = width * 4;
    std::vector<uint8_t> tmp(rowBytes);
    uint8_t* data = static_cast<uint8_t*>(pixels);

    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top = data + static_cast<size_t>(y) * rowBytes;
        uint8_t* bottom = data + static_cast<size_t>(height - 1 - y) * rowBytes;
        std::memcpy(tmp.data(), top, rowBytes);
        std::memcpy(top, bottom, rowBytes);
        std::memcpy(bottom, tmp.data(), rowBytes);
    }
}

ITexture QuarkGLTexture::LoadTexture(const char* path) {
    if (!path) return {};

    std::error_code pathError;
    std::string cacheKey = std::filesystem::weakly_canonical(path, pathError).string();
    if (pathError || cacheKey.empty()) {
        cacheKey = std::filesystem::path(path).lexically_normal().string();
    }

    const auto cached = m_textureCache.find(cacheKey);
    if (cached != m_textureCache.end()) {
        cached->second.references++;
        return cached->second.texture;
    }

    ImageFileData img;
    ITexture texture{};
    const bool loaded = LoadImageFile(path, img, 4);
    if (loaded) {
        texture.id = CreateTextureFromRgba(img.pixels.data(), img.width, img.height);
        texture.width = img.width;
        texture.height = img.height;
        texture.mipmaps = 1;
        texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        texture.valid = true;

        m_textureCache.emplace(cacheKey, CachedTexture{texture, 1});
        m_textureCacheKeys.emplace(texture.id, cacheKey);
    }
    return texture;
}

ITexture QuarkGLTexture::LoadTextureFromImage(const Image& image) {
    if (!image.data || image.width <= 0 || image.height <= 0 || image.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
        return {};
    }

    ITexture texture{};
    texture.id = CreateTextureFromRgba(static_cast<const uint8_t*>(image.data), image.width, image.height);
    if (texture.id == 0) {
        return texture;
    }

    texture.width = image.width;
    texture.height = image.height;
    texture.mipmaps = 1;
    texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    texture.valid = true;
    return texture;
}

void QuarkGLTexture::UnloadTexture(ITexture& texture) {
    if (texture.id) {
        const auto cacheKey = m_textureCacheKeys.find(texture.id);
        if (cacheKey != m_textureCacheKeys.end()) {
            auto cached = m_textureCache.find(cacheKey->second);
            if (cached != m_textureCache.end()) {
                cached->second.references--;
                if (cached->second.references > 0) {
                    texture = {};
                    return;
                }
                glDeleteTextures(1, &texture.id);
                m_textureCache.erase(cached);
            }
            m_textureCacheKeys.erase(cacheKey);
            texture = {};
            return;
        }

        glDeleteTextures(1, &texture.id);
    }
    texture = {};
}

ITexture QuarkGLTexture::GetRenderTextureTexture(IRenderTexture target) const {
    return target.texture;
}

IRenderTexture QuarkGLTexture::LoadRenderTexture(int width, int height) {
    IRenderTexture target{};

    glGenFramebuffers(1, &target.id);
    glBindFramebuffer(GL_FRAMEBUFFER, target.id);

    glGenTextures(1, &target.texture.id);
    glBindTexture(GL_TEXTURE_2D, target.texture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    const GLint textureFilter = gTextureFilterMode == TextureFilterMode::Nearest ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, textureFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, textureFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture.id, 0);

    glGenRenderbuffers(1, &target.depthId);
    glBindRenderbuffer(GL_RENDERBUFFER, target.depthId);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, target.depthId);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    target.texture.width = width;
    target.texture.height = height;
    target.texture.mipmaps = 1;
    target.texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    target.texture.valid = true;
    return target;
}

void QuarkGLTexture::UnloadRenderTexture(IRenderTexture target) {
    if (target.id) glDeleteFramebuffers(1, &target.id);
    if (target.depthId) glDeleteRenderbuffers(1, &target.depthId);
    if (target.texture.id) glDeleteTextures(1, &target.texture.id);
}

ITexture QuarkGLTexture::GenCheckerTexture(int width, int height, int cellSize, Color colorA, Color colorB) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Color color = (((x / cellSize) + (y / cellSize)) % 2 == 0) ? colorA : colorB;
            const size_t index = (static_cast<size_t>(y) * width + x) * 4;
            pixels[index + 0] = color.r;
            pixels[index + 1] = color.g;
            pixels[index + 2] = color.b;
            pixels[index + 3] = color.a;
        }
    }

    ITexture texture{};
    texture.id = CreateTextureFromRgba(pixels.data(), width, height);
    texture.width = width;
    texture.height = height;
    texture.mipmaps = 1;
    texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    texture.valid = true;
    return texture;
}

bool QuarkGLTexture::IsTextureValid(ITexture& texture) const {
    return texture.valid && texture.id != 0;
}

bool QuarkGLTexture::IsRenderTextureValid(IRenderTexture& target) const {
    return target.id != 0 && target.texture.valid;
}

Image QuarkGLTexture::ReadTextureImage(const ITexture& texture) const {
    if (!texture.valid || texture.id == 0 || texture.width <= 0 || texture.height <= 0) return Image{};

    const size_t bytes = static_cast<size_t>(texture.width) * texture.height * 4;
    void* buffer = MemAlloc(bytes);
    if (!buffer) return Image{};

    glBindTexture(GL_TEXTURE_2D, texture.id);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    glBindTexture(GL_TEXTURE_2D, 0);

    FlipRowsRgba(buffer, texture.width, texture.height);

    Image image{};
    image.data = buffer;
    image.width = texture.width;
    image.height = texture.height;
    image.mipmaps = 1;
    image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    return image;
}

Image QuarkGLTexture::ReadScreenImage(int width, int height, GLuint currentFbo) const {
    if (width <= 0 || height <= 0) return Image{};

    const size_t bytes = static_cast<size_t>(width) * height * 4;
    void* buffer = MemAlloc(bytes);
    if (!buffer) return Image{};

    glBindFramebuffer(GL_FRAMEBUFFER, currentFbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    if (currentFbo != 0) {
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    } else {
        glReadBuffer(GL_BACK);
    }
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer);

    FlipRowsRgba(buffer, width, height);

    Image image{};
    image.data = buffer;
    image.width = width;
    image.height = height;
    image.mipmaps = 1;
    image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    return image;
}

} // namespace qc
