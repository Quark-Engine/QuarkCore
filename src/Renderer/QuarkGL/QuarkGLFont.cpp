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

namespace qc {

int QuarkGLFont::DecodeUTF8(const char*& p) {
    const unsigned char lead = static_cast<unsigned char>(*p);
    int cp = 0;
    int seq = 0;
    if ((lead & 0x80) == 0) {
        cp = lead;
        seq = 1;
    } else if ((lead & 0xE0) == 0xC0) {
        cp = lead & 0x1F;
        seq = 2;
    } else if ((lead & 0xF0) == 0xE0) {
        cp = lead & 0x0F;
        seq = 3;
    } else if ((lead & 0xF8) == 0xF0) {
        cp = lead & 0x07;
        seq = 4;
    } else {
        cp = lead;
        seq = 1;
    }

    for (int k = 1; k < seq && p[k] != '\0'; ++k) {
        cp = (cp << 6) | (static_cast<unsigned char>(p[k]) & 0x3F);
    }

    p += seq;
    return cp;
}

int QuarkGLFont::FindGlyph(const FontData& fd, int codepoint) {
    for (int i = 0; i < static_cast<int>(fd.glyphs.size()); ++i) {
        if (fd.glyphs[i].value == codepoint) return i;
    }
    for (int i = 0; i < static_cast<int>(fd.glyphs.size()); ++i) {
        if (fd.glyphs[i].value == 63) return i;
    }
    return -1;
}

bool QuarkGLFont::LoadFontInternal(const char* filePath, const unsigned char* fileData, int dataSize,
                                  int pointSize, const int* codepoints, int codepointCount, FontData& out) {
    FT_Library ft = nullptr;
    if (FT_Init_FreeType(&ft) != 0) {
        return false;
    }

    FT_Face face = nullptr;
    if (fileData != nullptr) {
        if (FT_New_Memory_Face(ft, fileData, static_cast<FT_Long>(dataSize), 0, &face) != 0) {
            FT_Done_FreeType(ft);
            return false;
        }
    } else if (FT_New_Face(ft, filePath, 0, &face) != 0) {
        FT_Done_FreeType(ft);
        return false;
    }

    FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pointSize));

    std::vector<int> cps;
    const int* cpsPtr = codepoints;
    int cpsCount = codepointCount;
    if (cpsPtr == nullptr || cpsCount <= 0) {
        cps.reserve(96);
        for (int c = 32; c <= 126; ++c) cps.push_back(c);
        cpsPtr = cps.data();
        cpsCount = static_cast<int>(cps.size());
    }

    constexpr int atlasW = 1024;
    constexpr int atlasH = 1024;
    std::vector<uint8_t> atlas(static_cast<size_t>(atlasW) * atlasH * 4, 0);
    int penX = 1;
    int penY = 1;
    int rowH = 0;
    out.glyphs.clear();
    out.glyphs.reserve(static_cast<size_t>(cpsCount));

    for (int i = 0; i < cpsCount; ++i) {
        const int cp = cpsPtr[i];
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) continue;

        FT_GlyphSlot slot = face->glyph;
        const int gw = static_cast<int>(slot->bitmap.width);
        const int gh = static_cast<int>(slot->bitmap.rows);

        if (penX + gw + 1 > atlasW) {
            penX = 1;
            penY += rowH + 1;
            rowH = 0;
        }

        if (penY + gh + 1 > atlasH) {
            FT_Done_Face(face);
            FT_Done_FreeType(ft);
            return false;
        }

        for (int row = 0; row < gh; ++row) {
            for (int col = 0; col < gw; ++col) {
                const size_t dst = ((penY + row) * atlasW + (penX + col)) * 4;
                const uint8_t alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
                atlas[dst] = 255;
                atlas[dst + 1] = 255;
                atlas[dst + 2] = 255;
                atlas[dst + 3] = alpha;
            }
        }

        GlyphData g;
        g.value = cp;
        g.uv = Rectangle{static_cast<float>(penX) / atlasW, static_cast<float>(penY) / atlasH,
                         gw > 0 ? static_cast<float>(gw) / atlasW : 0.0f,
                         gh > 0 ? static_cast<float>(gh) / atlasH : 0.0f};
        g.rec = Rectangle{static_cast<float>(penX), static_cast<float>(penY),
                          gw > 0 ? static_cast<float>(gw) : 0.0f,
                          gh > 0 ? static_cast<float>(gh) : 0.0f};
        g.advanceX = static_cast<float>(slot->advance.x) / 64.0f;
        g.offsetX = static_cast<float>(slot->bitmap_left);
        g.offsetY = static_cast<float>(slot->bitmap_top);
        g.width = gw;
        g.height = gh;

        if (gw > 0 && gh > 0) {
            unsigned char* gdata = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(gw) * gh * 4));
            if (gdata) {
                for (int row = 0; row < gh; ++row) {
                    for (int col = 0; col < gw; ++col) {
                        const unsigned char alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
                        const size_t index = (static_cast<size_t>(row) * gw + col) * 4;
                        gdata[index + 0] = 255;
                        gdata[index + 1] = 255;
                        gdata[index + 2] = 255;
                        gdata[index + 3] = alpha;
                    }
                }
                g.image = Image{gdata, gw, gh, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            }
        }

        penX += gw + 1;
        rowH = std::max(rowH, gh);
        out.glyphs.push_back(g);
    }

    out.atlasTexture = QuarkGLTexture::CreateTextureFromRgba(atlas.data(), atlasW, atlasH);
    out.atlasWidth = atlasW;
    out.atlasHeight = atlasH;
    out.baseSize = pointSize;
    out.glyphCount = static_cast<int>(out.glyphs.size());
    out.ascent = static_cast<int>(face->size->metrics.ascender / 64);
    out.descent = static_cast<int>(face->size->metrics.descender / 64);
    out.lineHeight = static_cast<int>(face->size->metrics.height / 64);
    out.lineGap = out.lineHeight - (out.ascent - out.descent);

    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    return true;
}

