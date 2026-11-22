#include "QuarkCore/QuarkCore.hpp"
#include "QuarkInternal.hpp"
#include "Renderer/DefaultFont.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
bool LoadImageFile(const char* pPath, ImageFileData& out, int desiredChannels)
{
    out = {};

    if (pPath == nullptr || pPath[0] == '\0')
    {
        TraceLog(LogLevel::Warn, "IMAGE", "Cannot load image: path is null or empty");
        return false;
    }

    TraceLog(LogLevel::Trace, "IMAGE", TextFormat("Decoding image file: %s (requested channels: %d)", pPath, desiredChannels));

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pDecoded = stbi_load(pPath, &width, &height, &channels, desiredChannels);
    if (pDecoded == nullptr)
    {
        const char* pReason = stbi_failure_reason();
        TraceLog(LogLevel::Error, "IMAGE", TextFormat("Failed to decode image '%s': %s", pPath, pReason ? pReason : "Unknown STB error"));
        return false;
    }

    const int actualChannels = (desiredChannels > 0) ? desiredChannels : channels;
    if (width <= 0 || height <= 0 || actualChannels <= 0)
    {
        TraceLog(LogLevel::Error, "IMAGE", TextFormat("Invalid image dimensions for '%s': %dx%d (%d channels)", pPath, width, height, actualChannels));
        stbi_image_free(pDecoded);
        return false;
    }

    const size_t sizeBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(actualChannels);
    out.m_Width = width;
    out.m_Height = height;
    out.m_Channels = actualChannels;
    out.m_vPixels.resize(sizeBytes);
    std::memcpy(out.m_vPixels.data(), pDecoded, sizeBytes);

    stbi_image_free(pDecoded);

    TraceLog(LogLevel::Info, "IMAGE", TextFormat("Image decoded successfully: %s (%dx%d, %d channels [source: %d], %zu bytes)", pPath, width, height, actualChannels, channels, sizeBytes));
    return true;
}

namespace
{
    bool IsCompressedFormat(int format)
    {
        return format >= PIXELFORMAT_COMPRESSED_DXT1_RGB;
    }

    int PixelSize(int format)
    {
        switch (format)
        {
        case PIXELFORMAT_UNCOMPRESSED_GRAYSCALE:
        case PIXELFORMAT_UNCOMPRESSED_R8:
            return 1;
        case PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:
        case PIXELFORMAT_UNCOMPRESSED_R8G8:
            return 2;
        case PIXELFORMAT_UNCOMPRESSED_R5G6B5:
        case PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:
        case PIXELFORMAT_UNCOMPRESSED_R4G4B4A4:
        case PIXELFORMAT_UNCOMPRESSED_B5G6R5:
        case PIXELFORMAT_UNCOMPRESSED_B5G5R5A1:
        case PIXELFORMAT_UNCOMPRESSED_B4G4R4A4:
        case PIXELFORMAT_UNCOMPRESSED_R16:
            return 2;
        case PIXELFORMAT_UNCOMPRESSED_R8G8B8:
            return 3;
        case PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
        case PIXELFORMAT_UNCOMPRESSED_R8G8B8A8_SRGB:
        case PIXELFORMAT_UNCOMPRESSED_B8G8R8A8:
        case PIXELFORMAT_UNCOMPRESSED_R32:
            return 4;
        case PIXELFORMAT_UNCOMPRESSED_R32G32B32:
            return 12;
        case PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:
            return 16;
        case PIXELFORMAT_UNCOMPRESSED_R16G16B16:
            return 6;
        case PIXELFORMAT_UNCOMPRESSED_R16G16B16A16:
            return 8;
        default:
            return 0;
        }
    }
} // anonymous

QCAPI int GetPixelDataSize(int width, int height, int format)
{
    if (IsCompressedFormat(format))
        return 0;
    return width * height * PixelSize(format);
}

std::uint8_t ClampByte(int value)
{
    return static_cast<std::uint8_t>(std::max(0, std::min(255, value)));
}

QCAPI Color GetPixelColor(const void* pSrcPtr, int format)
{
    const std::uint8_t* pSrc = static_cast<const std::uint8_t*>(pSrcPtr);
    switch (format)
    {
    case PIXELFORMAT_UNCOMPRESSED_GRAYSCALE:
    case PIXELFORMAT_UNCOMPRESSED_R8:
        return Color{pSrc[0], pSrc[0], pSrc[0], 255};
    case PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:
        return Color{pSrc[0], pSrc[0], pSrc[0], pSrc[1]};
    case PIXELFORMAT_UNCOMPRESSED_R8G8:
        return Color{pSrc[0], pSrc[1], 0, 255};
    case PIXELFORMAT_UNCOMPRESSED_R5G6B5:
    {
        std::uint16_t v = static_cast<std::uint16_t>(pSrc[0] | (pSrc[1] << 8));
        return Color{
            ClampByte(((v >> 11) & 0x1F) << 3),
            ClampByte(((v >> 5) & 0x3F) << 2),
            ClampByte((v & 0x1F) << 3), 255};
    }
    case PIXELFORMAT_UNCOMPRESSED_B5G6R5:
    {
        std::uint16_t v = static_cast<std::uint16_t>(pSrc[0] | (pSrc[1] << 8));
        return Color{
            ClampByte(((v >> 11) & 0x1F) << 3),
            ClampByte(((v >> 5) & 0x3F) << 2),
            ClampByte((v & 0x1F) << 3), 255};
    }
    case PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:
    {
        std::uint16_t v = static_cast<std::uint16_t>(pSrc[0] | (pSrc[1] << 8));
        return Color{
            ClampByte(((v >> 11) & 0x1F) << 3),
            ClampByte(((v >> 6) & 0x1F) << 3),
            ClampByte(((v >> 1) & 0x1F) << 3),
            static_cast<std::uint8_t>((v & 1) ? 255 : 0)};
    }
    case PIXELFORMAT_UNCOMPRESSED_B5G5R5A1:
    {
        std::uint16_t v = static_cast<std::uint16_t>(pSrc[0] | (pSrc[1] << 8));
        return Color{
            ClampByte(((v >> 1) & 0x1F) << 3),
            ClampByte(((v >> 6) & 0x1F) << 3),
            ClampByte(((v >> 11) & 0x1F) << 3),
            static_cast<std::uint8_t>((v & 1) ? 255 : 0)};
    }
    case PIXELFORMAT_UNCOMPRESSED_R4G4B4A4:
    {
        std::uint16_t v = static_cast<std::uint16_t>(pSrc[0] | (pSrc[1] << 8));
        return Color{
            ClampByte(((v >> 12) & 0xF) << 4),
            ClampByte(((v >> 8) & 0xF) << 4),
            ClampByte(((v >> 4) & 0xF) << 4),
            ClampByte((v & 0xF) << 4)};
    }
    case PIXELFORMAT_UNCOMPRESSED_B4G4R4A4:
    {
        std::uint16_t v = static_cast<std::uint16_t>(pSrc[0] | (pSrc[1] << 8));
        return Color{
            ClampByte(((v >> 4) & 0xF) << 4),
            ClampByte(((v >> 8) & 0xF) << 4),
            ClampByte(((v >> 12) & 0xF) << 4),
            ClampByte((v & 0xF) << 4)};
    }
    case PIXELFORMAT_UNCOMPRESSED_R8G8B8:
        return Color{pSrc[0], pSrc[1], pSrc[2], 255};
    case PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
    case PIXELFORMAT_UNCOMPRESSED_R8G8B8A8_SRGB:
        return Color{pSrc[0], pSrc[1], pSrc[2], pSrc[3]};
    case PIXELFORMAT_UNCOMPRESSED_B8G8R8A8:
        return Color{pSrc[2], pSrc[1], pSrc[0], pSrc[3]};
    case PIXELFORMAT_UNCOMPRESSED_R32:
    {
        const float v = *reinterpret_cast<const float*>(pSrc);
        std::uint8_t c = ClampByte(static_cast<int>(v * 255.0f));
        return Color{c, c, c, 255};
    }
    case PIXELFORMAT_UNCOMPRESSED_R32G32B32:
    {
        const float* pF = reinterpret_cast<const float*>(pSrc);
        return Color{ClampByte(static_cast<int>(pF[0] * 255.0f)),
                     ClampByte(static_cast<int>(pF[1] * 255.0f)),
                     ClampByte(static_cast<int>(pF[2] * 255.0f)), 255};
    }
    case PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:
    {
        const float* pF = reinterpret_cast<const float*>(pSrc);
        return Color{ClampByte(static_cast<int>(pF[0] * 255.0f)),
                     ClampByte(static_cast<int>(pF[1] * 255.0f)),
                     ClampByte(static_cast<int>(pF[2] * 255.0f)),
                     ClampByte(static_cast<int>(pF[3] * 255.0f))};
    }
    case PIXELFORMAT_UNCOMPRESSED_R16:
    {
        std::uint16_t v = static_cast<std::uint16_t>(pSrc[0] | (pSrc[1] << 8));
        std::uint8_t c = static_cast<std::uint8_t>(v >> 8);
        return Color{c, c, c, 255};
    }
    case PIXELFORMAT_UNCOMPRESSED_R16G16B16:
    {
        std::uint8_t r = static_cast<std::uint8_t>(pSrc[1] >> 0);
        std::uint8_t g = static_cast<std::uint8_t>(pSrc[3] >> 0);
        std::uint8_t b = static_cast<std::uint8_t>(pSrc[5] >> 0);
        return Color{r, g, b, 255};
    }
    case PIXELFORMAT_UNCOMPRESSED_R16G16B16A16:
    {
        std::uint8_t r = static_cast<std::uint8_t>(pSrc[1]);
        std::uint8_t g = static_cast<std::uint8_t>(pSrc[3]);
        std::uint8_t b = static_cast<std::uint8_t>(pSrc[5]);
        std::uint8_t a = static_cast<std::uint8_t>(pSrc[7]);
        return Color{r, g, b, a};
    }
    default:
        return Color{0, 0, 0, 0};
    }
}

QCAPI void SetPixelColor(void* pDstPtr, Color color, int format)
{
    std::uint8_t* pDst = static_cast<std::uint8_t*>(pDstPtr);
    switch (format)
    {
    case PIXELFORMAT_UNCOMPRESSED_GRAYSCALE:
    case PIXELFORMAT_UNCOMPRESSED_R8:
        pDst[0] = color.r;
        break;
    case PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:
        pDst[0] = color.r;
        pDst[1] = color.a;
        break;
    case PIXELFORMAT_UNCOMPRESSED_R8G8:
        pDst[0] = color.r;
        pDst[1] = color.g;
        break;
    case PIXELFORMAT_UNCOMPRESSED_R5G6B5:
    {
        std::uint16_t v = static_cast<std::uint16_t>(
            ((color.r >> 3) << 11) | ((color.g >> 2) << 5) | (color.b >> 3));
        pDst[0] = static_cast<std::uint8_t>(v & 0xFF);
        pDst[1] = static_cast<std::uint8_t>(v >> 8);
        break;
    }
    case PIXELFORMAT_UNCOMPRESSED_B5G6R5:
    {
        std::uint16_t v = static_cast<std::uint16_t>(
            ((color.b >> 3) << 11) | ((color.g >> 2) << 5) | (color.r >> 3));
        pDst[0] = static_cast<std::uint8_t>(v & 0xFF);
        pDst[1] = static_cast<std::uint8_t>(v >> 8);
        break;
    }
    case PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:
    {
        std::uint16_t v = static_cast<std::uint16_t>(
            ((color.r >> 3) << 11) | ((color.g >> 3) << 6) | ((color.b >> 3) << 1) |
            ((color.a > 127) ? 1 : 0));
        pDst[0] = static_cast<std::uint8_t>(v & 0xFF);
        pDst[1] = static_cast<std::uint8_t>(v >> 8);
        break;
    }
    case PIXELFORMAT_UNCOMPRESSED_B5G5R5A1:
    {
        std::uint16_t v = static_cast<std::uint16_t>(
            ((color.b >> 3) << 11) | ((color.g >> 3) << 6) | ((color.r >> 3) << 1) |
            ((color.a > 127) ? 1 : 0));
        pDst[0] = static_cast<std::uint8_t>(v & 0xFF);
        pDst[1] = static_cast<std::uint8_t>(v >> 8);
        break;
    }
    case PIXELFORMAT_UNCOMPRESSED_R4G4B4A4:
    {
        std::uint16_t v = static_cast<std::uint16_t>(
            ((color.r >> 4) << 12) | ((color.g >> 4) << 8) | ((color.b >> 4) << 4) |
            (color.a >> 4));
        pDst[0] = static_cast<std::uint8_t>(v & 0xFF);
        pDst[1] = static_cast<std::uint8_t>(v >> 8);
        break;
    }
    case PIXELFORMAT_UNCOMPRESSED_B4G4R4A4:
    {
        std::uint16_t v = static_cast<std::uint16_t>(
            ((color.b >> 4) << 12) | ((color.g >> 4) << 8) | ((color.r >> 4) << 4) |
            (color.a >> 4));
        pDst[0] = static_cast<std::uint8_t>(v & 0xFF);
        pDst[1] = static_cast<std::uint8_t>(v >> 8);
        break;
    }
    case PIXELFORMAT_UNCOMPRESSED_R8G8B8:
        pDst[0] = color.r;
        pDst[1] = color.g;
        pDst[2] = color.b;
        break;
    case PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
    case PIXELFORMAT_UNCOMPRESSED_R8G8B8A8_SRGB:
        pDst[0] = color.r;
        pDst[1] = color.g;
        pDst[2] = color.b;
        pDst[3] = color.a;
        break;
    case PIXELFORMAT_UNCOMPRESSED_B8G8R8A8:
        pDst[0] = color.b;
        pDst[1] = color.g;
        pDst[2] = color.r;
        pDst[3] = color.a;
        break;
    case PIXELFORMAT_UNCOMPRESSED_R32:
    {
        float* pF = reinterpret_cast<float*>(pDst);
        pF[0] = color.r / 255.0f;
        break;
    }
    case PIXELFORMAT_UNCOMPRESSED_R32G32B32:
    {
        float* pF = reinterpret_cast<float*>(pDst);
        pF[0] = color.r / 255.0f;
        pF[1] = color.g / 255.0f;
        pF[2] = color.b / 255.0f;
        break;
    }
    case PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:
    {
        float* pF = reinterpret_cast<float*>(pDst);
        pF[0] = color.r / 255.0f;
        pF[1] = color.g / 255.0f;
        pF[2] = color.b / 255.0f;
        pF[3] = color.a / 255.0f;
        break;
    }
    case PIXELFORMAT_UNCOMPRESSED_R16:
    {
        pDst[0] = color.r;
        pDst[1] = 0;
        break;
    }
    case PIXELFORMAT_UNCOMPRESSED_R16G16B16:
    {
        pDst[0] = color.r;
        pDst[1] = 0;
        pDst[2] = color.g;
        pDst[3] = 0;
        pDst[4] = color.b;
        pDst[5] = 0;
        break;
    }
    case PIXELFORMAT_UNCOMPRESSED_R16G16B16A16:
    {
        pDst[0] = color.r;
        pDst[1] = 0;
        pDst[2] = color.g;
        pDst[3] = 0;
        pDst[4] = color.b;
        pDst[5] = 0;
        pDst[6] = color.a;
        pDst[7] = 0;
        break;
    }
    default:
        break;
    }
}

