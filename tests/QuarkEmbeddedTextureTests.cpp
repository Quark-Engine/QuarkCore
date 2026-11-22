#include <QuarkCore/QuarkCore.hpp>

#include "Renderer/QuarkAssimpTexture.hpp"

#include <assimp/scene.h>

#include <array>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
    bool Check(bool condition, const char* pMessage)
    {
        if (!condition)
        {
            std::cerr << pMessage << '\n';
            return false;
        }
        return true;
    }

    bool DecodeRawEmbeddedTexture()
    {
        aiScene scene{};
        scene.mNumTextures = 1;
        scene.mTextures = new aiTexture*[1]{new aiTexture{}};
        aiTexture& texture = *scene.mTextures[0];
        texture.mWidth = 2;
        texture.mHeight = 1;
        texture.pcData = new aiTexel[2]{};
        texture.pcData[0] = aiTexel{30, 20, 10, 40};
        texture.pcData[1] = aiTexel{70, 60, 50, 80};

        aiString reference("*0");
        Image image{};
        std::vector<unsigned char> vRawPixels;
        bool ownsImage = true;
        if (!Check(qci::DecodeEmbeddedAssimpTexture(scene, reference, image, vRawPixels, ownsImage),
                   "raw embedded texture did not decode"))
        {
            return false;
        }

        if (!Check(!ownsImage, "raw embedded texture unexpectedly claims image ownership") ||
            !Check(image.width == 2 && image.height == 1, "raw embedded dimensions are incorrect") ||
            !Check(image.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, "raw embedded format is incorrect") ||
            !Check(vRawPixels == std::vector<unsigned char>{10, 20, 30, 40, 50, 60, 70, 80},
                   "raw embedded texel channels were copied incorrectly"))
        {
            return false;
        }

        qci::ReleaseEmbeddedAssimpTexture(image, ownsImage);
        return Check(image.data == nullptr && vRawPixels.size() == 8,
                     "releasing a borrowed raw image changed its backing buffer");
    }

    bool DecodeCompressedEmbeddedTexture()
    {
        Image source = GenImageColor(2, 1, Color{12, 34, 56, 255});
        int encodedSize = 0;
        unsigned char* pEncoded = ExportImageToMemory(source, ".png", &encodedSize);
        UnloadImage(source);
        if (!Check(pEncoded != nullptr && encodedSize > 0, "failed to create compressed test image"))
        {
            if (pEncoded)
                MemFree(pEncoded);
            return false;
        }

        aiScene scene{};
        scene.mNumTextures = 1;
        scene.mTextures = new aiTexture*[1]{new aiTexture{}};
        aiTexture& texture = *scene.mTextures[0];
        texture.mWidth = static_cast<unsigned int>(encodedSize);
        texture.mHeight = 0;
        std::memcpy(texture.achFormatHint, "png", 4);
        const size_t texelCount =
            (static_cast<size_t>(encodedSize) + sizeof(aiTexel) - 1) / sizeof(aiTexel);
        texture.pcData = new aiTexel[texelCount]{};
        std::memcpy(texture.pcData, pEncoded, static_cast<size_t>(encodedSize));
        MemFree(pEncoded);

        aiString reference("*0");
        Image image{};
        std::vector<unsigned char> vRawPixels;
        bool ownsImage = false;
        if (!Check(qci::DecodeEmbeddedAssimpTexture(scene, reference, image, vRawPixels, ownsImage),
                   "compressed embedded texture did not decode"))
        {
            return false;
        }

        bool result = Check(ownsImage, "decoded compressed texture ownership was not reported") &&
                      Check(image.width == 2 && image.height == 1, "compressed embedded dimensions are incorrect") &&
                      Check(image.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
                            "compressed embedded image format is incorrect");
        if (result)
        {
            const auto* pPixels = static_cast<const unsigned char*>(image.data);
            result = Check(pPixels[0] == 12 && pPixels[1] == 34 && pPixels[2] == 56 && pPixels[3] == 255,
                           "compressed embedded pixel contents are incorrect");
        }

        qci::ReleaseEmbeddedAssimpTexture(image, ownsImage);
        return result && Check(image.data == nullptr, "owned decoded image was not released");
    }

    bool RejectInvalidEmbeddedReferences()
    {
        aiScene scene{};
        scene.mNumTextures = 1;
        scene.mTextures = new aiTexture*[1]{new aiTexture{}};
        scene.mTextures[0]->mWidth = 1;
        scene.mTextures[0]->mHeight = 1;
        scene.mTextures[0]->pcData = new aiTexel[1]{};

        Image image{};
        std::vector<unsigned char> vRawPixels;
        bool ownsImage = false;
        const std::array<const char*, 4> invalidReferences = {"albedo.png", "*", "*x", "*1"};
        for (const char* text : invalidReferences)
        {
            aiString reference(text);
            if (!Check(!qci::DecodeEmbeddedAssimpTexture(scene, reference, image, vRawPixels, ownsImage),
                       "invalid embedded texture reference was accepted") ||
                !Check(image.data == nullptr, "invalid embedded texture returned image data") ||
                !Check(!ownsImage, "invalid embedded texture claims ownership"))
            {
                return false;
            }
        }
        return true;
    }

    bool RejectMissingTextureArrayAndCompressedData()
    {
        aiScene scene{};
        scene.mNumTextures = 1;
        Image image{};
        std::vector<unsigned char> vRawPixels;
        bool ownsImage = false;
        aiString reference("*0");
        if (!Check(!qci::DecodeEmbeddedAssimpTexture(scene, reference, image, vRawPixels, ownsImage),
                   "missing embedded texture array was accepted"))
        {
            return false;
        }

        scene.mTextures = new aiTexture*[1]{new aiTexture{}};
        aiTexture& texture = *scene.mTextures[0];
        texture.mWidth = 4;
        texture.mHeight = 0;
        std::memcpy(texture.achFormatHint, "png", 4);
        texture.pcData = new aiTexel[1]{};
        return Check(!qci::DecodeEmbeddedAssimpTexture(scene, reference, image, vRawPixels, ownsImage),
                     "invalid compressed embedded image was accepted") &&
               Check(image.data == nullptr && !ownsImage,
                     "invalid compressed image left output in an owned/valid state");
    }
} // anonymous

int main()
{
    if (!DecodeRawEmbeddedTexture())
        return 1;
    if (!DecodeCompressedEmbeddedTexture())
        return 2;
    if (!RejectInvalidEmbeddedReferences())
        return 3;
    if (!RejectMissingTextureArrayAndCompressedData())
        return 4;
    return 0;
}