uint32_t QuarkGLFont::EnsureDefaultFont() {
    if (m_defaultFontId != 0) return m_defaultFontId;

    if (pixel_ttf == nullptr || pixel_ttf_len == 0) return 0;

    FontData fontData{};
    QuarkGLFont font;
    if (!font.LoadFontInternal(nullptr, pixel_ttf, static_cast<int>(pixel_ttf_len), 32, nullptr, 0, fontData)) return 0;

    m_defaultFontId = m_nextFontId++;
    m_fonts[m_defaultFontId] = std::move(fontData);
    return m_defaultFontId;
}

const FontData* QuarkGLFont::GetFontData(IFont font) const {
    const auto it = m_fonts.find(font.id);
    return it != m_fonts.end() ? &it->second : nullptr;
}

uint32_t QuarkGLFont::AddFont(FontData&& font) {
    const uint32_t id = m_nextFontId++;
    m_fonts[id] = std::move(font);
    return id;
}

void QuarkGLFont::RemoveFont(uint32_t fontId) {
    auto it = m_fonts.find(fontId);
    if (it == m_fonts.end()) return;

    if (it->second.atlasTexture) {
        glDeleteTextures(1, &it->second.atlasTexture);
    }
    for (GlyphData& g : it->second.glyphs) {
        std::free(g.image.data);
        g.image = Image{};
    }
    m_fonts.erase(it);
    if (fontId == m_defaultFontId) m_defaultFontId = 0;
}

void QuarkGLFont::Clear() {
    for (auto& [id, font] : m_fonts) {
        if (font.atlasTexture) glDeleteTextures(1, &font.atlasTexture);
        for (GlyphData& g : font.glyphs) {
            std::free(g.image.data);
            g.image = Image{};
        }
    }
    m_fonts.clear();
    m_defaultFontId = 0;
    m_nextFontId = 1;
}

void QuarkGLFont::DrawTextWithFontData(const FontData& fontData, const char* text,
                                       Vec2 pos, float fontSize, float spacing, Color tint) {
    if (!text) return;

    const float scale = fontSize / static_cast<float>(fontData.baseSize);
    const float lineHeight = static_cast<float>(fontData.lineHeight) * scale;
    const float baseline = static_cast<float>(fontData.ascent) * scale;

    float x = pos.x;
    float y = pos.y;
    bool first = true;

    for (const char* c = text; *c; ) {
        if (*c == '\n') {
            x = pos.x;
            y += lineHeight;
            first = true;
            ++c;
            continue;
        }

        const int cp = DecodeUTF8(c);
        const int idx = FindGlyph(fontData, cp);
        if (idx < 0) continue;

        const GlyphData& g = fontData.glyphs[static_cast<size_t>(idx)];
        if (!first) x += spacing;
        first = false;

        float gx = x + g.offsetX * scale;
        float gy = y + baseline - g.offsetY * scale;
        float gw = static_cast<float>(g.width) * scale;
        float gh = static_cast<float>(g.height) * scale;
        if (gw > 0.0f && gh > 0.0f) {
            (void)gx;
            (void)gy;
            (void)gw;
            (void)gh;
            (void)tint;
        }

        x += g.advanceX * scale;
    }
}

Vec2 QuarkGLFont::MeasureTextWithFontData(const FontData& fontData, const char* text,
                                          float fontSize, float spacing) const {
    if (!text) return {};

    const float scale = static_cast<float>(fontSize) / static_cast<float>(fontData.baseSize);
    const float lineHeight = static_cast<float>(fontData.lineHeight) * scale;

    float x = 0.0f;
    float maxW = 0.0f;
    bool first = true;
    int lines = 1;

    for (const char* c = text; *c; ) {
        if (*c == '\n') {
            maxW = std::max(maxW, x);
            x = 0.0f;
            first = true;
            ++lines;
            ++c;
            continue;
        }

        const int cp = DecodeUTF8(c);
        const int idx = FindGlyph(fontData, cp);
        if (idx < 0) continue;

        if (!first) x += spacing;
        first = false;
        x += fontData.glyphs[static_cast<size_t>(idx)].advanceX * scale;
    }

    return {std::max(maxW, x), lineHeight * static_cast<float>(lines)};
}

} // namespace qc