namespace
{
    int MipLevelSize(int level)
    {
        return (level <= 0) ? 1 : (level * level);
    }

    size_t MipOffset(int width, int height, int format, int mip, int mipmaps)
    {
        size_t offset = 0;
        for (int i = 0; i < mip && i < mipmaps; ++i)
        {
            int w = std::max(1, width >> i);
            int h = std::max(1, height >> i);
            offset += static_cast<size_t>(GetPixelDataSize(w, h, format));
        }
        return offset;
    }

    bool ConvertImageData(int width, int height, int mipmaps, int oldFormat, int newFormat,
                          const void* pOldData, std::uint8_t* pNewData)
    {
        if (IsCompressedFormat(oldFormat) || IsCompressedFormat(newFormat))
            return false;
        if (oldFormat == newFormat)
        {
            if (pOldData && pNewData && pOldData != pNewData)
            {
                std::memcpy(pNewData, pOldData, static_cast<size_t>(GetPixelDataSize(width, height, oldFormat)) * MipLevelSize(mipmaps));
            }
            return true;
        }

        for (int m = 0; m < mipmaps; ++m)
        {
            int w = std::max(1, width >> m);
            int h = std::max(1, height >> m);
            const std::uint8_t* pSrc = static_cast<const std::uint8_t*>(pOldData) + MipOffset(width, height, oldFormat, m, mipmaps);
            std::uint8_t* pDst = pNewData + MipOffset(width, height, newFormat, m, mipmaps);
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    Color c = GetPixelColor(pSrc + (static_cast<size_t>(y) * w + x) * PixelSize(oldFormat), oldFormat);
                    SetPixelColor(pDst + (static_cast<size_t>(y) * w + x) * PixelSize(newFormat), c, newFormat);
                }
            }
        }
        return true;
    }

    Image CreateImage(int width, int height, int format, size_t byteCount)
    {
        Image img{};
        if (width <= 0 || height <= 0)
            return img;
        img.data = MemAlloc(byteCount);
        if (!img.data)
            return img;
        img.width = width;
        img.height = height;
        img.mipmaps = 1;
        img.format = format;
        return img;
    }

    bool ImageToRGBA8(const Image& image, std::vector<std::uint8_t>& vOut)
    {
        if (!IsImageValid(image) || IsCompressedFormat(image.format))
            return false;
        vOut.resize(static_cast<size_t>(image.width) * image.height * 4);
        const std::uint8_t* pSrc = static_cast<const std::uint8_t*>(image.data);
        for (size_t i = 0; i < vOut.size() / 4; ++i)
        {
            Color c = GetPixelColor(pSrc + i * PixelSize(image.format), image.format);
            vOut[i * 4 + 0] = c.r;
            vOut[i * 4 + 1] = c.g;
            vOut[i * 4 + 2] = c.b;
            vOut[i * 4 + 3] = c.a;
        }
        return true;
    }

    bool EnsureRGBA8(Image* pImage)
    {
        if (!pImage)
            return false;
        if (pImage->format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
            return true;
        if (IsCompressedFormat(pImage->format))
        {
            TraceLog(LogLevel::Warn, "IMAGE", "Operation requires an R8G8B8A8 image, compressed formats are not supported by this operation");
            return false;
        }
        std::vector<std::uint8_t> vRgba;
        if (!ImageToRGBA8(*pImage, vRgba))
            return false;
        MemFree(pImage->data);
        pImage->data = MemAlloc(vRgba.size());
        if (!pImage->data)
            return false;
        std::memcpy(pImage->data, vRgba.data(), vRgba.size());
        pImage->format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        pImage->mipmaps = 1;
        return true;
    }

    void FlipImageRows(std::uint8_t* pData, int width, int height, int bytesPerPixel)
    {
        const size_t rowBytes = static_cast<size_t>(width) * bytesPerPixel;
        std::vector<std::uint8_t> vTmp(rowBytes);
        for (int y = 0; y < height / 2; ++y)
        {
            std::memcpy(vTmp.data(), pData + static_cast<size_t>(y) * rowBytes, rowBytes);
            std::memcpy(pData + static_cast<size_t>(y) * rowBytes, pData + static_cast<size_t>(height - 1 - y) * rowBytes, rowBytes);
            std::memcpy(pData + static_cast<size_t>(height - 1 - y) * rowBytes, vTmp.data(), rowBytes);
        }
    }
} // anonymous

void* MemAlloc(size_t size)
{
    return std::malloc(size ? size : 1);
}

void* MemRealloc(void* pPtr, unsigned int size)
{
    return std::realloc(pPtr, size ? size : 1);
}

void MemFree(void* pPtr)
{
    std::free(pPtr);
}

Image LoadImage(const char* pFileName)
{
    if (pFileName == nullptr || pFileName[0] == '\0')
    {
        TraceLog(LogLevel::Warn, "IMAGE", "Cannot load image: file name is null or empty");
        return Image{};
    }

    int width = 0, height = 0, channels = 0;
    stbi_uc* pDecoded = stbi_load(pFileName, &width, &height, &channels, 4);
    if (pDecoded == nullptr)
    {
        const char* pReason = stbi_failure_reason();
        TraceLog(LogLevel::Error, "IMAGE", TextFormat("Failed to load image '%s': %s", pFileName, pReason ? pReason : "Unknown decoder error"));
        return Image{};
    }
    if (width <= 0 || height <= 0)
    {
        stbi_image_free(pDecoded);
        return Image{};
    }

    const size_t sizeBytes = static_cast<size_t>(width) * height * 4;
    Image img = CreateImage(width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, sizeBytes);
    if (!img.data)
    {
        stbi_image_free(pDecoded);
        return Image{};
    }
    std::memcpy(img.data, pDecoded, sizeBytes);
    stbi_image_free(pDecoded);

    TraceLog(LogLevel::Info, "IMAGE", TextFormat("Image loaded: %s (%dx%d, RGBA8)", pFileName, width, height));
    return img;
}

Image LoadImageRaw(const char* pFileName, int width, int height, int format, int headerSize)
{
    Image img{};
    if (pFileName == nullptr || width <= 0 || height <= 0 || headerSize < 0)
        return img;
    if (IsCompressedFormat(format))
    {
        TraceLog(LogLevel::Warn, "IMAGE", "LoadImageRaw: compressed raw formats are not supported");
        return img;
    }

    std::ifstream file(pFileName, std::ios::binary);
    if (!file)
    {
        TraceLog(LogLevel::Error, "IMAGE", TextFormat("Failed to open raw image file: %s", pFileName));
        return img;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    const size_t dataSize = static_cast<size_t>(width) * height * PixelSize(format);
    if (fileSize < headerSize + static_cast<std::streamoff>(dataSize))
    {
        TraceLog(LogLevel::Error, "IMAGE", TextFormat("Raw image file too small: %s (%lld bytes)", pFileName, static_cast<long long>(fileSize)));
        return img;
    }
    file.seekg(headerSize, std::ios::beg);

    img = CreateImage(width, height, format, dataSize);
    if (!img.data)
        return img;
    file.read(static_cast<char*>(img.data), static_cast<std::streamsize>(dataSize));

    TraceLog(LogLevel::Info, "IMAGE", TextFormat("Raw image loaded: %s (%dx%d, format %d)", pFileName, width, height, format));
    return img;
}

Image LoadImageAnim(const char* pFileName, int* pFrames)
{
    if (pFrames)
        *pFrames = 1;
    Image img = LoadImage(pFileName);
    if (pFrames && IsImageValid(img))
        *pFrames = 1;
    return img;
}

Image LoadImageFromMemory(const char* pFileType, const unsigned char* pFileData, int dataSize)
{
    Image img{};
    if (pFileData == nullptr || dataSize <= 0)
        return img;

    int width = 0, height = 0, channels = 0;
    stbi_uc* pDecoded = stbi_load_from_memory(pFileData, dataSize, &width, &height, &channels, 4);
    if (pDecoded == nullptr)
    {
        const char* pReason = stbi_failure_reason();
        TraceLog(LogLevel::Error, "IMAGE", TextFormat("Failed to decode image from memory (%s): %s", pFileType ? pFileType : "unknown", pReason ? pReason : "Unknown decoder error"));
        return img;
    }

    const size_t sizeBytes = static_cast<size_t>(width) * height * 4;
    img = CreateImage(width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, sizeBytes);
    if (img.data)
        std::memcpy(img.data, pDecoded, sizeBytes);
    stbi_image_free(pDecoded);
    return img;
}

Image LoadImageAnimFromMemory(const char* pFileType, const unsigned char* pFileData, int dataSize, int* pFrames)
{
    if (pFrames)
        *pFrames = 1;
    Image img{};
    if (pFileData == nullptr || dataSize <= 0 || pFileType == nullptr)
        return img;

    if (std::strcmp(pFileType, ".gif") == 0)
    {
        int width = 0, height = 0, channels = 0, framesCount = 0;
        stbi_uc* pDecoded = stbi_load_gif_from_memory(pFileData, dataSize, nullptr, &width, &height, &framesCount, &channels, 4);
        if (pDecoded == nullptr || framesCount <= 0)
        {
            const char* pReason = stbi_failure_reason();
            TraceLog(LogLevel::Error, "IMAGE", TextFormat("Failed to decode GIF animation from memory: %s", pReason ? pReason : "Unknown decoder error"));
            return img;
        }

        const size_t sizeBytes = static_cast<size_t>(width) * height * framesCount * 4;
        img = CreateImage(width, height * framesCount, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, sizeBytes);
        if (img.data)
            std::memcpy(img.data, pDecoded, sizeBytes);
        if (pFrames)
            *pFrames = framesCount;
        stbi_image_free(pDecoded);
        return img;
    }

    return LoadImageFromMemory(pFileType, pFileData, dataSize);
}

bool IsImageValid(Image image)
{
    return image.data != nullptr && image.width > 0 && image.height > 0;
}

namespace
{
    struct WriteContext
    {
        std::vector<unsigned char> m_vBytes;
    };

    void WriteCallback(void* pContext, void* pData, int size)
    {
        WriteContext* pCtx = static_cast<WriteContext*>(pContext);
        const unsigned char* pSrc = static_cast<const unsigned char*>(pData);
        pCtx->m_vBytes.insert(pCtx->m_vBytes.end(), pSrc, pSrc + size);
    }

    const char* ExtensionOf(const char* pFileName)
    {
        if (!pFileName)
            return "";
        const char* pB = std::strrchr(pFileName, '.');
        return pB ? (pB + 1) : "";
    }

    bool ExportToMemory(Image image, const char* pFileType, std::vector<unsigned char>& vOut)
    {
        if (!IsImageValid(image) || IsCompressedFormat(image.format))
            return false;

        std::vector<std::uint8_t> vRgba;
        if (!ImageToRGBA8(image, vRgba))
            return false;

        std::string ext;
        if (pFileType)
        {
            ext = pFileType;
            if (!ext.empty() && ext[0] == '.')
                ext.erase(0, 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
        }

        std::uint8_t* pData8 = vRgba.data();

        if (ext == "png")
        {
            int len = 0;
            unsigned char* pPng = stbi_write_png_to_mem(pData8, image.width * 4, image.width, image.height, 4, &len);
            if (!pPng)
                return false;
            vOut.assign(pPng, pPng + len);
            std::free(pPng);
            return true;
        }

        WriteContext ctx;
        if (ext == "bmp")
        {
            return stbi_write_bmp_to_func(WriteCallback, &ctx, image.width, image.height, 4, pData8) != 0 && (vOut = std::move(ctx.m_vBytes), true);
        }
        if (ext == "tga")
        {
            return stbi_write_tga_to_func(WriteCallback, &ctx, image.width, image.height, 4, pData8) != 0 && (vOut = std::move(ctx.m_vBytes), true);
        }
        if (ext == "jpg" || ext == "jpeg")
        {
            return stbi_write_jpg_to_func(WriteCallback, &ctx, image.width, image.height, 4, pData8, 90) != 0 && (vOut = std::move(ctx.m_vBytes), true);
        }

        TraceLog(LogLevel::Warn, "IMAGE", TextFormat("Unsupported image export format: '%s'", pFileType ? pFileType : ""));
        return false;
    }
} // anonymous

bool ExportImage(Image image, const char* pFileName)
{
    if (!IsImageValid(image) || pFileName == nullptr)
        return false;

    std::vector<unsigned char> vBytes;
    if (!ExportToMemory(image, ExtensionOf(pFileName), vBytes))
        return false;

    std::ofstream file(pFileName, std::ios::binary);
    if (!file)
    {
        TraceLog(LogLevel::Error, "IMAGE", TextFormat("Failed to open output file: %s", pFileName));
        return false;
    }
    file.write(reinterpret_cast<const char*>(vBytes.data()), static_cast<std::streamsize>(vBytes.size()));
    TraceLog(LogLevel::Info, "IMAGE", TextFormat("Image exported: %s (%zu bytes)", pFileName, vBytes.size()));
    return true;
}

unsigned char* ExportImageToMemory(Image image, const char* pFileType, int* pFileSize)
{
    if (pFileSize)
        *pFileSize = 0;
    if (!IsImageValid(image))
        return nullptr;

    std::vector<unsigned char> vBytes;
    if (!ExportToMemory(image, pFileType, vBytes))
        return nullptr;

    unsigned char* pResult = static_cast<unsigned char*>(MemAlloc(vBytes.size()));
    if (!pResult)
        return nullptr;
    std::memcpy(pResult, vBytes.data(), vBytes.size());
    if (pFileSize)
        *pFileSize = static_cast<int>(vBytes.size());
    return pResult;
}

bool ExportImageAsCode(Image image, const char* pFileName)
{
    if (!IsImageValid(image) || pFileName == nullptr)
        return false;

    std::vector<std::uint8_t> vRgba;
    if (!ImageToRGBA8(image, vRgba))
        return false;

    std::ofstream file(pFileName);
    if (!file)
        return false;

    file << "/* Generated with QuarkCore ExportImageAsCode */\n\n";
    file << "static const unsigned int imageWidth = " << image.width << ";\n";
    file << "static const unsigned int imageHeight = " << image.height << ";\n";
    file << "static const unsigned char imageData[" << (vRgba.size() / (image.width * image.height)) << " * " << image.width << " * " << image.height << "] = {\n";

    for (size_t i = 0; i < vRgba.size(); i += 12)
    {
        file << "    ";
        for (size_t j = i; j < i + 12 && j < vRgba.size(); ++j)
        {
            file << static_cast<int>(vRgba[j]) << ",";
        }
        file << "\n";
    }
    file << "};\n";
    file.close();
    TraceLog(LogLevel::Info, "IMAGE", TextFormat("Image exported as code: %s", pFileName));
    return true;
}

Image GenImageColor(int width, int height, Color color)
{
    Image img = CreateImage(width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
                            static_cast<size_t>(width) * height * 4);
    if (!img.data)
        return img;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(img.data);
    for (int i = 0; i < width * height; ++i)
    {
        pPx[i * 4 + 0] = color.r;
        pPx[i * 4 + 1] = color.g;
        pPx[i * 4 + 2] = color.b;
        pPx[i * 4 + 3] = color.a;
    }
    return img;
}

Image GenImageGradientLinear(int width, int height, int direction, Color start, Color end)
{
    Image img = GenImageColor(width, height, BLANK);
    if (!img.data)
        return img;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(img.data);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            float t = 0.0f;
            if (direction == 0)
                t = (width > 1) ? static_cast<float>(x) / (width - 1) : 0.0f;
            else if (direction == 1)
                t = (height > 1) ? static_cast<float>(y) / (height - 1) : 0.0f;
            else if (direction == 2)
                t = (width + height > 2) ? static_cast<float>(x + y) / (width + height - 2) : 0.0f;
            else if (direction == 3)
                t = (width + height > 2) ? static_cast<float>((width - 1 - x) + y) / (width + height - 2) : 0.0f;
            Color c{
                ClampByte(static_cast<int>(Lerp(start.r, end.r, Clamp(t, 0.0f, 1.0f)))),
                ClampByte(static_cast<int>(Lerp(start.g, end.g, Clamp(t, 0.0f, 1.0f)))),
                ClampByte(static_cast<int>(Lerp(start.b, end.b, Clamp(t, 0.0f, 1.0f)))),
                ClampByte(static_cast<int>(Lerp(start.a, end.a, Clamp(t, 0.0f, 1.0f))))};
            size_t i = (static_cast<size_t>(y) * width + x) * 4;
            pPx[i] = c.r;
            pPx[i + 1] = c.g;
            pPx[i + 2] = c.b;
            pPx[i + 3] = c.a;
        }
    }
    return img;
}

