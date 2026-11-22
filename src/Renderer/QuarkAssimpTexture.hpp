#ifndef __QUARK_ASSIMP_TEXTURE__
#define __QUARK_ASSIMP_TEXTURE__

#include "QuarkCore/QuarkImage.hpp"

#include <assimp/scene.h>

#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <vector>

namespace qci
{
    inline bool DecodeEmbeddedAssimpTexture(const aiScene& scene, const aiString& reference,
                                            Image& image, std::vector<unsigned char>& vRawPixels,
                                            bool& ownsImage)
    {
        ownsImage = false;
        image = {};

        const std::string textureReference = reference.C_Str();
        if (textureReference.size() < 2 || textureReference[0] != '*')
        {
            return false;
        }

        size_t embeddedIndex = 0;
        try
        {
            size_t parsedChars = 0;
            const unsigned long parsedIndex = std::stoul(textureReference.substr(1), &parsedChars);
            if (parsedChars != textureReference.size() - 1 ||
                parsedIndex >= scene.mNumTextures)
            {
                return false;
            }
            embeddedIndex = static_cast<size_t>(parsedIndex);
        }
        catch (const std::exception&)
        {
            return false;
        }

        if (!scene.mTextures)
        {
            return false;
        }
        const aiTexture* pEmbeddedTexture = scene.mTextures[embeddedIndex];
        if (!pEmbeddedTexture || !pEmbeddedTexture->pcData)
        {
            return false;
        }

        if (pEmbeddedTexture->mHeight == 0)
        {
            if (pEmbeddedTexture->mWidth > static_cast<unsigned int>(std::numeric_limits<int>::max()))
            {
                return false;
            }

            std::string fileType = pEmbeddedTexture->achFormatHint;
            if (!fileType.empty() && fileType.front() != '.')
            {
                fileType.insert(fileType.begin(), '.');
            }
            image = LoadImageFromMemory(
                fileType.empty() ? ".bin" : fileType.c_str(),
                reinterpret_cast<const unsigned char*>(pEmbeddedTexture->pcData),
                static_cast<int>(pEmbeddedTexture->mWidth));
            ownsImage = image.data != nullptr;
            if (!IsImageValid(image))
            {
                if (ownsImage)
                {
                    UnloadImage(image);
                }
                image = {};
                ownsImage = false;
                return false;
            }
            return true;
        }

        if (pEmbeddedTexture->mWidth == 0 ||
            pEmbeddedTexture->mWidth > static_cast<unsigned int>(std::numeric_limits<int>::max()) ||
            pEmbeddedTexture->mHeight > static_cast<unsigned int>(std::numeric_limits<int>::max()) ||
            static_cast<size_t>(pEmbeddedTexture->mWidth) >
                std::numeric_limits<size_t>::max() / pEmbeddedTexture->mHeight / 4)
        {
            return false;
        }

        const size_t pixelCount =
            static_cast<size_t>(pEmbeddedTexture->mWidth) * pEmbeddedTexture->mHeight;
        vRawPixels.resize(pixelCount * 4);
        for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
        {
            const aiTexel& source = pEmbeddedTexture->pcData[pixelIndex];
            vRawPixels[pixelIndex * 4] = source.r;
            vRawPixels[pixelIndex * 4 + 1] = source.g;
            vRawPixels[pixelIndex * 4 + 2] = source.b;
            vRawPixels[pixelIndex * 4 + 3] = source.a;
        }

        image.data = vRawPixels.data();
        image.width = static_cast<int>(pEmbeddedTexture->mWidth);
        image.height = static_cast<int>(pEmbeddedTexture->mHeight);
        image.mipmaps = 1;
        image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        return true;
    }

    inline void ReleaseEmbeddedAssimpTexture(Image& image, bool ownsImage)
    {
        if (ownsImage)
        {
            UnloadImage(image);
        }
        image = {};
    }
} // qci

#endif // __QUARK_ASSIMP_TEXTURE__
