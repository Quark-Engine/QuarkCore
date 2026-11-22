#include "QuarkVkRenderer.hpp"
#include "../DebugFont.h"
#include "../DefaultFont.h"

#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace qci
{
    namespace
    {
        std::vector<int> DefaultCodepoints()
        {
            std::vector<int> vCps;
            vCps.reserve(96);
            for (int c = 32; c <= 126; ++c)
                vCps.push_back(c);
            return vCps;
        }

        int DecodeUTF8(const char*& p)
        {
            const unsigned char lead = static_cast<unsigned char>(*p);
            int cp = 0, seq = 0;
            if ((lead & 0x80) == 0)
            {
                cp = lead;
                seq = 1;
            }
            else if ((lead & 0xE0) == 0xC0)
            {
                cp = lead & 0x1F;
                seq = 2;
            }
            else if ((lead & 0xF0) == 0xE0)
            {
                cp = lead & 0x0F;
                seq = 3;
            }
            else if ((lead & 0xF8) == 0xF0)
            {
                cp = lead & 0x07;
                seq = 4;
            }
            else
            {
                cp = lead;
                seq = 1;
            }
            for (int k = 1; k < seq && p[k] != '\0'; ++k)
                cp = (cp << 6) | (static_cast<unsigned char>(p[k]) & 0x3F);
            p += seq;
            return cp;
        }
    } // anonymous

    static float NormalizeColorComponent(std::uint8_t value)
    {
        return static_cast<float>(value) / 255.0f;
    }

    bool QuarkVkRenderer::LoadFontInternal(const char* pFilePath, const unsigned char* pFileData, int dataSize,
                                           int pointSize, const int* pCodepoints, int codepointCount, FontData& out)
    {
        if ((!pFilePath && !pFileData) || pointSize <= 0)
        {
            TraceLog(LogLevel::Warn, "FONT", "[Vulkan] Cannot load font: invalid source or point size");
            return false;
        }

        TraceLog(LogLevel::Trace, "FONT", TextFormat("[Vulkan] FreeType initializing font: %s (size: %d pt)", pFilePath ? pFilePath : "<memory>", pointSize));

        FT_Library ft = nullptr;
        if (FT_Init_FreeType(&ft) != 0)
        {
            TraceLog(LogLevel::Error, "FONT", "[Vulkan] Failed to initialize FreeType library");
            return false;
        }

        std::string cleanFilePath = pFilePath ? pFilePath : "";
        bool syntheticItalic = false;
        const std::string italicTag = "|italic";
        if (cleanFilePath.size() > italicTag.size() &&
            cleanFilePath.compare(cleanFilePath.size() - italicTag.size(), italicTag.size(), italicTag) == 0)
        {
            cleanFilePath.erase(cleanFilePath.size() - italicTag.size());
            syntheticItalic = true;
        }

        FT_Face face = nullptr;
        if (pFileData != nullptr)
        {
            if (FT_New_Memory_Face(ft, pFileData, static_cast<FT_Long>(dataSize), 0, &face) != 0)
            {
                TraceLog(LogLevel::Error, "FONT", "[Vulkan] FreeType failed to open font from memory");
                FT_Done_FreeType(ft);
                return false;
            }
        }
        else if (FT_New_Face(ft, cleanFilePath.c_str(), 0, &face) != 0)
        {
            TraceLog(LogLevel::Error, "FONT", TextFormat("[Vulkan] FreeType failed to open font file: %s", pFilePath));
            FT_Done_FreeType(ft);
            return false;
        }

        FT_Select_Charmap(face, FT_ENCODING_UNICODE);
        if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pointSize)) != 0)
        {
            TraceLog(LogLevel::Error, "FONT", TextFormat("[Vulkan] FreeType failed to set pixel size %d for: %s", pointSize, pFilePath ? pFilePath : "<memory>"));
            FT_Done_Face(face);
            FT_Done_FreeType(ft);
            return false;
        }
        if (syntheticItalic)
        {
            FT_Matrix italicMatrix = {0x10000L, 0x0366AL, 0, 0x10000L};
            FT_Set_Transform(face, &italicMatrix, nullptr);
        }

        std::vector<int> vCps;
        const int* pCpsPtr = pCodepoints;
        int cpsCount = codepointCount;
        if (pCpsPtr == nullptr || cpsCount <= 0)
        {
            vCps = DefaultCodepoints();
            pCpsPtr = vCps.data();
            cpsCount = static_cast<int>(vCps.size());
        }

        constexpr int atlasWidth = 1024;
        constexpr int atlasHeight = 1024;
        std::vector<unsigned char> vAtlas(atlasWidth * atlasHeight * 4, 0);

        int penX = 1;
        int penY = 1;
        int rowH = 0;
        int renderedGlyphs = 0;
        out.m_vGlyphs.clear();
        out.m_vGlyphs.reserve(static_cast<size_t>(cpsCount));

        for (int i = 0; i < cpsCount; ++i)
        {
            const int cp = pCpsPtr[i];
            if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0)
            {
                continue;
            }

            FT_GlyphSlot slot = face->glyph;
            const int gw = slot->bitmap.width;
            const int gh = slot->bitmap.rows;

            GlyphData g;
            g.m_Value = cp;
            g.m_AdvanceX = static_cast<float>(slot->advance.x) / 64.f;
            g.m_OffsetX = static_cast<float>(slot->bitmap_left);
            g.m_OffsetY = static_cast<float>(slot->bitmap_top);
            g.m_Width = gw;
            g.m_Height = gh;

            if (gw > 0 && gh > 0)
            {
                unsigned char* pGdata = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(gw) * gh * 4));
                if (!pGdata)
                {
                    FT_Done_Face(face);
                    FT_Done_FreeType(ft);
                    return false;
                }
                for (int row = 0; row < gh; ++row)
                {
                    for (int col = 0; col < gw; ++col)
                    {
                        const unsigned char alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
                        pGdata[(static_cast<size_t>(row) * gw + col) * 4u + 0] = 255;
                        pGdata[(static_cast<size_t>(row) * gw + col) * 4u + 1] = 255;
                        pGdata[(static_cast<size_t>(row) * gw + col) * 4u + 2] = 255;
                        pGdata[(static_cast<size_t>(row) * gw + col) * 4u + 3] = alpha;
                    }
                }
                g.m_Image = Image{pGdata, gw, gh, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            }

            if (penX + gw + 1 > atlasWidth)
            {
                penX = 1;
                penY += rowH + 1;
                rowH = 0;
                if (penY + 1 > atlasHeight)
                {
                    TraceLog(LogLevel::Warn, "FONT", TextFormat("[Vulkan] Font atlas overflow (%dx%d) for font: %s", atlasWidth, atlasHeight, pFilePath ? pFilePath : "<memory>"));
                    FT_Done_Face(face);
                    FT_Done_FreeType(ft);
                    return false;
                }
            }

            for (int row = 0; row < gh; ++row)
            {
                for (int col = 0; col < gw; ++col)
                {
                    const size_t dst = ((penY + row) * atlasWidth + (penX + col)) * 4u;
                    const unsigned char alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
                    vAtlas[dst + 0] = 255;
                    vAtlas[dst + 1] = 255;
                    vAtlas[dst + 2] = 255;
                    vAtlas[dst + 3] = alpha;
                }
            }

            g.m_Rec = Rectangle{static_cast<float>(penX), static_cast<float>(penY),
                              static_cast<float>(gw), static_cast<float>(gh)};
            g.m_Uv = Rectangle{
                static_cast<float>(penX) / atlasWidth,
                static_cast<float>(penY) / atlasHeight,
                static_cast<float>(gw) / atlasWidth,
                static_cast<float>(gh) / atlasHeight};

            penX += gw + 1;
            rowH = std::max(rowH, gh);
            renderedGlyphs++;
            out.m_vGlyphs.push_back(g);
        }

        out.m_AtlasTextureId = m_VkResources.CreateTextureFromRGBA(vAtlas.data(), static_cast<uint32_t>(atlasWidth), static_cast<uint32_t>(atlasHeight));
        out.m_AtlasWidth = atlasWidth;
        out.m_AtlasHeight = atlasHeight;
        out.m_BaseSize = pointSize;
        out.m_GlyphCount = renderedGlyphs;
        out.m_Ascent = static_cast<int>(face->size->metrics.ascender / 64);
        out.m_Descent = static_cast<int>(face->size->metrics.descender / 64);
        out.m_LineHeight = static_cast<int>(face->size->metrics.height / 64);
        out.m_LineGap = out.m_LineHeight - (out.m_Ascent - out.m_Descent);

        const char* pFamily = face->family_name ? face->family_name : "Unknown";
        const char* pStyle = face->style_name ? face->style_name : "Regular";
        TraceLog(LogLevel::Info, "FONT", TextFormat("[Vulkan] Font rasterized: %s (%s %s, %d glyphs, Atlas: %dx%d, Ascent: %d, Descent: %d, LineHeight: %d)", pFilePath ? pFilePath : "<in-memory>", pFamily, pStyle, renderedGlyphs, atlasWidth, atlasHeight, out.m_Ascent, out.m_Descent, out.m_LineHeight));

        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return true;
    }

    int QuarkVkRenderer::FindGlyph(const FontData& fd, int codepoint)
    {
        for (int i = 0; i < static_cast<int>(fd.m_vGlyphs.size()); ++i)
        {
            if (fd.m_vGlyphs[i].m_Value == codepoint)
                return i;
        }
        for (int i = 0; i < static_cast<int>(fd.m_vGlyphs.size()); ++i)
        {
            if (fd.m_vGlyphs[i].m_Value == 63)
                return i;
        }
        return -1;
    }

    uint32_t QuarkVkRenderer::EnsureDefaultFont()
    {
        if (m_DefaultFontId != 0)
        {
            return m_DefaultFontId;
        }

        FontData fd{};
        if (!LoadFontInternal(nullptr, pixel_ttf, static_cast<int>(pixel_ttf_len), 32, nullptr, 0, fd))
        {
            return 0;
        }

        const uint32_t id = m_NextFontId++;
        m_Fonts[id] = std::move(fd);
        m_DefaultFontId = id;
        return id;
    }

    const QuarkVkRenderer::FontData* QuarkVkRenderer::GetFontData(IFont font) const
    {
        if (font.m_Id == 0)
        {
            return nullptr;
        }

        const auto it = m_Fonts.find(font.m_Id);
        return it != m_Fonts.end() ? &it->second : nullptr;
    }

    void QuarkVkRenderer::DrawTextWithFontData(const FontData& fd, const char* pText,
                                               Vec2 position, float fontSize, float spacing, Color tint)
    {
        if (!pText || fd.m_AtlasTextureId == 0)
        {
            return;
        }

        const float scale = fontSize / static_cast<float>(fd.m_BaseSize);
        const float lineHeight = static_cast<float>(fd.m_LineHeight) * scale;
        const float baseline = static_cast<float>(fd.m_Ascent) * scale;
        float x = position.x;
        float y = position.y;
        bool first = true;

        const float r = NormalizeColorComponent(tint.r);
        const float g = NormalizeColorComponent(tint.g);
        const float b = NormalizeColorComponent(tint.b);
        const float a = NormalizeColorComponent(tint.a);

        const VkDescriptorSet atlasDs = m_VkResources.DescriptorSet(fd.m_AtlasTextureId);
        if (atlasDs == VK_NULL_HANDLE)
        {
            return;
        }

        for (const char* c = pText; *c != '\0';)
        {
            if (*c == '\n')
            {
                x = position.x;
                y += lineHeight;
                first = true;
                ++c;
                continue;
            }

            const int cp = DecodeUTF8(c);
            const int idx = FindGlyph(fd, cp);
            if (idx < 0)
            {
                continue;
            }

            const GlyphData& glyph = fd.m_vGlyphs[static_cast<size_t>(idx)];
            if (!first)
            {
                x += spacing;
            }
            first = false;

            const float gx = x + glyph.m_OffsetX * scale;
            const float gy = y + baseline - glyph.m_OffsetY * scale;
            const float gw = static_cast<float>(glyph.m_Width) * scale;
            const float gh = static_cast<float>(glyph.m_Height) * scale;

            if (gw > 0.f && gh > 0.f)
            {
                AppendQuad(atlasDs,
                           gx, gy,
                           gx + gw, gy,
                           gx + gw, gy + gh,
                           gx, gy + gh,
                           r, g, b, a,
                           glyph.m_Uv.x, glyph.m_Uv.y, glyph.m_Uv.x + glyph.m_Uv.width, glyph.m_Uv.y + glyph.m_Uv.height);
            }

            x += glyph.m_AdvanceX * scale;
        }
    }

    Vec2 QuarkVkRenderer::MeasureTextWithFontData(const FontData& fd, const char* pText,
                                                  float fontSize, float spacing) const
    {
        if (!pText)
        {
            return {};
        }

        const float scale = fontSize / static_cast<float>(fd.m_BaseSize);
        const float lineHeight = static_cast<float>(fd.m_LineHeight) * scale;

        float x = 0.f;
        float maxW = 0.f;
        bool first = true;
        int lines = 1;

        for (const char* c = pText; *c != '\0';)
        {
            if (*c == '\n')
            {
                maxW = std::max(maxW, x);
                x = 0.f;
                first = true;
                ++lines;
                ++c;
                continue;
            }

            const int cp = DecodeUTF8(c);
            const int idx = FindGlyph(fd, cp);
            if (idx < 0)
            {
                continue;
            }

            const GlyphData& g = fd.m_vGlyphs[static_cast<size_t>(idx)];
            if (!first)
            {
                x += spacing;
            }
            first = false;
            x += g.m_AdvanceX * scale;
        }

        return Vec2{std::max(maxW, x), lineHeight * static_cast<float>(lines)};
    }

    void QuarkVkRenderer::DrawDebugText(const char* pText, int x, int y, int fontSize, Color color)
    {
        if (!pText || !*pText || fontSize <= 0)
        {
            return;
        }

        static std::unordered_map<int, IFont> s_debugFonts;
        auto it = s_debugFonts.find(fontSize);
        if (it == s_debugFonts.end())
        {
            FontData fontData{};
            if (!LoadFontInternal(nullptr, tahoma_ttf, static_cast<int>(tahoma_ttf_len), fontSize,
                                  nullptr, 0, fontData))
            {
                TraceLog(LogLevel::Error, "FONT", TextFormat("[Vulkan] Failed to load Tahoma debug font at %d px.", fontSize));
                return;
            }

            IFont font{};
            font.m_Id = m_NextFontId++;
            m_Fonts[font.m_Id] = std::move(fontData);
            it = s_debugFonts.emplace(fontSize, font).first;
        }

        const Color shadow = {0, 0, 0, 255};
        const std::array<Vec2, 9> offsets = {
            Vec2{-1.f, -1.f}, Vec2{0.f, -1.f}, Vec2{1.f, -1.f},
            Vec2{-1.f, 0.f}, Vec2{0.f, 0.f}, Vec2{1.f, 0.f},
            Vec2{-1.f, 1.f}, Vec2{0.f, 1.f}, Vec2{1.f, 1.f}};

        m_DrawingDebugOverlay = true;
        for (const Vec2& delta : offsets)
        {
            if (delta.x == 0.f && delta.y == 0.f)
            {
                continue;
            }
            DrawTextEx(it->second, pText, Vec2{static_cast<float>(x) + delta.x, static_cast<float>(y) + delta.y},
                       static_cast<float>(fontSize), 0.f, shadow);
        }

        DrawTextEx(it->second, pText, Vec2{static_cast<float>(x) + 1.f, static_cast<float>(y) + 1.f},
                   static_cast<float>(fontSize), 0.f, shadow);
        DrawTextEx(it->second, pText, Vec2{static_cast<float>(x), static_cast<float>(y)},
                   static_cast<float>(fontSize), 0.f, color);
        m_DrawingDebugOverlay = false;
    }

    void QuarkVkRenderer::DrawText(const char* pText, int x, int y, int fontSize, Color color)
    {
        const uint32_t id = EnsureDefaultFont();
        if (id == 0)
        {
            return;
        }

        DrawTextWithFontData(m_Fonts[id], pText, Vec2{static_cast<float>(x), static_cast<float>(y)},
                             static_cast<float>(fontSize), 0.f, color);
    }

    void QuarkVkRenderer::DrawTextEx(IFont font, const char* pText, Vec2 position, float fontSize, float spacing, Color tint)
    {
        const FontData* pFd = GetFontData(font);
        if (!pFd)
        {
            const uint32_t id = EnsureDefaultFont();
            if (id == 0)
            {
                return;
            }
            pFd = &m_Fonts[id];
        }

        DrawTextWithFontData(*pFd, pText, position, fontSize, spacing, tint);
    }

    Vec2 QuarkVkRenderer::MeasureTextEx(IFont font, const char* pText, float fontSize, float spacing)
    {
        const FontData* pFd = GetFontData(font);
        if (!pFd)
        {
            const uint32_t id = EnsureDefaultFont();
            if (id == 0)
            {
                return {};
            }
            pFd = &m_Fonts[id];
        }

        return MeasureTextWithFontData(*pFd, pText, fontSize, spacing);
    }

    int QuarkVkRenderer::MeasureText(const char* pText, int fontSize)
    {
        const uint32_t id = EnsureDefaultFont();
        if (id == 0)
        {
            return 0;
        }

        return static_cast<int>(std::round(MeasureTextWithFontData(m_Fonts[id], pText, static_cast<float>(fontSize), 0.f).x));
    }

    IFont QuarkVkRenderer::LoadFont(const char* pFilePath, int fontSize,
                                    const int* pCodepoints, int codepointCount)
    {
        if (!pFilePath || fontSize <= 0)
        {
            TraceLog(LogLevel::Info, "FONT", "[Vulkan] Loading default system font...");
            const uint32_t id = EnsureDefaultFont();
            if (id)
                TraceLog(LogLevel::Info, "FONT", TextFormat("[Vulkan] Default font loaded (Font ID: %u)", id));
            return IFont{id};
        }

        TraceLog(LogLevel::Trace, "FONT", TextFormat("[Vulkan] Loading font file: %s (size: %d pt, codepoints: %d)", pFilePath, fontSize, codepointCount));

        FontData fd{};
        if (!LoadFontInternal(pFilePath, nullptr, 0, fontSize, pCodepoints, codepointCount, fd))
        {
            TraceLog(LogLevel::Error, "FONT", TextFormat("[Vulkan] Failed to load font: %s", pFilePath));
            return IFont{};
        }

        const uint32_t id = m_NextFontId++;
        const uint32_t atlasId = fd.m_AtlasTextureId;
        const int baseSize = fd.m_BaseSize;
        m_Fonts[id] = std::move(fd);

        TraceLog(LogLevel::Info, "FONT", TextFormat("[Vulkan] Font loaded successfully: %s (BaseSize: %d, Atlas ID: %u, Font ID: %u)", pFilePath, baseSize, atlasId, id));
        return IFont{id};
    }

    IFont QuarkVkRenderer::LoadFontFromMemory(const char* pFileType, const unsigned char* pFileData, int dataSize,
                                              int fontSize, const int* pCodepoints, int codepointCount)
    {
        if (!pFileData || dataSize <= 0)
        {
            TraceLog(LogLevel::Error, "FONT", "[Vulkan] LoadFontFromMemory: invalid memory buffer");
            return IFont{};
        }

        TraceLog(LogLevel::Trace, "FONT", TextFormat("[Vulkan] Loading font from memory (type: %s, size: %d pt, %d bytes)", pFileType ? pFileType : ".ttf", fontSize, dataSize));

        FontData fd{};
        if (!LoadFontInternal(nullptr, pFileData, dataSize, fontSize, pCodepoints, codepointCount, fd))
        {
            TraceLog(LogLevel::Error, "FONT", "[Vulkan] Failed to load font from memory");
            return IFont{};
        }

        const uint32_t id = m_NextFontId++;
        const uint32_t atlasId = fd.m_AtlasTextureId;
        m_Fonts[id] = std::move(fd);

        TraceLog(LogLevel::Info, "FONT", TextFormat("[Vulkan] Font loaded from memory (BaseSize: %d, Atlas ID: %u, Font ID: %u)", fd.m_BaseSize, atlasId, id));
        return IFont{id};
    }

    void QuarkVkRenderer::UnloadFont(IFont& font)
    {
        if (font.m_Id == 0)
        {
            return;
        }

        const auto it = m_Fonts.find(font.m_Id);
        if (it != m_Fonts.end())
        {
            const uint32_t atlasId = it->second.m_AtlasTextureId;
            if (it->second.m_AtlasTextureId != 0)
            {
                if (m_Device != VK_NULL_HANDLE)
                {
                    vkDeviceWaitIdle(m_Device);
                }
                ReplaceMaterialTextureReferences(it->second.m_AtlasTextureId);
                m_VkResources.DestroyTexture(it->second.m_AtlasTextureId);
            }
            for (GlyphData& g : it->second.m_vGlyphs)
            {
                std::free(g.m_Image.data);
                g.m_Image = Image{};
            }
            if (font.m_Id == m_DefaultFontId)
            {
                m_DefaultFontId = 0;
            }
            m_Fonts.erase(it);
            TraceLog(LogLevel::Info, "FONT", TextFormat("[Vulkan] Font unloaded (Font ID: %u, Atlas ID: %u)", font.m_Id, atlasId));
        }

        font.m_Id = 0;
    }

    void QuarkVkRenderer::FillFont(IFont font, Font& out)
    {
        const FontData* pFd = GetFontData(font);
        if (!pFd)
        {
            const uint32_t id = EnsureDefaultFont();
            pFd = (id != 0) ? GetFontData(IFont{id}) : nullptr;
        }
        if (!pFd)
        {
            out.valid = false;
            return;
        }

        out.baseSize = pFd->m_BaseSize;
        out.glyphCount = pFd->m_GlyphCount;
        out.glyphPadding = 2;
        out.valid = true;
        out._rendererFontId = font.m_Id;
        out.texture = Texture2D{pFd->m_AtlasTextureId, pFd->m_AtlasWidth, pFd->m_AtlasHeight, 1,
                                PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, true};

        delete[] out.recs;
        delete[] out.glyphs;
        out.recs = new Rectangle[pFd->m_GlyphCount];
        out.glyphs = new GlyphInfo[pFd->m_GlyphCount];

        for (int i = 0; i < pFd->m_GlyphCount; ++i)
        {
            const GlyphData& g = pFd->m_vGlyphs[static_cast<size_t>(i)];
            out.recs[i] = g.m_Rec;
            out.glyphs[i].value = g.m_Value;
            out.glyphs[i].offsetX = static_cast<int>(g.m_OffsetX);
            out.glyphs[i].offsetY = static_cast<int>(g.m_OffsetY);
            out.glyphs[i].advanceX = static_cast<int>(g.m_AdvanceX);
            out.glyphs[i].image = g.m_Image;
        }
    }
} // qci