Image GenImageGradientRadial(int width, int height, float density, Color inner, Color outer)
{
    Image img = GenImageColor(width, height, BLANK);
    if (!img.data)
        return img;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(img.data);
    const float cx = (width - 1) * 0.5f;
    const float cy = (height - 1) * 0.5f;
    const float maxDist = std::sqrt(cx * cx + cy * cy) * (density > 0.0f ? density : 1.0f);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            const float t = Clamp(maxDist > 0.0f ? d / maxDist : 0.0f, 0.0f, 1.0f);
            Color c{
                ClampByte(static_cast<int>(Lerp(inner.r, outer.r, t))),
                ClampByte(static_cast<int>(Lerp(inner.g, outer.g, t))),
                ClampByte(static_cast<int>(Lerp(inner.b, outer.b, t))),
                ClampByte(static_cast<int>(Lerp(inner.a, outer.a, t)))};
            size_t i = (static_cast<size_t>(y) * width + x) * 4;
            pPx[i] = c.r;
            pPx[i + 1] = c.g;
            pPx[i + 2] = c.b;
            pPx[i + 3] = c.a;
        }
    }
    return img;
}

Image GenImageGradientSquare(int width, int height, float density, Color inner, Color outer)
{
    Image img = GenImageColor(width, height, BLANK);
    if (!img.data)
        return img;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(img.data);
    const float cx = (width - 1) * 0.5f;
    const float cy = (height - 1) * 0.5f;
    const float maxDist = std::max(cx, cy) * (density > 0.0f ? density : 1.0f);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const float dx = std::fabs(x - cx);
            const float dy = std::fabs(y - cy);
            const float t = Clamp(maxDist > 0.0f ? std::max(dx, dy) / maxDist : 0.0f, 0.0f, 1.0f);
            Color c{
                ClampByte(static_cast<int>(Lerp(inner.r, outer.r, t))),
                ClampByte(static_cast<int>(Lerp(inner.g, outer.g, t))),
                ClampByte(static_cast<int>(Lerp(inner.b, outer.b, t))),
                ClampByte(static_cast<int>(Lerp(inner.a, outer.a, t)))};
            size_t i = (static_cast<size_t>(y) * width + x) * 4;
            pPx[i] = c.r;
            pPx[i + 1] = c.g;
            pPx[i + 2] = c.b;
            pPx[i + 3] = c.a;
        }
    }
    return img;
}

Image GenImageChecked(int width, int height, int checksX, int checksY, Color col1, Color col2)
{
    Image img = GenImageColor(width, height, BLANK);
    if (!img.data || checksX <= 0 || checksY <= 0)
        return img;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(img.data);
    const int cellW = std::max(1, width / checksX);
    const int cellH = std::max(1, height / checksY);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            Color c = (((x / cellW) + (y / cellH)) % 2 == 0) ? col1 : col2;
            size_t i = (static_cast<size_t>(y) * width + x) * 4;
            pPx[i] = c.r;
            pPx[i + 1] = c.g;
            pPx[i + 2] = c.b;
            pPx[i + 3] = c.a;
        }
    }
    return img;
}

Image GenImageWhiteNoise(int width, int height, float factor)
{
    Image img = GenImageColor(width, height, BLANK);
    if (!img.data)
        return img;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(img.data);
    const int maxValue = std::max(0, std::min(255, static_cast<int>(255.0f * factor)));
    for (int i = 0; i < width * height; ++i)
    {
        std::uint8_t v = static_cast<std::uint8_t>(GetRandomValue(0, maxValue));
        pPx[i * 4 + 0] = v;
        pPx[i * 4 + 1] = v;
        pPx[i * 4 + 2] = v;
        pPx[i * 4 + 3] = 255;
    }
    return img;
}

Image GenImagePerlinNoise(int width, int height, int offsetX, int offsetY, float scale)
{
    Image img = GenImageColor(width, height, BLANK);
    if (!img.data)
        return img;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(img.data);

    static int s_aPerm[512];
    {
        static bool s_Initialized = false;
        if (!s_Initialized)
        {
            int aPermutation[256];
            for (int i = 0; i < 256; ++i)
                aPermutation[i] = i;
            for (int i = 255; i > 0; --i)
            {
                int j = GetRandomValue(0, i);
                std::swap(aPermutation[i], aPermutation[j]);
            }
            for (int i = 0; i < 512; ++i)
                s_aPerm[i] = aPermutation[i & 255];
            s_Initialized = true;
        }
    }

    const auto fade = [](float t)
    {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    };
    const auto lerpV = [](float a, float b, float t)
    {
        return a + t * (b - a);
    };
    const auto grad = [](int hash, float x, float y)
    {
        int h = hash & 3;
        float u = (h < 2) ? x : y;
        float v = (h < 2) ? y : x;
        return ((h & 1) == 0) ? u : -u + (v * 2.0f - 1.0f) * 0.0f;
    };

    const float s = scale > 0.0f ? scale : 1.0f;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const float fx = (offsetX + x) / s;
            const float fy = (offsetY + y) / s;
            const int x0 = static_cast<int>(std::floor(fx)) & 255;
            const int y0 = static_cast<int>(std::floor(fy)) & 255;
            const int x1 = (x0 + 1) & 255;
            const int y1 = (y0 + 1) & 255;
            const float tx = fx - std::floor(fx);
            const float ty = fy - std::floor(fy);
            const float u = fade(tx);
            const float v = fade(ty);

            const float n00 = grad(s_aPerm[s_aPerm[x0] + y0], tx, ty);
            const float n10 = grad(s_aPerm[s_aPerm[x1] + y0], tx - 1.0f, ty);
            const float n01 = grad(s_aPerm[s_aPerm[x0] + y1], tx, ty - 1.0f);
            const float n11 = grad(s_aPerm[s_aPerm[x1] + y1], tx - 1.0f, ty - 1.0f);

            const float nx0 = lerpV(n00, n10, u);
            const float nx1 = lerpV(n01, n11, u);
            const float n = lerpV(nx0, nx1, v) * 0.5f + 0.5f;

            const std::uint8_t c = static_cast<std::uint8_t>(n * 255.0f);
            size_t i = (static_cast<size_t>(y) * width + x) * 4;
            pPx[i] = c;
            pPx[i + 1] = c;
            pPx[i + 2] = c;
            pPx[i + 3] = 255;
        }
    }
    return img;
}

Image GenImageCellular(int width, int height, int tileSize)
{
    Image img = GenImageColor(width, height, BLANK);
    if (!img.data || tileSize <= 0)
        return img;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(img.data);

    const int cols = (width + tileSize - 1) / tileSize;
    const int rows = (height + tileSize - 1) / tileSize;
    std::vector<Vec2> vPoints(static_cast<size_t>(cols + 1) * (rows + 1));
    for (int ty = 0; ty <= rows; ++ty)
    {
        for (int tx = 0; tx <= cols; ++tx)
        {
            float jitterX = static_cast<float>(GetRandomValue(-50, 50)) / 100.0f;
            float jitterY = static_cast<float>(GetRandomValue(-50, 50)) / 100.0f;
            vPoints[static_cast<size_t>(ty) * (cols + 1) + tx] = Vec2{
                (tx + 0.5f + jitterX) * tileSize,
                (ty + 0.5f + jitterY) * tileSize};
        }
    }

    const auto distFn = [](Vec2 a, Vec2 b)
    {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    };

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            int cx = x / tileSize;
            int cy = y / tileSize;
            float minDist = 1e30f;
            for (int ny = std::max(0, cy - 1); ny <= std::min(rows, cy + 1); ++ny)
            {
                for (int nx = std::max(0, cx - 1); nx <= std::min(cols, cx + 1); ++nx)
                {
                    float d = distFn(Vec2{static_cast<float>(x), static_cast<float>(y)},
                                     vPoints[static_cast<size_t>(ny) * (cols + 1) + nx]);
                    if (d < minDist)
                        minDist = d;
                }
            }
            const float t = Clamp(minDist / tileSize, 0.0f, 1.0f);
            std::uint8_t c = static_cast<std::uint8_t>(t * 255.0f);
            size_t i = (static_cast<size_t>(y) * width + x) * 4;
            pPx[i] = c;
            pPx[i + 1] = c;
            pPx[i + 2] = c;
            pPx[i + 3] = 255;
        }
    }
    return img;
}

Image GenImageText(int width, int height, const char* pText)
{
    (void)width;
    (void)height;
    return ImageText(pText ? pText : "", 12, WHITE);
}

