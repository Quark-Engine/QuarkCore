#include "QuarkGLTexture.hpp"
#include "../../QuarkInternal.hpp"

#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>

namespace qci
{
    GLuint QuarkGLTexture::CreateTextureFromRgba(const uint8_t* pPixels, int width, int height)
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);

        const GLint textureFilter = g_TextureFilter == TEXTURE_FILTER_POINT ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, textureFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, textureFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pPixels);
        glBindTexture(GL_TEXTURE_2D, 0);
        return id;
    }

    void QuarkGLTexture::FlipRowsRgba(void* pPixels, int width, int height)
    {
        const int rowBytes = width * 4;
        std::vector<uint8_t> vTmp(rowBytes);
        uint8_t* pData = static_cast<uint8_t*>(pPixels);

        for (int y = 0; y < height / 2; ++y)
        {
            uint8_t* pTop = pData + static_cast<size_t>(y) * rowBytes;
            uint8_t* pBottom = pData + static_cast<size_t>(height - 1 - y) * rowBytes;
            std::memcpy(vTmp.data(), pTop, rowBytes);
            std::memcpy(pTop, pBottom, rowBytes);
            std::memcpy(pBottom, vTmp.data(), rowBytes);
        }
    }

    ITexture QuarkGLTexture::LoadTexture(const char* pPath)
    {
        if (!pPath)
        {
            return {};
        }

        std::error_code pathError;
        std::string cacheKey = std::filesystem::weakly_canonical(pPath, pathError).string();
        if (pathError || cacheKey.empty())
        {
            cacheKey = std::filesystem::path(pPath).lexically_normal().string();
        }

        const auto cached = m_TextureCache.find(cacheKey);
        if (cached != m_TextureCache.end())
        {
            cached->second.m_References++;
            return cached->second.m_Texture;
        }

        ImageFileData img;
        ITexture texture{};
        const bool loaded = LoadImageFile(pPath, img, 4);
        if (loaded)
        {
            texture.m_Id = CreateTextureFromRgba(img.m_vPixels.data(), img.m_Width, img.m_Height);
            texture.m_Width = img.m_Width;
            texture.m_Height = img.m_Height;
            texture.m_Mipmaps = 1;
            texture.m_Format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
            texture.m_Valid = true;

            m_TextureCache.emplace(cacheKey, CachedTexture{texture, 1});
            m_TextureCacheKeys.emplace(texture.m_Id, cacheKey);
        }
        return texture;
    }

    ITexture QuarkGLTexture::LoadTextureFromImage(const Image& image)
    {
        if (!image.data || image.width <= 0 || image.height <= 0 || image.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
        {
            return {};
        }

        ITexture texture{};
        texture.m_Id = CreateTextureFromRgba(static_cast<const uint8_t*>(image.data), image.width, image.height);
        if (texture.m_Id == 0)
        {
            return texture;
        }

        texture.m_Width = image.width;
        texture.m_Height = image.height;
        texture.m_Mipmaps = 1;
        texture.m_Format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        texture.m_Valid = true;
        return texture;
    }

    ITexture QuarkGLTexture::LoadTextureCubemap(const unsigned char* pRgbaFaces, int faceSize)
    {
        if (!pRgbaFaces || faceSize <= 0)
        {
            return {};
        }

        GLuint id = 0;
        glGenTextures(1, &id);
        if (id == 0)
        {
            return {};
        }
        glBindTexture(GL_TEXTURE_CUBE_MAP, id);

        const GLint textureFilter = g_TextureFilter == TEXTURE_FILTER_POINT ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, textureFilter);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, textureFilter);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        const size_t faceBytes = static_cast<size_t>(faceSize) * faceSize * 4;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (GLuint face = 0; face < 6; ++face)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA8,
                         faceSize, faceSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         pRgbaFaces + face * faceBytes);
        }
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        m_CubemapTextureIds.insert(id);

        return ITexture{id, faceSize, faceSize, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, true};
    }

    bool QuarkGLTexture::IsCubemap(uint32_t textureId) const
    {
        return m_CubemapTextureIds.find(static_cast<GLuint>(textureId)) != m_CubemapTextureIds.end();
    }

    void QuarkGLTexture::UnloadTexture(ITexture& texture)
    {
        if (texture.m_Id)
        {
            const auto cacheKey = m_TextureCacheKeys.find(texture.m_Id);
            if (cacheKey != m_TextureCacheKeys.end())
            {
                auto cached = m_TextureCache.find(cacheKey->second);
                if (cached != m_TextureCache.end())
                {
                    cached->second.m_References--;
                    if (cached->second.m_References > 0)
                    {
                        texture = {};
                        return;
                    }
                    glDeleteTextures(1, &texture.m_Id);
                    m_TextureCache.erase(cached);
                }
                m_TextureCacheKeys.erase(cacheKey);
                texture = {};
                return;
            }

            m_CubemapTextureIds.erase(texture.m_Id);
            glDeleteTextures(1, &texture.m_Id);
        }
        texture = {};
    }

    ITexture QuarkGLTexture::GetRenderTextureTexture(IRenderTexture target) const
    {
        return target.m_Texture;
    }

    IRenderTexture QuarkGLTexture::LoadRenderTexture(int width, int height)
    {
        IRenderTexture target{};

        glGenFramebuffers(1, &target.m_Id);
        glBindFramebuffer(GL_FRAMEBUFFER, target.m_Id);

        glGenTextures(1, &target.m_Texture.m_Id);
        glBindTexture(GL_TEXTURE_2D, target.m_Texture.m_Id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        const GLint textureFilter = g_TextureFilter == TEXTURE_FILTER_POINT ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, textureFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, textureFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.m_Texture.m_Id, 0);

        glGenRenderbuffers(1, &target.m_DepthId);
        glBindRenderbuffer(GL_RENDERBUFFER, target.m_DepthId);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, target.m_DepthId);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        target.m_Texture.m_Width = width;
        target.m_Texture.m_Height = height;
        target.m_Texture.m_Mipmaps = 1;
        target.m_Texture.m_Format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        target.m_Texture.m_Valid = true;
        return target;
    }

    void QuarkGLTexture::UnloadRenderTexture(IRenderTexture target)
    {
        if (target.m_Id)
        {
            glDeleteFramebuffers(1, &target.m_Id);
        }
        if (target.m_DepthId)
        {
            glDeleteRenderbuffers(1, &target.m_DepthId);
        }
        if (target.m_Texture.m_Id)
        {
            glDeleteTextures(1, &target.m_Texture.m_Id);
        }
    }

    ITexture QuarkGLTexture::GenCheckerTexture(int width, int height, int cellSize, Color colorA, Color colorB)
    {
        std::vector<uint8_t> vPixels(static_cast<size_t>(width) * height * 4);

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                Color color = (((x / cellSize) + (y / cellSize)) % 2 == 0) ? colorA : colorB;
                const size_t index = (static_cast<size_t>(y) * width + x) * 4;
                vPixels[index + 0] = color.r;
                vPixels[index + 1] = color.g;
                vPixels[index + 2] = color.b;
                vPixels[index + 3] = color.a;
            }
        }

        ITexture texture{};
        texture.m_Id = CreateTextureFromRgba(vPixels.data(), width, height);
        texture.m_Width = width;
        texture.m_Height = height;
        texture.m_Mipmaps = 1;
        texture.m_Format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        texture.m_Valid = true;
        return texture;
    }

    bool QuarkGLTexture::IsTextureValid(ITexture& texture) const
    {
        return texture.m_Valid && texture.m_Id != 0;
    }

    bool QuarkGLTexture::IsRenderTextureValid(IRenderTexture& target) const
    {
        return target.m_Id != 0 && target.m_Texture.m_Valid;
    }

    Image QuarkGLTexture::ReadTextureImage(const ITexture& texture) const
    {
        if (!texture.m_Valid || texture.m_Id == 0 || texture.m_Width <= 0 || texture.m_Height <= 0)
        {
            return Image{};
        }

        const size_t bytes = static_cast<size_t>(texture.m_Width) * texture.m_Height * 4;
        void* pBuffer = MemAlloc(bytes);
        if (!pBuffer)
        {
            return Image{};
        }

        glBindTexture(GL_TEXTURE_2D, texture.m_Id);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pBuffer);
        glBindTexture(GL_TEXTURE_2D, 0);

        FlipRowsRgba(pBuffer, texture.m_Width, texture.m_Height);

        Image image{};
        image.data = pBuffer;
        image.width = texture.m_Width;
        image.height = texture.m_Height;
        image.mipmaps = 1;
        image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        return image;
    }

    Image QuarkGLTexture::ReadScreenImage(int width, int height, GLuint currentFbo) const
    {
        if (width <= 0 || height <= 0)
        {
            return Image{};
        }

        const size_t bytes = static_cast<size_t>(width) * height * 4;
        void* pBuffer = MemAlloc(bytes);
        if (!pBuffer)
        {
            return Image{};
        }

        glBindFramebuffer(GL_FRAMEBUFFER, currentFbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        if (currentFbo != 0)
        {
            glReadBuffer(GL_COLOR_ATTACHMENT0);
        }
        else
        {
            glReadBuffer(GL_BACK);
        }
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pBuffer);

        FlipRowsRgba(pBuffer, width, height);

        Image image{};
        image.data = pBuffer;
        image.width = width;
        image.height = height;
        image.mipmaps = 1;
        image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        return image;
    }
} // qci
