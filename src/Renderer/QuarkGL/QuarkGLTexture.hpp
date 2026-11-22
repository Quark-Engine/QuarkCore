#ifndef __QUARK_GL_TEXTURE__
#define __QUARK_GL_TEXTURE__

#include "../QuarkIRenderer.hpp"

#include <glad/glad.h>

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace qci
{
    class QuarkGLTexture
    {
    public:
        struct CachedTexture
        {
            ITexture m_Texture{};
            int m_References = 0;
        };

        QuarkGLTexture() = default;

        ITexture LoadTexture(const char* pPath);
        ITexture LoadTextureFromImage(const Image& image);
        ITexture LoadTextureCubemap(const unsigned char* pRgbaFaces, int faceSize);
        bool IsCubemap(uint32_t textureId) const;
        void UnloadTexture(ITexture& texture);
        ITexture GetRenderTextureTexture(IRenderTexture target) const;
        IRenderTexture LoadRenderTexture(int width, int height);
        void UnloadRenderTexture(IRenderTexture target);
        ITexture GenCheckerTexture(int width, int height, int cellSize, Color colorA, Color colorB);
        bool IsTextureValid(ITexture& texture) const;
        bool IsRenderTextureValid(IRenderTexture& target) const;
        Image ReadTextureImage(const ITexture& texture) const;
        Image ReadScreenImage(int width, int height, GLuint currentFbo) const;

        static GLuint CreateTextureFromRgba(const uint8_t* pPixels, int width, int height);
        static void FlipRowsRgba(void* pPixels, int width, int height);

    private:
        std::unordered_map<std::string, CachedTexture> m_TextureCache;
        std::unordered_map<GLuint, std::string> m_TextureCacheKeys;
        std::unordered_set<GLuint> m_CubemapTextureIds;
    };
} // qci

#endif // __QUARK_GL_TEXTURE__