namespace
{
    bool RasterizeText(const char* pText, float fontSize, float spacing, Color tint,
                       int& outWidth, int& outHeight, std::vector<std::uint8_t>& vOutRGBA)
    {
        outWidth = 0;
        outHeight = 0;
        vOutRGBA.clear();

        if (pText == nullptr || *pText == '\0')
            return true;

        FT_Library ft = nullptr;
        if (FT_Init_FreeType(&ft) != 0)
            return false;

        FT_Face face = nullptr;
        if (FT_New_Memory_Face(ft, pixel_ttf, static_cast<FT_Long>(pixel_ttf_len), 0, &face) != 0)
        {
            FT_Done_FreeType(ft);
            return false;
        }
        if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(std::max(1.0f, fontSize))) != 0)
        {
            FT_Done_Face(face);
            FT_Done_FreeType(ft);
            return false;
        }

        const int ascender = static_cast<int>(face->size->metrics.ascender) >> 6;
        const int descender = static_cast<int>(face->size->metrics.descender) >> 6;

        const int textLen = static_cast<int>(std::strlen(pText));

        int width = 0;
        for (int i = 0; i < textLen; ++i)
        {
            if (FT_Load_Char(face, static_cast<unsigned char>(pText[i]), 0) != 0)
                continue;
            width += static_cast<int>(face->glyph->advance.x) >> 6;
            if (i < textLen - 1)
                width += static_cast<int>(spacing);
        }
        const int height = std::max(1, ascender - descender + 1);

        if (width <= 0)
        {
            FT_Done_Face(face);
            FT_Done_FreeType(ft);
            return true;
        }

        vOutRGBA.assign(static_cast<size_t>(width) * height * 4, 0);
        outWidth = width;
        outHeight = height;

        int penX = 0;
        for (int i = 0; i < textLen; ++i)
        {
            const unsigned char c = static_cast<unsigned char>(pText[i]);
            if (FT_Load_Char(face, c, FT_LOAD_RENDER) != 0)
                continue;
            FT_GlyphSlot slot = face->glyph;
            const int gw = slot->bitmap.width;
            const int gh = slot->bitmap.rows;
            const int offsetX = penX + slot->bitmap_left;
            const int offsetY = ascender - slot->bitmap_top;
            for (int py = 0; py < gh; ++py)
            {
                const int dy = offsetY + py;
                if (dy < 0 || dy >= height)
                    continue;
                for (int pxx = 0; pxx < gw; ++pxx)
                {
                    const int dx = offsetX + pxx;
                    if (dx < 0 || dx >= width)
                        continue;
                    const std::uint8_t coverage = slot->bitmap.buffer[py * slot->bitmap.pitch + pxx];
                    if (coverage == 0)
                        continue;
                    std::uint8_t* pDst = &vOutRGBA[(static_cast<size_t>(dy) * width + dx) * 4];
                    pDst[0] = tint.r;
                    pDst[1] = tint.g;
                    pDst[2] = tint.b;
                    pDst[3] = static_cast<std::uint8_t>((tint.a * static_cast<int>(coverage) + 127) / 255);
                }
            }
            penX += static_cast<int>(slot->advance.x) >> 6;
            if (i < textLen - 1)
                penX += static_cast<int>(spacing);
        }

        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return true;
    }
} // anonymous

Image ImageText(const char* pText, int fontSize, Color color)
{
    int w = 0, h = 0;
    std::vector<std::uint8_t> vRgba;
    if (!RasterizeText(pText ? pText : "", static_cast<float>(fontSize), 1.0f, color, w, h, vRgba))
    {
        return Image{};
    }
    Image img = CreateImage(w, h, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, vRgba.size());
    if (img.data)
        std::memcpy(img.data, vRgba.data(), vRgba.size());
    return img;
}

Image ImageTextEx(Font font, const char* pText, float fontSize, float spacing, Color tint)
{
    (void)font;
    int w = 0, h = 0;
    std::vector<std::uint8_t> vRgba;
    if (!RasterizeText(pText ? pText : "", fontSize, spacing, tint, w, h, vRgba))
    {
        return Image{};
    }
    Image img = CreateImage(w, h, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, vRgba.size());
    if (img.data)
        std::memcpy(img.data, vRgba.data(), vRgba.size());
    return img;
}

Image ImageCopy(Image image)
{
    if (!IsImageValid(image) || IsCompressedFormat(image.format))
        return Image{};
    (void)PixelSize(image.format);
    const size_t bytes = static_cast<size_t>(GetPixelDataSize(image.width, image.height, image.format)) * MipLevelSize(image.mipmaps);
    Image copy = CreateImage(image.width, image.height, image.format, bytes);
    if (!copy.data)
        return copy;
    copy.mipmaps = image.mipmaps;
    std::memcpy(copy.data, image.data, bytes);
    return copy;
}

Image ImageFromImage(Image image, Rectangle rec)
{
    Image out{};
    if (!IsImageValid(image) || IsCompressedFormat(image.format))
        return out;

    int startX = static_cast<int>(rec.x);
    int startY = static_cast<int>(rec.y);
    int endX = static_cast<int>(rec.x + rec.width);
    int endY = static_cast<int>(rec.y + rec.height);
    if (startX < 0)
        startX = 0;
    if (startY < 0)
        startY = 0;
    if (endX > image.width)
        endX = image.width;
    if (endY > image.height)
        endY = image.height;
    if (endX <= startX || endY <= startY)
        return out;

    const int newW = endX - startX;
    const int newH = endY - startY;
    const size_t bytes = static_cast<size_t>(GetPixelDataSize(newW, newH, image.format));
    out = CreateImage(newW, newH, image.format, bytes);
    if (!out.data)
        return out;

    const int bpp = PixelSize(image.format);
    const std::uint8_t* pSrc = static_cast<const std::uint8_t*>(image.data);
    std::uint8_t* pDst = static_cast<std::uint8_t*>(out.data);
    for (int y = 0; y < newH; ++y)
    {
        std::memcpy(pDst + static_cast<size_t>(y) * newW * bpp,
                    pSrc + (static_cast<size_t>(startY + y) * image.width + startX) * bpp,
                    static_cast<size_t>(newW) * bpp);
    }
    return out;
}

void UnloadImage(Image image)
{
    if (image.data)
        MemFree(image.data);
}

Image ImageFromChannel(Image image, int selectedChannel)
{
    Image out{};
    if (!IsImageValid(image) || IsCompressedFormat(image.format))
        return out;

    Color* pPixels = LoadImageColors(image);
    if (pPixels == nullptr)
        return out;

    if (selectedChannel == -1)
    {
        for (int i = 0; i < image.width * image.height; ++i)
        {
            const int gray = (static_cast<int>(pPixels[i].r) + pPixels[i].g + pPixels[i].b) / 3;
            pPixels[i].r = pPixels[i].g = pPixels[i].b = static_cast<std::uint8_t>(gray);
        }
    }
    else if ((selectedChannel >= 0) && (selectedChannel <= 3))
    {
        for (int i = 0; i < image.width * image.height; ++i)
        {
            switch (selectedChannel)
            {
            case 1:
                pPixels[i].g = pPixels[i].b = pPixels[i].r;
                break;
            case 2:
                pPixels[i].r = pPixels[i].b = pPixels[i].g;
                break;
            case 3:
                pPixels[i].r = pPixels[i].g = pPixels[i].b;
                break;
            }
        }
    }

    out = GenImageColor(image.width, image.height, WHITE);
    ImageFormat(&out, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
    if (!IsImageValid(out))
    {
        UnloadImageColors(pPixels);
        return out;
    }
    for (int y = 0; y < image.height; ++y)
    {
        for (int x = 0; x < image.width; ++x)
        {
            ImageDrawPixel(&out, x, y, pPixels[y * image.width + x]);
        }
    }
    UnloadImageColors(pPixels);
    return out;
}

void ImageFormat(Image* pImage, int newFormat)
{
    if (!pImage || !IsImageValid(*pImage) || pImage->format == newFormat)
        return;
    if (IsCompressedFormat(pImage->format) || IsCompressedFormat(newFormat))
    {
        TraceLog(LogLevel::Warn, "IMAGE", "ImageFormat: compressed formats are not supported");
        return;
    }

    const size_t size = static_cast<size_t>(GetPixelDataSize(pImage->width, pImage->height, newFormat)) * MipLevelSize(pImage->mipmaps);
    std::uint8_t* pNewData = static_cast<std::uint8_t*>(MemAlloc(size));
    if (!pNewData)
        return;

    if (!ConvertImageData(pImage->width, pImage->height, pImage->mipmaps, pImage->format, newFormat,
                          pImage->data, pNewData))
    {
        MemFree(pNewData);
        return;
    }
    MemFree(pImage->data);
    pImage->data = pNewData;
    pImage->format = newFormat;
}

void ImageToPOT(Image* pImage, Color fill)
{
    if (!pImage || !IsImageValid(*pImage))
        return;
    int potW = 1;
    int potH = 1;
    while (potW < pImage->width)
        potW *= 2;
    while (potH < pImage->height)
        potH *= 2;
    if (potW == pImage->width && potH == pImage->height)
        return;
    ImageResizeCanvas(pImage, potW, potH, 0, 0, fill);
}

void ImageCrop(Image* pImage, Rectangle crop)
{
    if (!pImage)
        return;
    Image cropped = ImageFromImage(*pImage, crop);
    if (cropped.data)
    {
        UnloadImage(*pImage);
        *pImage = cropped;
    }
}

void ImageAlphaCrop(Image* pImage, float threshold)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;

    const int bpp = PixelSize(pImage->format);
    const std::uint8_t* pSrc = static_cast<const std::uint8_t*>(pImage->data);
    int minX = pImage->width, minY = pImage->height, maxX = -1, maxY = -1;

    for (int y = 0; y < pImage->height; ++y)
    {
        for (int x = 0; x < pImage->width; ++x)
        {
            Color c = GetPixelColor(pSrc + (static_cast<size_t>(y) * pImage->width + x) * bpp, pImage->format);
            if (c.a / 255.0f > threshold)
            {
                if (x < minX)
                    minX = x;
                if (x > maxX)
                    maxX = x;
                if (y < minY)
                    minY = y;
                if (y > maxY)
                    maxY = y;
            }
        }
    }
    if (maxX < minX || maxY < minY)
        return;
    ImageCrop(pImage, Rectangle{static_cast<float>(minX), static_cast<float>(minY),
                               static_cast<float>(maxX - minX + 1), static_cast<float>(maxY - minY + 1)});
}

void ImageAlphaClear(Image* pImage, Color color, float threshold)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    if (!EnsureRGBA8(pImage))
        return;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(pImage->data);
    for (int i = 0; i < pImage->width * pImage->height; ++i)
    {
        if (pPx[i * 4 + 3] / 255.0f <= threshold)
        {
            pPx[i * 4 + 0] = color.r;
            pPx[i * 4 + 1] = color.g;
            pPx[i * 4 + 2] = color.b;
            pPx[i * 4 + 3] = color.a;
        }
    }
}

void ImageAlphaMask(Image* pImage, Image alphaMask)
{
    if (!pImage || !IsImageValid(*pImage) || !IsImageValid(alphaMask))
        return;
    if (!EnsureRGBA8(pImage))
        return;
    std::vector<std::uint8_t> vMask;
    if (!ImageToRGBA8(alphaMask, vMask))
        return;

    std::uint8_t* pPx = static_cast<std::uint8_t*>(pImage->data);
    for (int y = 0; y < pImage->height; ++y)
    {
        for (int x = 0; x < pImage->width; ++x)
        {
            int mx = std::min(x, std::max(0, alphaMask.width - 1));
            int my = std::min(y, std::max(0, alphaMask.height - 1));
            size_t i = (static_cast<size_t>(y) * pImage->width + x) * 4;
            pPx[i + 3] = vMask[(static_cast<size_t>(my) * alphaMask.width + mx) * 4];
        }
    }
}

void ImageAlphaPremultiply(Image* pImage)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    if (!EnsureRGBA8(pImage))
        return;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(pImage->data);
    for (int i = 0; i < pImage->width * pImage->height; ++i)
    {
        float a = pPx[i * 4 + 3] / 255.0f;
        pPx[i * 4 + 0] = ClampByte(static_cast<int>(pPx[i * 4 + 0] * a));
        pPx[i * 4 + 1] = ClampByte(static_cast<int>(pPx[i * 4 + 1] * a));
        pPx[i * 4 + 2] = ClampByte(static_cast<int>(pPx[i * 4 + 2] * a));
    }
}

void ImageBlurGaussian(Image* pImage, int blurSize)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    if (blurSize <= 0)
        return;
    if (!EnsureRGBA8(pImage))
        return;

    std::vector<std::uint8_t> vSrc(static_cast<size_t>(pImage->width) * pImage->height * 4);
    std::memcpy(vSrc.data(), pImage->data, vSrc.size());
    std::vector<std::uint8_t> vDst(vSrc.size(), 0);

    const int radius = blurSize;
    const int kernelSize = radius * 2 + 1;
    std::vector<float> kernel(static_cast<size_t>(kernelSize) * kernelSize);
    float sigma = std::max(0.5f, static_cast<float>(blurSize));
    float sum = 0.0f;
    for (int ky = -radius; ky <= radius; ++ky)
    {
        for (int kx = -radius; kx <= radius; ++kx)
        {
            float e = std::exp(-(kx * kx + ky * ky) / (2.0f * sigma * sigma));
            kernel[static_cast<size_t>(ky + radius) * kernelSize + (kx + radius)] = e;
            sum += e;
        }
    }
    for (float& k : kernel)
        k /= sum;

    const int w = pImage->width;
    const int h = pImage->height;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            float acc[4] = {0, 0, 0, 0};
            for (int ky = -radius; ky <= radius; ++ky)
            {
                int sy = std::clamp(y + ky, 0, h - 1);
                for (int kx = -radius; kx <= radius; ++kx)
                {
                    int sx = std::clamp(x + kx, 0, w - 1);
                    const std::uint8_t* p = &vSrc[(static_cast<size_t>(sy) * w + sx) * 4];
                    float k = kernel[static_cast<size_t>(ky + radius) * kernelSize + (kx + radius)];
                    for (int ch = 0; ch < 4; ++ch)
                        acc[ch] += p[ch] * k;
                }
            }
            std::uint8_t* pD = &vDst[(static_cast<size_t>(y) * w + x) * 4];
            for (int ch = 0; ch < 4; ++ch)
                pD[ch] = ClampByte(static_cast<int>(acc[ch]));
        }
    }
    std::memcpy(pImage->data, vDst.data(), vDst.size());
}

