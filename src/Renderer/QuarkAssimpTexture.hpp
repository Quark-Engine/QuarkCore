#ifndef __QUARK_ASSIMP_TEXTURE__
#define __QUARK_ASSIMP_TEXTURE__

#include "QuarkCore/QuarkImage.hpp"

#include <assimp/scene.h>

#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <vector>

namespace qci {
inline bool DecodeEmbeddedAssimpTexture(const aiScene& scene, const aiString& reference,
                                        Image& image, std::vector<unsigned char>& rawPixels,
                                        bool& ownsImage) {
    ownsImage = false;
    image = {};

    const std::string textureReference = reference.C_Str();
    if (textureReference.size() < 2 || textureReference[0] != '*') {
        return false;
    }

    size_t embeddedIndex = 0;
    try {
        size_t parsedChars = 0;
        const unsigned long parsedIndex = std::stoul(textureReference.substr(1), &parsedChars);
        if (parsedChars != textureReference.size() - 1 ||
            parsedIndex >= scene.mNumTextures) {
            return false;
        }
        embeddedIndex = static_cast<size_t>(parsedIndex);
    } catch (const std::exception&) {
        return false;
    }

    if (!scene.mTextures) {
        return false;
    }
    const aiTexture* embeddedTexture = scene.mTextures[embeddedIndex];
    if (!embeddedTexture || !embeddedTexture->pcData) {
        return false;
    }

    if (embeddedTexture->mHeight == 0) {
        if (embeddedTexture->mWidth > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
            return false;
        }

        std::string fileType = embeddedTexture->achFormatHint;
        if (!fileType.empty() && fileType.front() != '.') {
            fileType.insert(fileType.begin(), '.');
        }
        image = LoadImageFromMemory(
            fileType.empty() ? ".bin" : fileType.c_str(),
            reinterpret_cast<const unsigned char*>(embeddedTexture->pcData),
            static_cast<int>(embeddedTexture->mWidth));
        ownsImage = image.data != nullptr;
        if (!IsImageValid(image)) {
            if (ownsImage) {
                UnloadImage(image);
            }
            image = {};
            ownsImage = false;
            return false;
        }
        return true;
    }

    if (embeddedTexture->mWidth == 0 ||
        embeddedTexture->mWidth > static_cast<unsigned int>(std::numeric_limits<int>::max()) ||
        embeddedTexture->mHeight > static_cast<unsigned int>(std::numeric_limits<int>::max()) ||
        static_cast<size_t>(embeddedTexture->mWidth) >
            std::numeric_limits<size_t>::max() / embeddedTexture->mHeight / 4) {
        return false;
    }

    const size_t pixelCount =
        static_cast<size_t>(embeddedTexture->mWidth) * embeddedTexture->mHeight;
    rawPixels.resize(pixelCount * 4);
    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const aiTexel& source = embeddedTexture->pcData[pixelIndex];
        rawPixels[pixelIndex * 4] = source.r;
        rawPixels[pixelIndex * 4 + 1] = source.g;
        rawPixels[pixelIndex * 4 + 2] = source.b;
        rawPixels[pixelIndex * 4 + 3] = source.a;
    }

    image.data = rawPixels.data();
    image.width = static_cast<int>(embeddedTexture->mWidth);
    image.height = static_cast<int>(embeddedTexture->mHeight);
    image.mipmaps = 1;
    image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    return true;
}

inline void ReleaseEmbeddedAssimpTexture(Image& image, bool ownsImage) {
    if (ownsImage) {
        UnloadImage(image);
    }
    image = {};
}
} // namespace qci

#endif // __QUARK_ASSIMP_TEXTURE__
