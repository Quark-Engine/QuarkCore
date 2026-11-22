#include "QuarkGLFont.hpp"

#include "QuarkGLTexture.hpp"
#include "../../QuarkInternal.hpp"
#include "../DefaultFont.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace qci
{
    int QuarkGLFont::DecodeUTF8(const char*& pText)
    {
        const unsigned char lead = static_cast<unsigned char>(*pText);
        int cp = 0;
        int seq = 0;
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

        for (int k = 1; k < seq && pText[k] != '\0'; ++k)
        {
            cp = (cp << 6) | (static_cast<unsigned char>(pText[k]) & 0x3F);
        }

        pText += seq;
        return cp;
    }

    int QuarkGLFont::FindGlyph(const FontData& fd, int codepoint)
    {
        for (int i = 0; i < static_cast<int>(fd.m_vGlyphs.size()); ++i)
        {
            if (fd.m_vGlyphs[i].m_Value == codepoint)
            {
                return i;
            }
        }
        for (int i = 0; i < static_cast<int>(fd.m_vGlyphs.size()); ++i)
        {
            if (fd.m_vGlyphs[i].m_Value == 63)
            {
                return i;
            }
        }
        return -1;
    }

    bool QuarkGLFont::LoadFontInternal(const char* pFilePath, const unsigned char* pFileData, int dataSize,
                                       int pointSize, const int* pCodepoints, int codepointCount, FontData& out)
    {
        FT_Library ft = nullptr;
        if (FT_Init_FreeType(&ft) != 0)
        {
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
                FT_Done_FreeType(ft);
                return false;
            }
        }
        else if (FT_New_Face(ft, cleanFilePath.c_str(), 0, &face) != 0)
        {
            FT_Done_FreeType(ft);
            return false;
        }

        FT_Select_Charmap(face, FT_ENCODING_UNICODE);
        FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pointSize));
        if (syntheticItalic)
        {
            FT_Matrix italicMatrix = {0x10000L, 0x0366AL, 0, 0x10000L};
            FT_Set_Transform(face, &italicMatrix, nullptr);
        }

        std::vector<int> vCps;
        const int* pCps = pCodepoints;
        int cpsCount = codepointCount;
        if (pCps == nullptr || cpsCount <= 0)
        {
            vCps.reserve(96);
            for (int c = 32; c <= 126; ++c)
            {
                vCps.push_back(c);
            }
            pCps = vCps.data();
            cpsCount = static_cast<int>(vCps.size());
        }

        constexpr int atlasW = 1024;
        constexpr int atlasH = 1024;
        std::vector<uint8_t> vAtlas(static_cast<size_t>(atlasW) * atlasH * 4, 0);
        int penX = 1;
        int penY = 1;
        int rowH = 0;
        out.m_vGlyphs.clear();
        out.m_vGlyphs.reserve(static_cast<size_t>(cpsCount));

        for (int i = 0; i < cpsCount; ++i)
        {
            const int cp = pCps[i];
            if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0)
            {
                continue;
            }

            FT_GlyphSlot slot = face->glyph;
            const int gw = static_cast<int>(slot->bitmap.width);
            const int gh = static_cast<int>(slot->bitmap.rows);

            if (penX + gw + 1 > atlasW)
            {
                penX = 1;
                penY += rowH + 1;
                rowH = 0;
            }

            if (penY + gh + 1 > atlasH)
            {
                FT_Done_Face(face);
                FT_Done_FreeType(ft);
                return false;
            }

            for (int row = 0; row < gh; ++row)
            {
                for (int col = 0; col < gw; ++col)
                {
                    const size_t dst = ((penY + row) * atlasW + (penX + col)) * 4;
                    const uint8_t alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
                    vAtlas[dst] = 255;
                    vAtlas[dst + 1] = 255;
                    vAtlas[dst + 2] = 255;
                    vAtlas[dst + 3] = alpha;
                }
            }

            GlyphData g;
            g.m_Value = cp;
            g.m_Uv = Rectangle{static_cast<float>(penX) / atlasW, static_cast<float>(penY) / atlasH,
                             gw > 0 ? static_cast<float>(gw) / atlasW : 0.0f,
                             gh > 0 ? static_cast<float>(gh) / atlasH : 0.0f};
            g.m_Rec = Rectangle{static_cast<float>(penX), static_cast<float>(penY),
                              gw > 0 ? static_cast<float>(gw) : 0.0f,
                              gh > 0 ? static_cast<float>(gh) : 0.0f};
            g.m_AdvanceX = static_cast<float>(slot->advance.x) / 64.0f;
            g.m_OffsetX = static_cast<float>(slot->bitmap_left);
            g.m_OffsetY = static_cast<float>(slot->bitmap_top);
            g.m_Width = gw;
            g.m_Height = gh;

            if (gw > 0 && gh > 0)
            {
                unsigned char* pGlyphData = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(gw) * gh * 4));
                if (pGlyphData)
                {
                    for (int row = 0; row < gh; ++row)
                    {
                        for (int col = 0; col < gw; ++col)
                        {
                            const unsigned char alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
                            const size_t index = (static_cast<size_t>(row) * gw + col) * 4;
                            pGlyphData[index + 0] = 255;
                            pGlyphData[index + 1] = 255;
                            pGlyphData[index + 2] = 255;
                            pGlyphData[index + 3] = alpha;
                        }
                    }
                    g.m_Image = Image{pGlyphData, gw, gh, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                }
            }

            penX += gw + 1;
            rowH = std::max(rowH, gh);
            out.m_vGlyphs.push_back(g);
        }

        out.m_AtlasTexture = QuarkGLTexture::CreateTextureFromRgba(vAtlas.data(), atlasW, atlasH);
        out.m_AtlasWidth = atlasW;
        out.m_AtlasHeight = atlasH;
        out.m_BaseSize = pointSize;
        out.m_GlyphCount = static_cast<int>(out.m_vGlyphs.size());
        out.m_Ascent = static_cast<int>(face->size->metrics.ascender / 64);
        out.m_Descent = static_cast<int>(face->size->metrics.descender / 64);
        out.m_LineHeight = static_cast<int>(face->size->metrics.height / 64);
        out.m_LineGap = out.m_LineHeight - (out.m_Ascent - out.m_Descent);

        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return true;
    }

    uint32_t QuarkGLFont::EnsureDefaultFont()
    {
        if (m_DefaultFontId != 0)
        {
            return m_DefaultFontId;
        }

        FontData fontData{};
        QuarkGLFont font;
        if (!font.LoadFontInternal(nullptr, pixel_ttf, static_cast<int>(pixel_ttf_len), 32, nullptr, 0, fontData))
        {
            return 0;
        }

        m_DefaultFontId = m_NextFontId++;
        m_Fonts[m_DefaultFontId] = std::move(fontData);
        return m_DefaultFontId;
    }

    const FontData* QuarkGLFont::GetFontData(IFont font) const
    {
        const auto it = m_Fonts.find(font.m_Id);
        return it != m_Fonts.end() ? &it->second : nullptr;
    }

    uint32_t QuarkGLFont::AddFont(FontData&& font)
    {
        const uint32_t id = m_NextFontId++;
        m_Fonts[id] = std::move(font);
        return id;
    }

    void QuarkGLFont::RemoveFont(uint32_t fontId)
    {
        auto it = m_Fonts.find(fontId);
        if (it == m_Fonts.end())
        {
            return;
        }

        if (it->second.m_AtlasTexture)
        {
            glDeleteTextures(1, &it->second.m_AtlasTexture);
        }
        for (GlyphData& g : it->second.m_vGlyphs)
        {
            std::free(g.m_Image.data);
            g.m_Image = Image{};
        }
        m_Fonts.erase(it);
        if (fontId == m_DefaultFontId)
        {
            m_DefaultFontId = 0;
        }
    }

    void QuarkGLFont::Clear()
    {
        for (auto& [id, font] : m_Fonts)
        {
            if (font.m_AtlasTexture)
            {
                glDeleteTextures(1, &font.m_AtlasTexture);
            }
            for (GlyphData& g : font.m_vGlyphs)
            {
                std::free(g.m_Image.data);
                g.m_Image = Image{};
            }
        }
        m_Fonts.clear();
        m_DefaultFontId = 0;
        m_NextFontId = 1;
    }

    void QuarkGLFont::DrawTextWithFontData(const FontData& fontData, const char* pText,
                                           Vec2 pos, float fontSize, float spacing, Color tint)
    {
        if (!pText)
        {
            return;
        }

        const float scale = fontSize / static_cast<float>(fontData.m_BaseSize);
        const float lineHeight = static_cast<float>(fontData.m_LineHeight) * scale;
        const float baseline = static_cast<float>(fontData.m_Ascent) * scale;

        float x = pos.x;
        float y = pos.y;
        bool first = true;

        for (const char* pChar = pText; *pChar;)
        {
            if (*pChar == '\n')
            {
                x = pos.x;
                y += lineHeight;
                first = true;
                ++pChar;
                continue;
            }

            const int cp = DecodeUTF8(pChar);
            const int idx = FindGlyph(fontData, cp);
            if (idx < 0)
            {
                continue;
            }

            const GlyphData& g = fontData.m_vGlyphs[static_cast<size_t>(idx)];
            if (!first)
            {
                x += spacing;
            }
            first = false;

            float gx = x + g.m_OffsetX * scale;
            float gy = y + baseline - g.m_OffsetY * scale;
            float gw = static_cast<float>(g.m_Width) * scale;
            float gh = static_cast<float>(g.m_Height) * scale;
            if (gw > 0.0f && gh > 0.0f)
            {
                (void)gx;
                (void)gy;
                (void)gw;
                (void)gh;
                (void)tint;
            }

            x += g.m_AdvanceX * scale;
        }
    }

    Vec2 QuarkGLFont::MeasureTextWithFontData(const FontData& fontData, const char* pText,
                                              float fontSize, float spacing) const
    {
        if (!pText)
        {
            return {};
        }

        const float scale = static_cast<float>(fontSize) / static_cast<float>(fontData.m_BaseSize);
        const float lineHeight = static_cast<float>(fontData.m_LineHeight) * scale;

        float x = 0.0f;
        float maxW = 0.0f;
        bool first = true;
        int lines = 1;

        for (const char* pChar = pText; *pChar;)
        {
            if (*pChar == '\n')
            {
                maxW = std::max(maxW, x);
                x = 0.0f;
                first = true;
                ++lines;
                ++pChar;
                continue;
            }

            const int cp = DecodeUTF8(pChar);
            const int idx = FindGlyph(fontData, cp);
            if (idx < 0)
            {
                continue;
            }

            if (!first)
            {
                x += spacing;
            }
            first = false;
            x += fontData.m_vGlyphs[static_cast<size_t>(idx)].m_AdvanceX * scale;
        }

        return {std::max(maxW, x), lineHeight * static_cast<float>(lines)};
    }
} // qci