void ImageKernelConvolution(Image* pImage, const float* pKernel, int kernelSize)
{
    if (!pImage || !IsImageValid(*pImage) || pKernel == nullptr || kernelSize <= 0)
        return;
    if (!EnsureRGBA8(pImage))
        return;

    std::vector<float> vNorm(static_cast<size_t>(kernelSize) * kernelSize);
    float sum = 0.0f;
    for (int i = 0; i < kernelSize * kernelSize; ++i)
        sum += pKernel[i];
    if (sum == 0.0f)
        return;
    for (int i = 0; i < kernelSize * kernelSize; ++i)
        vNorm[static_cast<size_t>(i)] = pKernel[i] / sum;

    const int w = pImage->width;
    const int h = pImage->height;
    const int half = kernelSize / 2;
    std::vector<std::uint8_t> vSrc(static_cast<size_t>(w) * h * 4);
    std::memcpy(vSrc.data(), pImage->data, vSrc.size());
    std::vector<std::uint8_t> vDst(vSrc.size(), 0);

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            float acc[4] = {0, 0, 0, 0};
            for (int ky = -half; ky <= half; ++ky)
            {
                int sy = std::clamp(y + ky, 0, h - 1);
                for (int kx = -half; kx <= half; ++kx)
                {
                    int sx = std::clamp(x + kx, 0, w - 1);
                    const std::uint8_t* p = &vSrc[(static_cast<size_t>(sy) * w + sx) * 4];
                    const float k = vNorm[static_cast<size_t>(ky + half) * kernelSize + (kx + half)];
                    for (int ch = 0; ch < 4; ++ch)
                        acc[ch] += p[ch] * k;
                }
            }
            std::uint8_t* pD = &vDst[(static_cast<size_t>(y) * w + x) * 4];
            for (int ch = 0; ch < 4; ++ch)
                pD[ch] = ClampByte(static_cast<int>(acc[ch]));
        }
    }
    std::memcpy(pImage->data, vDst.data(), vDst.size());
}

void ImageResize(Image* pImage, int newWidth, int newHeight)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    if (newWidth <= 0 || newHeight <= 0)
        return;
    if (pImage->width == newWidth && pImage->height == newHeight)
        return;

    const int bpp = PixelSize(pImage->format);
    const size_t bytes = static_cast<size_t>(newWidth) * newHeight * bpp;
    std::uint8_t* pNewData = static_cast<std::uint8_t*>(MemAlloc(bytes));
    if (!pNewData)
        return;

    const std::uint8_t* pSrc = static_cast<const std::uint8_t*>(pImage->data);
    const int oldW = pImage->width;
    const int oldH = pImage->height;

    for (int y = 0; y < newHeight; ++y)
    {
        for (int x = 0; x < newWidth; ++x)
        {
            const float fx = (oldW > 1) ? (static_cast<float>(x) + 0.5f) * oldW / newWidth - 0.5f : 0.0f;
            const float fy = (oldH > 1) ? (static_cast<float>(y) + 0.5f) * oldH / newHeight - 0.5f : 0.0f;
            const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, oldW - 1);
            const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, oldH - 1);
            const int x1 = std::min(oldW - 1, x0 + 1);
            const int y1 = std::min(oldH - 1, y0 + 1);
            const float tx = std::fabs(fx - x0);
            const float ty = std::fabs(fy - y0);

            Color c00 = GetPixelColor(pSrc + (static_cast<size_t>(y0) * oldW + x0) * bpp, pImage->format);
            Color c10 = GetPixelColor(pSrc + (static_cast<size_t>(y0) * oldW + x1) * bpp, pImage->format);
            Color c01 = GetPixelColor(pSrc + (static_cast<size_t>(y1) * oldW + x0) * bpp, pImage->format);
            Color c11 = GetPixelColor(pSrc + (static_cast<size_t>(y1) * oldW + x1) * bpp, pImage->format);

            Color result{
                ClampByte(static_cast<int>(Lerp(Lerp(c00.r, c10.r, tx), Lerp(c01.r, c11.r, tx), ty))),
                ClampByte(static_cast<int>(Lerp(Lerp(c00.g, c10.g, tx), Lerp(c01.g, c11.g, tx), ty))),
                ClampByte(static_cast<int>(Lerp(Lerp(c00.b, c10.b, tx), Lerp(c01.b, c11.b, tx), ty))),
                ClampByte(static_cast<int>(Lerp(Lerp(c00.a, c10.a, tx), Lerp(c01.a, c11.a, tx), ty)))};
            SetPixelColor(pNewData + (static_cast<size_t>(y) * newWidth + x) * bpp, result, pImage->format);
        }
    }

    MemFree(pImage->data);
    pImage->data = pNewData;
    pImage->width = newWidth;
    pImage->height = newHeight;
}

void ImageResizeNN(Image* pImage, int newWidth, int newHeight)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    if (newWidth <= 0 || newHeight <= 0)
        return;
    if (pImage->width == newWidth && pImage->height == newHeight)
        return;

    const int bpp = PixelSize(pImage->format);
    const size_t bytes = static_cast<size_t>(newWidth) * newHeight * bpp;
    std::uint8_t* pNewData = static_cast<std::uint8_t*>(MemAlloc(bytes));
    if (!pNewData)
        return;

    const std::uint8_t* pSrc = static_cast<const std::uint8_t*>(pImage->data);
    for (int y = 0; y < newHeight; ++y)
    {
        for (int x = 0; x < newWidth; ++x)
        {
            const int sx = std::min(pImage->width - 1, static_cast<int>(static_cast<float>(x) * pImage->width / newWidth));
            const int sy = std::min(pImage->height - 1, static_cast<int>(static_cast<float>(y) * pImage->height / newHeight));
            std::memcpy(pNewData + (static_cast<size_t>(y) * newWidth + x) * bpp,
                        pSrc + (static_cast<size_t>(sy) * pImage->width + sx) * bpp,
                        static_cast<size_t>(bpp));
        }
    }

    MemFree(pImage->data);
    pImage->data = pNewData;
    pImage->width = newWidth;
    pImage->height = newHeight;
}

void ImageResizeCanvas(Image* pImage, int newWidth, int newHeight, int offsetX, int offsetY, Color fill)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    if (newWidth <= 0 || newHeight <= 0)
        return;
    if (pImage->width == newWidth && pImage->height == newHeight && offsetX == 0 && offsetY == 0)
        return;

    const int bpp = PixelSize(pImage->format);
    const size_t bytes = static_cast<size_t>(newWidth) * newHeight * bpp;
    std::uint8_t* pNewData = static_cast<std::uint8_t*>(MemAlloc(bytes));
    if (!pNewData)
        return;

    for (int y = 0; y < newHeight; ++y)
    {
        for (int x = 0; x < newWidth; ++x)
        {
            SetPixelColor(pNewData + (static_cast<size_t>(y) * newWidth + x) * bpp, fill, pImage->format);
        }
    }

    const std::uint8_t* pSrc = static_cast<const std::uint8_t*>(pImage->data);
    for (int y = 0; y < pImage->height; ++y)
    {
        const int dy = y + offsetY;
        if (dy < 0 || dy >= newHeight)
            continue;
        for (int x = 0; x < pImage->width; ++x)
        {
            const int dx = x + offsetX;
            if (dx < 0 || dx >= newWidth)
                continue;
            std::memcpy(pNewData + (static_cast<size_t>(dy) * newWidth + dx) * bpp,
                        pSrc + (static_cast<size_t>(y) * pImage->width + x) * bpp,
                        static_cast<size_t>(bpp));
        }
    }

    MemFree(pImage->data);
    pImage->data = pNewData;
    pImage->width = newWidth;
    pImage->height = newHeight;
}

void ImageMipmaps(Image* pImage)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    if (pImage->mipmaps > 1)
        return;

    int mipCount = 1;
    int w = pImage->width, h = pImage->height;
    while (w > 1 || h > 1)
    {
        w = std::max(1, w >> 1);
        h = std::max(1, h >> 1);
        mipCount++;
    }

    const int bpp = PixelSize(pImage->format);
    size_t totalBytes = 0;
    w = pImage->width;
    h = pImage->height;
    for (int m = 0; m < mipCount; ++m)
    {
        totalBytes += static_cast<size_t>(w) * h * bpp;
        w = std::max(1, w >> 1);
        h = std::max(1, h >> 1);
    }

    std::uint8_t* pMipData = static_cast<std::uint8_t*>(MemAlloc(totalBytes));
    if (!pMipData)
        return;

    const std::uint8_t* pSrc = static_cast<const std::uint8_t*>(pImage->data);
    size_t offset = 0;
    w = pImage->width;
    h = pImage->height;
    {
        std::memcpy(pMipData, pSrc, static_cast<size_t>(w) * h * bpp);
        offset += static_cast<size_t>(w) * h * bpp;
    }

    int prevW = w, prevH = h;
    for (int m = 1; m < mipCount; ++m)
    {
        int newW = std::max(1, prevW >> 1);
        int newH = std::max(1, prevH >> 1);
        for (int y = 0; y < newH; ++y)
        {
            for (int x = 0; x < newW; ++x)
            {
                const int sx = std::min(prevW - 1, x * 2);
                const int sy = std::min(prevH - 1, y * 2);
                Color a = GetPixelColor(pSrc + (static_cast<size_t>(sy) * prevW + sx) * bpp, pImage->format);
                Color b = GetPixelColor(pSrc + (static_cast<size_t>(sy) * prevW + std::min(prevW - 1, sx + 1)) * bpp, pImage->format);
                Color c = GetPixelColor(pSrc + (static_cast<size_t>(std::min(prevH - 1, sy + 1)) * prevW + sx) * bpp, pImage->format);
                Color d = GetPixelColor(pSrc + (static_cast<size_t>(std::min(prevH - 1, sy + 1)) * prevW + std::min(prevW - 1, sx + 1)) * bpp, pImage->format);
                Color avg{
                    static_cast<std::uint8_t>((a.r + b.r + c.r + d.r) / 4),
                    static_cast<std::uint8_t>((a.g + b.g + c.g + d.g) / 4),
                    static_cast<std::uint8_t>((a.b + b.b + c.b + d.b) / 4),
                    static_cast<std::uint8_t>((a.a + b.a + c.a + d.a) / 4)};
                SetPixelColor(pMipData + offset + (static_cast<size_t>(y) * newW + x) * bpp, avg, pImage->format);
            }
        }
        offset += static_cast<size_t>(newW) * newH * bpp;
        prevW = newW;
        prevH = newH;
    }

    MemFree(pImage->data);
    pImage->data = pMipData;
    pImage->mipmaps = mipCount;
}

void ImageDither(Image* pImage, int rBpp, int gBpp, int bBpp, int aBpp)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;

    std::vector<std::uint8_t> vRgba;
    if (!ImageToRGBA8(*pImage, vRgba))
        return;

    const int w = pImage->width;
    const int h = pImage->height;

    int targetFormat = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    if (rBpp == 5 && gBpp == 6 && bBpp == 5 && aBpp == 0)
        targetFormat = PIXELFORMAT_UNCOMPRESSED_R5G6B5;
    else if (rBpp == 5 && gBpp == 5 && bBpp == 5 && aBpp == 1)
        targetFormat = PIXELFORMAT_UNCOMPRESSED_R5G5B5A1;
    else if (rBpp == 4 && gBpp == 4 && bBpp == 4 && aBpp == 4)
        targetFormat = PIXELFORMAT_UNCOMPRESSED_R4G4B4A4;
    else if (rBpp == 0 && gBpp == 0 && bBpp == 0 && aBpp == 8)
        targetFormat = PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA;
    else if (aBpp == 0 && rBpp > 0 && gBpp == 0 && bBpp == 0)
        targetFormat = PIXELFORMAT_UNCOMPRESSED_R8;

    std::vector<float> vR(static_cast<size_t>(w) * h);
    std::vector<float> vG(static_cast<size_t>(w) * h);
    std::vector<float> vB(static_cast<size_t>(w) * h);
    std::vector<float> vA(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i)
    {
        vR[i] = vRgba[i * 4 + 0];
        vG[i] = vRgba[i * 4 + 1];
        vB[i] = vRgba[i * 4 + 2];
        vA[i] = vRgba[i * 4 + 3];
    }

    const auto ditherChannel = [](float* arr, int idx, int bpp, int w, int h)
    {
        if (bpp <= 0)
            return 0;
        const float maxV = static_cast<float>((1 << bpp) - 1);
        float v = arr[idx];
        int quant = static_cast<int>(std::round(v * maxV / 255.0f));
        if (quant > static_cast<int>(maxV))
            quant = static_cast<int>(maxV);
        float newVal = quant * 255.0f / maxV;
        float err = v - newVal;
        int x = idx % w;
        int y = idx / w;
        auto add = [&](int nx, int ny, float wgt)
        {
            if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                return;
            arr[static_cast<size_t>(ny) * w + nx] = std::min(255.0f, std::max(0.0f, arr[static_cast<size_t>(ny) * w + nx] + err * wgt));
        };
        add(x + 1, y, 7.0f / 16.0f);
        add(x - 1, y + 1, 3.0f / 16.0f);
        add(x, y + 1, 5.0f / 16.0f);
        add(x + 1, y + 1, 1.0f / 16.0f);
        return quant;
    };

    const size_t bytes = static_cast<size_t>(GetPixelDataSize(w, h, targetFormat));
    std::uint8_t* pNewData = static_cast<std::uint8_t*>(MemAlloc(bytes));
    if (!pNewData)
        return;

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const int idx = y * w + x;
            int rv = ditherChannel(vR.data(), idx, rBpp, w, h);
            int gv = ditherChannel(vG.data(), idx, gBpp, w, h);
            int bv = ditherChannel(vB.data(), idx, bBpp, w, h);
            int av = ditherChannel(vA.data(), idx, aBpp, w, h);
            Color c{
                static_cast<std::uint8_t>(rBpp > 0 ? rv : 255),
                static_cast<std::uint8_t>(gBpp > 0 ? gv : 255),
                static_cast<std::uint8_t>(bBpp > 0 ? bv : 255),
                static_cast<std::uint8_t>(aBpp > 0 ? av : 255)};
            SetPixelColor(pNewData + (static_cast<size_t>(y) * w + x) * PixelSize(targetFormat), c, targetFormat);
        }
    }

    MemFree(pImage->data);
    pImage->data = pNewData;
    pImage->format = targetFormat;
}

void ImageFlipVertical(Image* pImage)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    FlipImageRows(static_cast<std::uint8_t*>(pImage->data), pImage->width, pImage->height, PixelSize(pImage->format));
}

void ImageFlipHorizontal(Image* pImage)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    const int bpp = PixelSize(pImage->format);
    std::uint8_t* pPx = static_cast<std::uint8_t*>(pImage->data);
    std::vector<std::uint8_t> vTmp(static_cast<size_t>(bpp));
    for (int y = 0; y < pImage->height; ++y)
    {
        for (int x = 0; x < pImage->width / 2; ++x)
        {
            int a = (y * pImage->width + x) * bpp;
            int bx = (y * pImage->width + (pImage->width - 1 - x)) * bpp;
            std::memcpy(vTmp.data(), pPx + a, static_cast<size_t>(bpp));
            std::memcpy(pPx + a, pPx + bx, static_cast<size_t>(bpp));
            std::memcpy(pPx + bx, vTmp.data(), static_cast<size_t>(bpp));
        }
    }
}

void ImageRotate(Image* pImage, int degrees)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    degrees = ((degrees % 360) + 360) % 360;
    if (degrees == 0)
        return;
    if (degrees == 90 || degrees == 270)
        ImageRotateCW(pImage);
    else if (degrees == 180)
        ImageRotateCCW(pImage);
    else
    {
        TraceLog(LogLevel::Warn, "IMAGE", "ImageRotate: only 90, 180 and 270 degree rotations are supported");
    }
}

void ImageRotateCW(Image* pImage)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    const int bpp = PixelSize(pImage->format);
    const int w = pImage->width;
    const int h = pImage->height;
    const size_t bytes = static_cast<size_t>(w) * h * bpp;
    std::uint8_t* pNewData = static_cast<std::uint8_t*>(MemAlloc(bytes));
    if (!pNewData)
        return;

    const std::uint8_t* pSrc = static_cast<const std::uint8_t*>(pImage->data);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            int nx = h - 1 - y;
            int ny = x;
            std::memcpy(pNewData + (static_cast<size_t>(ny) * h + nx) * bpp,
                        pSrc + (static_cast<size_t>(y) * w + x) * bpp, static_cast<size_t>(bpp));
        }
    }
    MemFree(pImage->data);
    pImage->data = pNewData;
    pImage->width = h;
    pImage->height = w;
}

void ImageRotateCCW(Image* pImage)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    const int bpp = PixelSize(pImage->format);
    const int w = pImage->width;
    const int h = pImage->height;
    const size_t bytes = static_cast<size_t>(w) * h * bpp;
    std::uint8_t* pNewData = static_cast<std::uint8_t*>(MemAlloc(bytes));
    if (!pNewData)
        return;

    const std::uint8_t* pSrc = static_cast<const std::uint8_t*>(pImage->data);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            int nx = y;
            int ny = w - 1 - x;
            std::memcpy(pNewData + (static_cast<size_t>(ny) * h + nx) * bpp,
                        pSrc + (static_cast<size_t>(y) * w + x) * bpp, static_cast<size_t>(bpp));
        }
    }
    MemFree(pImage->data);
    pImage->data = pNewData;
    pImage->width = h;
    pImage->height = w;
}

void ImageColorTint(Image* pImage, Color color)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    const int bpp = PixelSize(pImage->format);
    std::uint8_t* pPx = static_cast<std::uint8_t*>(pImage->data);
    const float tr = color.r / 255.0f, tg = color.g / 255.0f, tb = color.b / 255.0f, ta = color.a / 255.0f;
    for (int i = 0; i < pImage->width * pImage->height; ++i)
    {
        Color c = GetPixelColor(pPx + static_cast<size_t>(i) * bpp, pImage->format);
        c.r = ClampByte(static_cast<int>(c.r * tr));
        c.g = ClampByte(static_cast<int>(c.g * tg));
        c.b = ClampByte(static_cast<int>(c.b * tb));
        c.a = ClampByte(static_cast<int>(c.a * ta));
        SetPixelColor(pPx + static_cast<size_t>(i) * bpp, c, pImage->format);
    }
}

void ImageColorInvert(Image* pImage)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    const int bpp = PixelSize(pImage->format);
    std::uint8_t* pPx = static_cast<std::uint8_t*>(pImage->data);
    for (int i = 0; i < pImage->width * pImage->height; ++i)
    {
        Color c = GetPixelColor(pPx + static_cast<size_t>(i) * bpp, pImage->format);
        if (c.a == 0)
            continue;
        c.r = static_cast<std::uint8_t>(255 - c.r);
        c.g = static_cast<std::uint8_t>(255 - c.g);
        c.b = static_cast<std::uint8_t>(255 - c.b);
        SetPixelColor(pPx + static_cast<size_t>(i) * bpp, c, pImage->format);
    }
}

void ImageColorGrayscale(Image* pImage)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    const int bpp = PixelSize(pImage->format);
    std::uint8_t* pPx = static_cast<std::uint8_t*>(pImage->data);
    for (int i = 0; i < pImage->width * pImage->height; ++i)
    {
        Color c = GetPixelColor(pPx + static_cast<size_t>(i) * bpp, pImage->format);
        if (c.a == 0)
            continue;
        int gray = static_cast<int>(0.299f * c.r + 0.587f * c.g + 0.114f * c.b);
        c.r = ClampByte(gray);
        c.g = ClampByte(gray);
        c.b = ClampByte(gray);
        SetPixelColor(pPx + static_cast<size_t>(i) * bpp, c, pImage->format);
    }
}

void ImageColorContrast(Image* pImage, int contrast)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    if (!EnsureRGBA8(pImage))
        return;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(pImage->data);
    const float factor = contrast > 0 ? (259.0f * (contrast + 255)) / (255.0f * (259 - contrast)) : 1.0f;
    for (int i = 0; i < pImage->width * pImage->height; ++i)
    {
        pPx[i * 4 + 0] = ClampByte(static_cast<int>(factor * (pPx[i * 4 + 0] - 128) + 128));
        pPx[i * 4 + 1] = ClampByte(static_cast<int>(factor * (pPx[i * 4 + 1] - 128) + 128));
        pPx[i * 4 + 2] = ClampByte(static_cast<int>(factor * (pPx[i * 4 + 2] - 128) + 128));
    }
}

void ImageColorBrightness(Image* pImage, int brightness)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    if (!EnsureRGBA8(pImage))
        return;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(pImage->data);
    for (int i = 0; i < pImage->width * pImage->height; ++i)
    {
        pPx[i * 4 + 0] = ClampByte(static_cast<int>(pPx[i * 4 + 0]) + brightness);
        pPx[i * 4 + 1] = ClampByte(static_cast<int>(pPx[i * 4 + 1]) + brightness);
        pPx[i * 4 + 2] = ClampByte(static_cast<int>(pPx[i * 4 + 2]) + brightness);
        pPx[i * 4 + 3] = ClampByte(static_cast<int>(pPx[i * 4 + 3]) + brightness);
    }
}

void ImageColorReplace(Image* pImage, Color color, Color replace)
{
    if (!pImage || !IsImageValid(*pImage) || IsCompressedFormat(pImage->format))
        return;
    const int bpp = PixelSize(pImage->format);
    std::uint8_t* pPx = static_cast<std::uint8_t*>(pImage->data);
    for (int i = 0; i < pImage->width * pImage->height; ++i)
    {
        Color c = GetPixelColor(pPx + static_cast<size_t>(i) * bpp, pImage->format);
        if (c.r == color.r && c.g == color.g && c.b == color.b && c.a == color.a)
        {
            SetPixelColor(pPx + static_cast<size_t>(i) * bpp, replace, pImage->format);
        }
    }
}

void ImageClearBackground(Image* pDst, Color color)
{
    if (!pDst || !IsImageValid(*pDst))
        return;
    if (!EnsureRGBA8(pDst))
        return;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(pDst->data);
    for (int i = 0; i < pDst->width * pDst->height; ++i)
    {
        pPx[i * 4 + 0] = color.r;
        pPx[i * 4 + 1] = color.g;
        pPx[i * 4 + 2] = color.b;
        pPx[i * 4 + 3] = color.a;
    }
}

static Color LerpColor(Color a, Color b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return Color{
        static_cast<std::uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<std::uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<std::uint8_t>(a.b + (b.b - a.b) * t),
        static_cast<std::uint8_t>(a.a + (b.a - a.a) * t)};
}

void SetImagePixel(Image* dst, int x, int y, Color color);
void ImageDrawLine(Image* dst, int startPosX, int startPosY, int endPosX, int endPosY, Color color);

void ImageDrawImagePro(Image* pDst, Image src, Rectangle srcRec, Rectangle dstRec, Vec2 origin, float rotation, Color tint)
{
    if (!pDst || !IsImageValid(*pDst) || !IsImageValid(src))
        return;
    if (!EnsureRGBA8(pDst))
        return;

    std::vector<std::uint8_t> vSrcRGBA;
    if (!ImageToRGBA8(src, vSrcRGBA))
        return;

    const int dstW = pDst->width;
    const int dstH = pDst->height;
    std::uint8_t* pDstPx = static_cast<std::uint8_t*>(pDst->data);

    const float srcRecW = (srcRec.width > 0.0f) ? srcRec.width : static_cast<float>(src.width);
    const float srcRecH = (srcRec.height > 0.0f) ? srcRec.height : static_cast<float>(src.height);
    const float dstRecW = (dstRec.width != 0.0f) ? dstRec.width : srcRecW;
    const float dstRecH = (dstRec.height != 0.0f) ? dstRec.height : srcRecH;

    const float scaleX = dstRecW / srcRecW;
    const float scaleY = dstRecH / srcRecH;

    const float cosR = std::cos(rotation * DEG2RAD);
    const float sinR = std::sin(rotation * DEG2RAD);

    const float pivotX = dstRec.x + origin.x;
    const float pivotY = dstRec.y + origin.y;

    struct Pt
    {
        float m_X, m_Y;
    };
    Pt corners[4] = {
        {dstRec.x, dstRec.y},
        {dstRec.x + dstRecW, dstRec.y},
        {dstRec.x, dstRec.y + dstRecH},
        {dstRec.x + dstRecW, dstRec.y + dstRecH}};
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (int i = 0; i < 4; ++i)
    {
        float dx = corners[i].m_X - pivotX;
        float dy = corners[i].m_Y - pivotY;
        float rx = pivotX + dx * cosR - dy * sinR;
        float ry = pivotY + dx * sinR + dy * cosR;
        minX = std::min(minX, rx);
        minY = std::min(minY, ry);
        maxX = std::max(maxX, rx);
        maxY = std::max(maxY, ry);
    }

    const int minXi = static_cast<int>(std::floor(minX));
    const int minYi = static_cast<int>(std::floor(minY));
    const int maxXi = static_cast<int>(std::ceil(maxX));
    const int maxYi = static_cast<int>(std::ceil(maxY));

    for (int py = minYi; py <= maxYi; ++py)
    {
        for (int pxx = minXi; pxx <= maxXi; ++pxx)
        {
            float ox = pxx - pivotX;
            float oy = py - pivotY;
            float lx = ox * cosR + oy * sinR;
            float ly = -ox * sinR + oy * cosR;
            lx = (lx - (dstRec.x - pivotX)) / scaleX;
            ly = (ly - (dstRec.y - pivotY)) / scaleY;
            lx += srcRec.x;
            ly += srcRec.y;
            float sx = std::clamp(lx, 0.0f, static_cast<float>(src.width) - 1.0f);
            float sy = std::clamp(ly, 0.0f, static_cast<float>(src.height) - 1.0f);
            const int ix = std::min(src.width - 1, static_cast<int>(sx));
            const int iy = std::min(src.height - 1, static_cast<int>(sy));
            const std::uint8_t* p = &vSrcRGBA[(static_cast<size_t>(iy) * src.width + ix) * 4];
            float sr = p[0] * tint.r / 255.0f;
            float sg = p[1] * tint.g / 255.0f;
            float sb = p[2] * tint.b / 255.0f;
            float sa = p[3] * tint.a / 255.0f;
            if (sa <= 0.0f)
                continue;
            std::uint8_t* pD = pDstPx + (static_cast<size_t>(std::clamp(py, 0, dstH - 1)) * dstW + std::clamp(pxx, 0, dstW - 1)) * 4;
            if (sa >= 1.0f || pD[3] == 0)
            {
                pD[0] = ClampByte(static_cast<int>(sr));
                pD[1] = ClampByte(static_cast<int>(sg));
                pD[2] = ClampByte(static_cast<int>(sb));
                pD[3] = ClampByte(static_cast<int>(sa));
            }
            else
            {
                float da = pD[3] / 255.0f;
                float oa = sa + da * (1.0f - sa);
                pD[0] = ClampByte(static_cast<int>((sr * sa + pD[0] * da * (1.0f - sa)) / oa));
                pD[1] = ClampByte(static_cast<int>((sg * sa + pD[1] * da * (1.0f - sa)) / oa));
                pD[2] = ClampByte(static_cast<int>((sb * sa + pD[2] * da * (1.0f - sa)) / oa));
                pD[3] = ClampByte(static_cast<int>(oa * 255.0f));
            }
        }
    }
}

void ImageDrawImage(Image* pDst, Image src, int posX, int posY, Color tint)
{
    ImageDrawImagePro(pDst, src, Rectangle{0.0f, 0.0f, static_cast<float>(src.width), static_cast<float>(src.height)},
                      Rectangle{static_cast<float>(posX), static_cast<float>(posY), static_cast<float>(src.width), static_cast<float>(src.height)},
                      Vec2{0.0f, 0.0f}, 0.0f, tint);
}

void ImageDrawImageEx(Image* pDst, Image src, Vec2 position, float rotation, float scale, Color tint)
{
    const float dstW = static_cast<float>(src.width) * scale;
    const float dstH = static_cast<float>(src.height) * scale;
    ImageDrawImagePro(pDst, src, Rectangle{0.0f, 0.0f, static_cast<float>(src.width), static_cast<float>(src.height)},
                      Rectangle{position.x, position.y, dstW, dstH},
                      Vec2{dstW * 0.5f, dstH * 0.5f}, rotation, tint);
}

void ImageDrawImageRec(Image* pDst, Image src, Rectangle srcRec, Vec2 position, Color tint)
{
    Rectangle dstRec{position.x, position.y, srcRec.width, srcRec.height};
    ImageDrawImagePro(pDst, src, srcRec, dstRec, Vec2{0.0f, 0.0f}, 0.0f, tint);
}

void ImageDraw(Image* pDst, Image src, Rectangle srcRec, Rectangle dstRec, Color tint)
{
    ImageDrawImagePro(pDst, src, srcRec, dstRec, Vec2{0.0f, 0.0f}, 0.0f, tint);
}

void ImageDrawRectangle(Image* pDst, int posX, int posY, int width, int height, Color color)
{
    ImageDrawRectangleRec(pDst, Rectangle{static_cast<float>(posX), static_cast<float>(posY), static_cast<float>(width), static_cast<float>(height)}, color);
}

void ImageDrawRectangleV(Image* pDst, Vec2 position, Vec2 size, Color color)
{
    ImageDrawRectangleRec(pDst, Rectangle{position.x, position.y, size.x, size.y}, color);
}

void ImageDrawRectangleRec(Image* pDst, Rectangle rec, Color color)
{
    if (!pDst || !IsImageValid(*pDst))
        return;
    if (!EnsureRGBA8(pDst))
        return;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(pDst->data);
    const int x0 = std::clamp(static_cast<int>(rec.x), 0, pDst->width);
    const int y0 = std::clamp(static_cast<int>(rec.y), 0, pDst->height);
    const int x1 = std::clamp(static_cast<int>(rec.x + rec.width), 0, pDst->width);
    const int y1 = std::clamp(static_cast<int>(rec.y + rec.height), 0, pDst->height);
    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x1; ++x)
        {
            std::uint8_t* pD = pPx + (static_cast<size_t>(y) * pDst->width + x) * 4;
            if (color.a >= 255 || pD[3] == 0)
            {
                pD[0] = color.r;
                pD[1] = color.g;
                pD[2] = color.b;
                pD[3] = color.a;
            }
            else
            {
                float sa = color.a / 255.0f;
                float da = pD[3] / 255.0f;
                float oa = sa + da * (1.0f - sa);
                pD[0] = ClampByte(static_cast<int>((color.r * sa + pD[0] * da * (1.0f - sa)) / oa));
                pD[1] = ClampByte(static_cast<int>((color.g * sa + pD[1] * da * (1.0f - sa)) / oa));
                pD[2] = ClampByte(static_cast<int>((color.b * sa + pD[2] * da * (1.0f - sa)) / oa));
                pD[3] = ClampByte(static_cast<int>(oa * 255.0f));
            }
        }
    }
}

void ImageDrawRectanglePro(Image* pDst, Rectangle rec, Vec2 origin, float rotation, Color color)
{
    if (!pDst || !IsImageValid(*pDst))
        return;
    if (!EnsureRGBA8(pDst))
        return;

    const int dstW = pDst->width;
    const int dstH = pDst->height;

    const float cosR = std::cos(rotation * DEG2RAD);
    const float sinR = std::sin(rotation * DEG2RAD);

    const float pivotX = rec.x + origin.x;
    const float pivotY = rec.y + origin.y;

    struct Pt
    {
        float m_X, m_Y;
    };
    Pt corners[4] = {
        {rec.x, rec.y},
        {rec.x + rec.width, rec.y},
        {rec.x, rec.y + rec.height},
        {rec.x + rec.width, rec.y + rec.height}};
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (int i = 0; i < 4; ++i)
    {
        float dx = corners[i].m_X - pivotX;
        float dy = corners[i].m_Y - pivotY;
        float rx = pivotX + dx * cosR - dy * sinR;
        float ry = pivotY + dx * sinR + dy * cosR;
        minX = std::min(minX, rx);
        minY = std::min(minY, ry);
        maxX = std::max(maxX, rx);
        maxY = std::max(maxY, ry);
    }

    for (int py = static_cast<int>(std::floor(minY)); py <= static_cast<int>(std::ceil(maxY)); ++py)
    {
        for (int px = static_cast<int>(std::floor(minX)); px <= static_cast<int>(std::ceil(maxX)); ++px)
        {
            if (px < 0 || py < 0 || px >= dstW || py >= dstH)
                continue;
            float ox = px - pivotX;
            float oy = py - pivotY;
            float lx = (ox * cosR + oy * sinR) - (rec.x - pivotX);
            float ly = (-ox * sinR + oy * cosR) - (rec.y - pivotY);
            if (lx >= 0.0f && lx < rec.width && ly >= 0.0f && ly < rec.height)
            {
                SetImagePixel(pDst, px, py, color);
            }
        }
    }
}

void ImageDrawRectangleLines(Image* pDst, int posX, int posY, int width, int height, Color color)
{
    ImageDrawLine(pDst, posX, posY, posX + width, posY, color);
    ImageDrawLine(pDst, posX, posY, posX, posY + height, color);
    ImageDrawLine(pDst, posX + width, posY, posX + width, posY + height, color);
    ImageDrawLine(pDst, posX, posY + height, posX + width, posY + height, color);
}

void ImageDrawRectangleLinesEx(Image* pDst, Rectangle rec, int thick, Color color)
{
    if (!pDst || thick <= 0)
        return;
    const int x0 = static_cast<int>(rec.x);
    const int y0 = static_cast<int>(rec.y);
    const int x1 = x0 + static_cast<int>(rec.width);
    const int y1 = y0 + static_cast<int>(rec.height);
    ImageDrawRectangleRec(pDst, Rectangle{static_cast<float>(x0), static_cast<float>(y0), rec.width, static_cast<float>(thick)}, color);
    ImageDrawRectangleRec(pDst, Rectangle{static_cast<float>(x0), static_cast<float>(y1 - thick), rec.width, static_cast<float>(thick)}, color);
    ImageDrawRectangleRec(pDst, Rectangle{static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(thick), rec.height}, color);
    ImageDrawRectangleRec(pDst, Rectangle{static_cast<float>(x1 - thick), static_cast<float>(y0), static_cast<float>(thick), rec.height}, color);
}

void ImageDrawRectangleGradient(Image* pDst, int posX, int posY, int width, int height, Color col1, Color col2, Color col3, Color col4)
{
    ImageDrawRectangleGradientEx(pDst, Rectangle{static_cast<float>(posX), static_cast<float>(posY), static_cast<float>(width), static_cast<float>(height)},
                                 col1, col2, col3, col4);
}

void ImageDrawRectangleGradientEx(Image* pDst, Rectangle rec, Color col1, Color col2, Color col3, Color col4)
{
    if (!pDst || !IsImageValid(*pDst))
        return;
    if (!EnsureRGBA8(pDst))
        return;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(pDst->data);
    const int x0 = std::clamp(static_cast<int>(rec.x), 0, pDst->width);
    const int y0 = std::clamp(static_cast<int>(rec.y), 0, pDst->height);
    const int x1 = std::clamp(static_cast<int>(rec.x + rec.width), 0, pDst->width);
    const int y1 = std::clamp(static_cast<int>(rec.y + rec.height), 0, pDst->height);
    const float spanX = static_cast<float>(std::max(1, x1 - x0));
    const float spanY = static_cast<float>(std::max(1, y1 - y0));
    for (int y = y0; y < y1; ++y)
    {
        const float alphaI = static_cast<float>(y - y0) / spanY;
        const Color topColor = LerpColor(col1, col2, alphaI);
        const Color bottomColor = LerpColor(col4, col3, alphaI);
        for (int x = x0; x < x1; ++x)
        {
            const float alphaJ = static_cast<float>(x - x0) / spanX;
            const Color c = LerpColor(topColor, bottomColor, alphaJ);
            std::uint8_t* pD = pPx + (static_cast<size_t>(y) * pDst->width + x) * 4;
            if (c.a >= 255 || pD[3] == 0)
            {
                pD[0] = c.r;
                pD[1] = c.g;
                pD[2] = c.b;
                pD[3] = c.a;
            }
            else
            {
                float sa = c.a / 255.0f;
                float da = pD[3] / 255.0f;
                float oa = sa + da * (1.0f - sa);
                pD[0] = ClampByte(static_cast<int>((c.r * sa + pD[0] * da * (1.0f - sa)) / oa));
                pD[1] = ClampByte(static_cast<int>((c.g * sa + pD[1] * da * (1.0f - sa)) / oa));
                pD[2] = ClampByte(static_cast<int>((c.b * sa + pD[2] * da * (1.0f - sa)) / oa));
                pD[3] = ClampByte(static_cast<int>(oa * 255.0f));
            }
        }
    }
}

void ImageDrawCircle(Image* pDst, int centerX, int centerY, int radius, Color color)
{
    if (!pDst || !IsImageValid(*pDst) || radius <= 0)
        return;
    if (!EnsureRGBA8(pDst))
        return;
    std::uint8_t* pPx = static_cast<std::uint8_t*>(pDst->data);
    for (int y = centerY - radius; y <= centerY + radius; ++y)
    {
        for (int x = centerX - radius; x <= centerX + radius; ++x)
        {
            const int dx = x - centerX;
            const int dy = y - centerY;
            if (dx * dx + dy * dy <= radius * radius)
            {
                if (x < 0 || y < 0 || x >= pDst->width || y >= pDst->height)
                    continue;
                std::uint8_t* pD = pPx + (static_cast<size_t>(y) * pDst->width + x) * 4;
                if (color.a >= 255 || pD[3] == 0)
                {
                    pD[0] = color.r;
                    pD[1] = color.g;
                    pD[2] = color.b;
                    pD[3] = color.a;
                }
                else
                {
                    float sa = color.a / 255.0f;
                    float da = pD[3] / 255.0f;
                    float oa = sa + da * (1.0f - sa);
                    pD[0] = ClampByte(static_cast<int>((color.r * sa + pD[0] * da * (1.0f - sa)) / oa));
                    pD[1] = ClampByte(static_cast<int>((color.g * sa + pD[1] * da * (1.0f - sa)) / oa));
                    pD[2] = ClampByte(static_cast<int>((color.b * sa + pD[2] * da * (1.0f - sa)) / oa));
                    pD[3] = ClampByte(static_cast<int>(oa * 255.0f));
                }
            }
        }
    }
}

void ImageDrawCircleV(Image* pDst, Vec2 center, int radius, Color color)
{
    ImageDrawCircle(pDst, static_cast<int>(center.x), static_cast<int>(center.y), radius, color);
}

void SetImagePixel(Image* pDst, int x, int y, Color color)
{
    if (!pDst || !IsImageValid(*pDst))
        return;
    if (!EnsureRGBA8(pDst))
        return;
    if (x < 0 || y < 0 || x >= pDst->width || y >= pDst->height)
        return;
    std::uint8_t* pD = static_cast<std::uint8_t*>(pDst->data) + (static_cast<size_t>(y) * pDst->width + x) * 4;
    if (color.a >= 255 || pD[3] == 0)
    {
        pD[0] = color.r;
        pD[1] = color.g;
        pD[2] = color.b;
        pD[3] = color.a;
    }
    else
    {
        float sa = color.a / 255.0f;
        float da = pD[3] / 255.0f;
        float oa = sa + da * (1.0f - sa);
        pD[0] = ClampByte(static_cast<int>((color.r * sa + pD[0] * da * (1.0f - sa)) / oa));
        pD[1] = ClampByte(static_cast<int>((color.g * sa + pD[1] * da * (1.0f - sa)) / oa));
        pD[2] = ClampByte(static_cast<int>((color.b * sa + pD[2] * da * (1.0f - sa)) / oa));
        pD[3] = ClampByte(static_cast<int>(oa * 255.0f));
    }
}

void ImageDrawPixel(Image* pDst, int posX, int posY, Color color)
{
    SetImagePixel(pDst, posX, posY, color);
}

void ImageDrawPixelV(Image* pDst, Vec2 position, Color color)
{
    SetImagePixel(pDst, static_cast<int>(position.x), static_cast<int>(position.y), color);
}

void ImageDrawCircleLines(Image* pDst, int centerX, int centerY, int radius, Color color)
{
    if (!pDst || radius <= 0)
        return;
    int x = radius;
    int y = 0;
    int err = 1 - radius;
    while (x >= y)
    {
        SetImagePixel(pDst, centerX + x, centerY + y, color);
        SetImagePixel(pDst, centerX - x, centerY + y, color);
        SetImagePixel(pDst, centerX + x, centerY - y, color);
        SetImagePixel(pDst, centerX - x, centerY - y, color);
        SetImagePixel(pDst, centerX + y, centerY + x, color);
        SetImagePixel(pDst, centerX - y, centerY + x, color);
        SetImagePixel(pDst, centerX + y, centerY - x, color);
        SetImagePixel(pDst, centerX - y, centerY - x, color);
        y++;
        if (err < 0)
            err += 2 * y + 1;
        else
        {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void ImageDrawCircleLinesV(Image* pDst, Vec2 center, int radius, Color color)
{
    ImageDrawCircleLines(pDst, static_cast<int>(center.x), static_cast<int>(center.y), radius, color);
}

void ImageDrawCircleGradient(Image* pDst, Vec2 center, float radius, Color inner, Color outer)
{
    if (!pDst || radius <= 0)
        return;
    for (int y = static_cast<int>(std::ceil(center.y - radius)); y <= static_cast<int>(std::floor(center.y + radius)); ++y)
    {
        for (int x = static_cast<int>(std::ceil(center.x - radius)); x <= static_cast<int>(std::floor(center.x + radius)); ++x)
        {
            const float dx = static_cast<float>(x) - center.x;
            const float dy = static_cast<float>(y) - center.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= radius)
                SetImagePixel(pDst, x, y, LerpColor(inner, outer, dist / radius));
        }
    }
}

namespace
{
    void DrawThickLine(Image* pDst, int x0, int y0, int x1, int y1, int thick, Color color)
    {
        if (!pDst || thick <= 0)
            return;
        if (thick == 1)
        {
            int x = x0, y = y0;
            int dx = std::abs(x1 - x0);
            int dy = -std::abs(y1 - y0);
            int sx = x0 < x1 ? 1 : -1;
            int sy = y0 < y1 ? 1 : -1;
            int err = dx + dy;
            while (true)
            {
                SetImagePixel(pDst, x, y, color);
                if (x == x1 && y == y1)
                    break;
                int e2 = 2 * err;
                if (e2 >= dy)
                {
                    err += dy;
                    x += sx;
                }
                if (e2 <= dx)
                {
                    err += dx;
                    y += sy;
                }
            }
            return;
        }

        const float ax = static_cast<float>(x0);
        const float ay = static_cast<float>(y0);
        const float bx = static_cast<float>(x1);
        const float by = static_cast<float>(y1);
        float vx = bx - ax;
        float vy = by - ay;
        const float len = std::sqrt(vx * vx + vy * vy);
        if (len < 0.5f)
        {
            ImageDrawCircle(pDst, x0, y0, thick / 2, color);
            return;
        }
        vx /= len;
        vy /= len;
        const float nx = -vy;
        const float ny = vx;
        const float half = thick * 0.5f;

        float minX = std::min(ax, bx) - half;
        float maxX = std::max(ax, bx) + half;
        float minY = std::min(ay, by) - half;
        float maxY = std::max(ay, by) + half;

        for (int py = static_cast<int>(minY); py <= static_cast<int>(maxY); ++py)
        {
            for (int px = static_cast<int>(minX); px <= static_cast<int>(maxX); ++px)
            {
                const float fx = static_cast<float>(px) + 0.5f;
                const float fy = static_cast<float>(py) + 0.5f;
                const float dxv = fx - ax;
                const float dyv = fy - ay;
                const float t = dxv * vx + dyv * vy;
                const float perp = std::fabs(dxv * nx + dyv * ny);
                bool onSegment = (t >= -half) && (t <= len + half) && (perp <= half);
                if (onSegment)
                    SetImagePixel(pDst, px, py, color);
            }
        }
    }
} // anonymous

void ImageDrawLine(Image* pDst, int startPosX, int startPosY, int endPosX, int endPosY, Color color)
{
    DrawThickLine(pDst, startPosX, startPosY, endPosX, endPosY, 1, color);
}

void ImageDrawLineV(Image* pDst, Vec2 start, Vec2 end, Color color)
{
    DrawThickLine(pDst, static_cast<int>(start.x), static_cast<int>(start.y),
                  static_cast<int>(end.x), static_cast<int>(end.y), 1, color);
}

void ImageDrawLineEx(Image* pDst, Vec2 start, Vec2 end, int thick, Color color)
{
    DrawThickLine(pDst, static_cast<int>(start.x), static_cast<int>(start.y),
                  static_cast<int>(end.x), static_cast<int>(end.y), thick, color);
}

void ImageDrawLineStrip(Image* pDst, Vec2* pPoints, int count, Color color)
{
    if (pPoints == nullptr || count < 2)
        return;
    for (int i = 0; i < count - 1; ++i)
        ImageDrawLineV(pDst, pPoints[i], pPoints[i + 1], color);
}

static bool ImagePointInTriangle(Vec2 p, Vec2 v1, Vec2 v2, Vec2 v3)
{
    const float d1 = (p.x - v2.x) * (v1.y - v2.y) - (v1.x - v2.x) * (p.y - v2.y);
    const float d2 = (p.x - v3.x) * (v2.y - v3.y) - (v2.x - v3.x) * (p.y - v3.y);
    const float d3 = (p.x - v1.x) * (v3.y - v1.y) - (v3.x - v1.x) * (p.y - v1.y);
    const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(hasNeg && hasPos);
}

void ImageDrawTriangleGradient(Image* pDst, Vec2 v1, Vec2 v2, Vec2 v3, Color c1, Color c2, Color c3)
{
    if (!pDst || !IsImageValid(*pDst))
        return;
    if (!EnsureRGBA8(pDst))
        return;

    const int minX = static_cast<int>(std::floor(std::min({v1.x, v2.x, v3.x})));
    const int minY = static_cast<int>(std::floor(std::min({v1.y, v2.y, v3.y})));
    const int maxX = static_cast<int>(std::ceil(std::max({v1.x, v2.x, v3.x})));
    const int maxY = static_cast<int>(std::ceil(std::max({v1.y, v2.y, v3.y})));

    const float denom = (v2.y - v3.y) * (v1.x - v3.x) + (v3.x - v2.x) * (v1.y - v3.y);
    if (std::fabs(denom) < 1e-9f)
        return;

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            if (x < 0 || y < 0 || x >= pDst->width || y >= pDst->height)
                continue;
            const Vec2 p{static_cast<float>(x), static_cast<float>(y)};
            const float w1 = ((v2.y - v3.y) * (p.x - v3.x) + (v3.x - v2.x) * (p.y - v3.y)) / denom;
            const float w2 = ((v3.y - v1.y) * (p.x - v3.x) + (v1.x - v3.x) * (p.y - v3.y)) / denom;
            const float w3 = 1.0f - w1 - w2;
            if (ImagePointInTriangle(p, v1, v2, v3))
            {
                Color c{
                    ClampByte(static_cast<int>(c1.r * w1 + c2.r * w2 + c3.r * w3)),
                    ClampByte(static_cast<int>(c1.g * w1 + c2.g * w2 + c3.g * w3)),
                    ClampByte(static_cast<int>(c1.b * w1 + c2.b * w2 + c3.b * w3)),
                    ClampByte(static_cast<int>(c1.a * w1 + c2.a * w2 + c3.a * w3))};
                SetImagePixel(pDst, x, y, c);
            }
        }
    }
}

void ImageDrawTriangle(Image* pDst, Vec2 v1, Vec2 v2, Vec2 v3, Color color)
{
    ImageDrawTriangleGradient(pDst, v1, v2, v3, color, color, color);
}

void ImageDrawTriangleLines(Image* pDst, Vec2 v1, Vec2 v2, Vec2 v3, Color color)
{
    ImageDrawLineV(pDst, v1, v2, color);
    ImageDrawLineV(pDst, v2, v3, color);
    ImageDrawLineV(pDst, v3, v1, color);
}

void ImageDrawTriangleFan(Image* pDst, Vec2* pPoints, int count, Color color)
{
    if (pPoints == nullptr || count < 3)
        return;
    for (int i = 0; i < count - 2; ++i)
    {
        ImageDrawTriangle(pDst, pPoints[0], pPoints[i + 1], pPoints[i + 2], color);
    }
}

void ImageDrawTriangleStrip(Image* pDst, Vec2* pPoints, int count, Color color)
{
    if (pPoints == nullptr || count < 3)
        return;
    for (int i = 0; i < count - 2; ++i)
    {
        if ((i % 2) == 1)
            ImageDrawTriangle(pDst, pPoints[i], pPoints[i + 1], pPoints[i + 2], color);
        else
            ImageDrawTriangle(pDst, pPoints[i + 1], pPoints[i], pPoints[i + 2], color);
    }
}

void ImageDrawText(Image* pDst, const char* pText, int posX, int posY, int fontSize, Color color)
{
    if (!pDst || !pText)
        return;
    int w = 0, h = 0;
    std::vector<std::uint8_t> vRgba;
    if (!RasterizeText(pText, static_cast<float>(fontSize), 1.0f, color, w, h, vRgba))
        return;
    Image img = CreateImage(w, h, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, vRgba.size());
    if (!img.data)
        return;
    std::memcpy(img.data, vRgba.data(), vRgba.size());
    ImageDraw(pDst, img, Rectangle{0, 0, static_cast<float>(w), static_cast<float>(h)},
              Rectangle{static_cast<float>(posX), static_cast<float>(posY), static_cast<float>(w), static_cast<float>(h)}, WHITE);
    UnloadImage(img);
}

void ImageDrawTextEx(Image* pDst, Font font, const char* pText, Vec2 position, float fontSize, float spacing, Color tint)
{
    (void)font;
    if (!pDst || !pText)
        return;
    int w = 0, h = 0;
    std::vector<std::uint8_t> vRgba;
    if (!RasterizeText(pText, fontSize, spacing, tint, w, h, vRgba))
        return;
    Image img = CreateImage(w, h, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, vRgba.size());
    if (!img.data)
        return;
    std::memcpy(img.data, vRgba.data(), vRgba.size());
    ImageDraw(pDst, img, Rectangle{0, 0, static_cast<float>(w), static_cast<float>(h)},
              Rectangle{position.x, position.y, static_cast<float>(w), static_cast<float>(h)}, WHITE);
    UnloadImage(img);
}

void ImageDrawTextPro(Image* pDst, Font font, const char* pText, Vec2 position, Vec2 origin, float rotation, float fontSize, float spacing, Color tint)
{
    (void)font;
    if (!pDst || !pText)
        return;
    int w = 0, h = 0;
    std::vector<std::uint8_t> vRgba;
    if (!RasterizeText(pText, fontSize, spacing, tint, w, h, vRgba))
        return;
    Image img = CreateImage(w, h, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, vRgba.size());
    if (!img.data)
        return;
    std::memcpy(img.data, vRgba.data(), vRgba.size());
    ImageDrawImagePro(pDst, img, Rectangle{0, 0, static_cast<float>(w), static_cast<float>(h)},
                      Rectangle{position.x - origin.x, position.y - origin.y, static_cast<float>(w), static_cast<float>(h)},
                      origin, rotation, WHITE);
    UnloadImage(img);
}

Color* LoadImageColors(Image image)
{
    if (!IsImageValid(image) || IsCompressedFormat(image.format))
        return nullptr;
    const size_t count = static_cast<size_t>(image.width) * image.height;
    Color* pColors = static_cast<Color*>(MemAlloc(count * sizeof(Color)));
    if (!pColors)
        return nullptr;

    const int bpp = PixelSize(image.format);
    const std::uint8_t* pSrc = static_cast<const std::uint8_t*>(image.data);
    for (size_t i = 0; i < count; ++i)
    {
        pColors[i] = GetPixelColor(pSrc + i * bpp, image.format);
    }
    return pColors;
}

void UnloadImageColors(Color* pColors)
{
    if (pColors)
        MemFree(pColors);
}

Color* LoadImagePalette(Image image, int maxPaletteSize, int* pColorCount)
{
    if (pColorCount)
        *pColorCount = 0;
    if (!IsImageValid(image))
        return nullptr;
    if (maxPaletteSize <= 0)
        return nullptr;

    std::vector<std::uint8_t> vRgba;
    if (!ImageToRGBA8(image, vRgba))
        return nullptr;

    std::unordered_map<std::uint32_t, int> histogram;
    for (size_t i = 0; i < vRgba.size() / 4; ++i)
    {
        std::uint32_t key = (static_cast<std::uint32_t>(vRgba[i * 4 + 0]) << 24) |
                            (static_cast<std::uint32_t>(vRgba[i * 4 + 1]) << 16) |
                            (static_cast<std::uint32_t>(vRgba[i * 4 + 2]) << 8) |
                            static_cast<std::uint32_t>(vRgba[i * 4 + 3]);
        histogram[key]++;
    }

    std::vector<std::pair<std::uint32_t, int>> vEntries(histogram.begin(), histogram.end());
    std::sort(vEntries.begin(), vEntries.end(),
              [](const std::pair<std::uint32_t, int>& a, const std::pair<std::uint32_t, int>& b)
              {
                  return a.second > b.second;
              });

    const int count = std::min(maxPaletteSize, static_cast<int>(vEntries.size()));
    Color* pPalette = static_cast<Color*>(MemAlloc(static_cast<size_t>(count) * sizeof(Color)));
    if (!pPalette)
        return nullptr;

    for (int i = 0; i < count; ++i)
    {
        pPalette[i] = Color{
            static_cast<std::uint8_t>((vEntries[static_cast<size_t>(i)].first >> 24) & 0xFF),
            static_cast<std::uint8_t>((vEntries[static_cast<size_t>(i)].first >> 16) & 0xFF),
            static_cast<std::uint8_t>((vEntries[static_cast<size_t>(i)].first >> 8) & 0xFF),
            static_cast<std::uint8_t>(vEntries[static_cast<size_t>(i)].first & 0xFF)};
    }
    if (pColorCount)
        *pColorCount = count;
    return pPalette;
}

void UnloadImagePalette(Color* pColors)
{
    if (pColors)
        MemFree(pColors);
}

Rectangle GetImageAlphaBorder(Image image, float threshold)
{
    Rectangle rec{0, 0, 0, 0};
    if (!IsImageValid(image) || IsCompressedFormat(image.format))
        return rec;

    const int bpp = PixelSize(image.format);
    const std::uint8_t* pSrc = static_cast<const std::uint8_t*>(image.data);
    int minX = image.width, minY = image.height, maxX = -1, maxY = -1;
    for (int y = 0; y < image.height; ++y)
    {
        for (int x = 0; x < image.width; ++x)
        {
            Color c = GetPixelColor(pSrc + (static_cast<size_t>(y) * image.width + x) * bpp, image.format);
            if (c.a / 255.0f > threshold)
            {
                if (x < minX)
                    minX = x;
                if (x > maxX)
                    maxX = x;
                if (y < minY)
                    minY = y;
                if (y > maxY)
                    maxY = y;
            }
        }
    }
    if (maxX < minX || maxY < minY)
        return rec;
    rec.x = static_cast<float>(minX);
    rec.y = static_cast<float>(minY);
    rec.width = static_cast<float>(maxX - minX + 1);
    rec.height = static_cast<float>(maxY - minY + 1);
    return rec;
}

Color GetImageColor(Image image, int x, int y)
{
    if (!IsImageValid(image) || IsCompressedFormat(image.format))
        return Color{0, 0, 0, 0};
    if (x < 0 || y < 0 || x >= image.width || y >= image.height)
        return Color{0, 0, 0, 0};
    const int bpp = PixelSize(image.format);
    const std::uint8_t* pSrc = static_cast<const std::uint8_t*>(image.data);
    return GetPixelColor(pSrc + (static_cast<size_t>(y) * image.width + x) * bpp, image.format);
}
