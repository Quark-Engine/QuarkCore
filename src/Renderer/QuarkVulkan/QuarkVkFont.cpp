#include "QuarkVkRenderer.hpp"

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

namespace qc {
namespace {

const char* FindDefaultFontPath() {
#ifdef _WIN32
    static constexpr const char* kCandidates[] = {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/consola.ttf"
    };
#else
    static constexpr const char* kCandidates[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf"
    };
#endif
    for (const char* path : kCandidates) {
        std::ifstream file(path);
        if (file.good()) {
            return path;
        }
    }
    return nullptr;
}

std::vector<int> DefaultCodepoints() {
    std::vector<int> cps;
    cps.reserve(96);
    for (int c = 32; c <= 126; ++c) cps.push_back(c);
    return cps;
}

int DecodeUTF8(const char*& p) {
    const unsigned char lead = static_cast<unsigned char>(*p);
    int cp = 0, seq = 0;
    if ((lead & 0x80) == 0)      { cp = lead; seq = 1; }
    else if ((lead & 0xE0) == 0xC0) { cp = lead & 0x1F; seq = 2; }
    else if ((lead & 0xF0) == 0xE0) { cp = lead & 0x0F; seq = 3; }
    else if ((lead & 0xF8) == 0xF0) { cp = lead & 0x07; seq = 4; }
    else                        { cp = lead; seq = 1; }
    for (int k = 1; k < seq && p[k] != '\0'; ++k)
        cp = (cp << 6) | (static_cast<unsigned char>(p[k]) & 0x3F);
    p += seq;
    return cp;
}

} // namespace

static float NormalizeColorComponent(std::uint8_t value) {
    return static_cast<float>(value) / 255.0f;
}

bool QuarkVkRenderer::LoadFontInternal(const char* filePath, const unsigned char* fileData, int dataSize,
                                       int pointSize, const int* codepoints, int codepointCount, FontData& out) {
    if ((!filePath && !fileData) || pointSize <= 0) {
        TraceLog(LogLevel::Warn, "FONT", "[Vulkan] Cannot load font: invalid source or point size");
        return false;
    }

    TraceLog(LogLevel::Trace, "FONT", TextFormat("[Vulkan] FreeType initializing font: %s (size: %d pt)", filePath ? filePath : "<memory>", pointSize));

    FT_Library ft = nullptr;
    if (FT_Init_FreeType(&ft) != 0) {
        TraceLog(LogLevel::Error, "FONT", "[Vulkan] Failed to initialize FreeType library");
        return false;
    }

    FT_Face face = nullptr;
    if (fileData != nullptr) {
        if (FT_New_Memory_Face(ft, fileData, static_cast<FT_Long>(dataSize), 0, &face) != 0) {
            TraceLog(LogLevel::Error, "FONT", "[Vulkan] FreeType failed to open font from memory");
            FT_Done_FreeType(ft);
            return false;
        }
    } else if (FT_New_Face(ft, filePath, 0, &face) != 0) {
        TraceLog(LogLevel::Error, "FONT", TextFormat("[Vulkan] FreeType failed to open font file: %s", filePath));
        FT_Done_FreeType(ft);
        return false;
    }

    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pointSize)) != 0) {
        TraceLog(LogLevel::Error, "FONT", TextFormat("[Vulkan] FreeType failed to set pixel size %d for: %s", pointSize, filePath ? filePath : "<memory>"));
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return false;
    }

    std::vector<int> cps;
    const int* cpsPtr = codepoints;
    int cpsCount = codepointCount;
    if (cpsPtr == nullptr || cpsCount <= 0) {
        cps = DefaultCodepoints();
        cpsPtr = cps.data();
        cpsCount = static_cast<int>(cps.size());
    }

    constexpr int atlasWidth = 1024;
    constexpr int atlasHeight = 1024;
    std::vector<unsigned char> atlas(atlasWidth * atlasHeight * 4, 0);

    int penX = 1;
    int penY = 1;
    int rowH = 0;
    int renderedGlyphs = 0;
    out.glyphs.clear();
    out.glyphs.reserve(static_cast<size_t>(cpsCount));

    for (int i = 0; i < cpsCount; ++i) {
        const int cp = cpsPtr[i];
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) {
            continue;
        }

        FT_GlyphSlot slot = face->glyph;
        const int gw = slot->bitmap.width;
        const int gh = slot->bitmap.rows;

        GlyphData g;
        g.value    = cp;
        g.advanceX = static_cast<float>(slot->advance.x) / 64.f;
        g.offsetX  = static_cast<float>(slot->bitmap_left);
        g.offsetY  = static_cast<float>(slot->bitmap_top);
        g.width    = gw;
        g.height   = gh;

        if (gw > 0 && gh > 0) {
            unsigned char* gdata = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(gw) * gh * 4));
            if (!gdata) {
                FT_Done_Face(face);
                FT_Done_FreeType(ft);
                return false;
            }
            for (int row = 0; row < gh; ++row) {
                for (int col = 0; col < gw; ++col) {
                    const unsigned char alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
                    gdata[(static_cast<size_t>(row) * gw + col) * 4u + 0] = 255;
                    gdata[(static_cast<size_t>(row) * gw + col) * 4u + 1] = 255;
                    gdata[(static_cast<size_t>(row) * gw + col) * 4u + 2] = 255;
                    gdata[(static_cast<size_t>(row) * gw + col) * 4u + 3] = alpha;
                }
            }
            g.image = Image{ gdata, gw, gh, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        }

        if (penX + gw + 1 > atlasWidth) {
            penX = 1;
            penY += rowH + 1;
            rowH = 0;
            if (penY + 1 > atlasHeight) {
                TraceLog(LogLevel::Warn, "FONT", TextFormat("[Vulkan] Font atlas overflow (%dx%d) for font: %s", atlasWidth, atlasHeight, filePath ? filePath : "<memory>"));
                FT_Done_Face(face);
                FT_Done_FreeType(ft);
                return false;
            }
        }

        for (int row = 0; row < gh; ++row) {
            for (int col = 0; col < gw; ++col) {
                const size_t dst = ((penY + row) * atlasWidth + (penX + col)) * 4u;
                const unsigned char alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
                atlas[dst + 0] = 255;
                atlas[dst + 1] = 255;
                atlas[dst + 2] = 255;
                atlas[dst + 3] = alpha;
            }
        }

        g.rec = Rectangle{ static_cast<float>(penX), static_cast<float>(penY),
                           static_cast<float>(gw), static_cast<float>(gh) };
        g.uv = Rectangle{
            static_cast<float>(penX) / atlasWidth,
            static_cast<float>(penY) / atlasHeight,
            static_cast<float>(gw) / atlasWidth,
            static_cast<float>(gh) / atlasHeight
        };

        penX += gw + 1;
        rowH = std::max(rowH, gh);
        renderedGlyphs++;
        out.glyphs.push_back(g);
    }

    out.atlasTextureId = m_vkResources.CreateTextureFromRGBA(atlas.data(), static_cast<uint32_t>(atlasWidth), static_cast<uint32_t>(atlasHeight));
    out.atlasWidth  = atlasWidth;
    out.atlasHeight = atlasHeight;
    out.baseSize    = pointSize;
    out.glyphCount  = renderedGlyphs;
    out.ascent      = static_cast<int>(face->size->metrics.ascender / 64);
    out.descent     = static_cast<int>(face->size->metrics.descender / 64);
    out.lineHeight  = static_cast<int>(face->size->metrics.height / 64);
    out.lineGap     = out.lineHeight - (out.ascent - out.descent);

    const char* family = face->family_name ? face->family_name : "Unknown";
    const char* style  = face->style_name ? face->style_name : "Regular";
    TraceLog(LogLevel::Info, "FONT", TextFormat("[Vulkan] Font rasterized: %s (%s %s, %d glyphs, Atlas: %dx%d, Ascent: %d, Descent: %d, LineHeight: %d)",
        filePath ? filePath : "<in-memory>", family, style, renderedGlyphs, atlasWidth, atlasHeight, out.ascent, out.descent, out.lineHeight));

    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    return true;
}

int QuarkVkRenderer::FindGlyph(const FontData& fd, int codepoint) {
    for (int i = 0; i < static_cast<int>(fd.glyphs.size()); ++i) {
        if (fd.glyphs[i].value == codepoint) return i;
    }
    for (int i = 0; i < static_cast<int>(fd.glyphs.size()); ++i) {
        if (fd.glyphs[i].value == 63) return i;
    }
    return -1;
}

uint32_t QuarkVkRenderer::EnsureDefaultFont() {
    if (m_defaultFontId != 0) {
        return m_defaultFontId;
    }

    const char* path = FindDefaultFontPath();
    if (!path) {
        return 0;
    }

    FontData fd{};
    if (!LoadFontInternal(path, nullptr, 0, 32, nullptr, 0, fd)) {
        return 0;
    }

    const uint32_t id = m_nextFontId++;
    m_fonts[id] = std::move(fd);
    m_defaultFontId = id;
    return id;
}

const QuarkVkRenderer::FontData* QuarkVkRenderer::GetFontData(IFont font) const {
    if (font.id == 0) {
        return nullptr;
    }

    const auto it = m_fonts.find(font.id);
    return it != m_fonts.end() ? &it->second : nullptr;
}

void QuarkVkRenderer::DrawTextWithFontData(const FontData& fd, const char* text,
                                            Vec2 position, float fontSize, float spacing, Color tint) {
    if (!text || fd.atlasTextureId == 0) {
        return;
    }

    const float scale = fontSize / static_cast<float>(fd.baseSize);
    const float lineHeight = static_cast<float>(fd.lineHeight) * scale;
    const float baseline = static_cast<float>(fd.ascent) * scale;
    float x = position.x;
    float y = position.y;
    bool first = true;

    const float r = NormalizeColorComponent(tint.r);
    const float g = NormalizeColorComponent(tint.g);
    const float b = NormalizeColorComponent(tint.b);
    const float a = NormalizeColorComponent(tint.a);

    const VkDescriptorSet atlasDs = m_vkResources.DescriptorSet(fd.atlasTextureId);
    if (atlasDs == VK_NULL_HANDLE) {
        return;
    }

    for (const char* c = text; *c != '\0'; ) {
        if (*c == '\n') {
            x = position.x;
            y += lineHeight;
            first = true;
            ++c;
            continue;
        }

        const int cp = DecodeUTF8(c);
        const int idx = FindGlyph(fd, cp);
        if (idx < 0) {
            continue;
        }

        const GlyphData& glyph = fd.glyphs[static_cast<size_t>(idx)];
        if (!first) {
            x += spacing;
        }
        first = false;

        const float gx = x + glyph.offsetX * scale;
        const float gy = y + baseline - glyph.offsetY * scale;
        const float gw = static_cast<float>(glyph.width) * scale;
        const float gh = static_cast<float>(glyph.height) * scale;

        if (gw > 0.f && gh > 0.f) {
            AppendQuad(atlasDs,
                       gx, gy,
                       gx + gw, gy,
                       gx + gw, gy + gh,
                       gx, gy + gh,
                       r, g, b, a,
                       glyph.uv.x, glyph.uv.y, glyph.uv.x + glyph.uv.width, glyph.uv.y + glyph.uv.height);
        }

        x += glyph.advanceX * scale;
    }
}

Vec2 QuarkVkRenderer::MeasureTextWithFontData(const FontData& fd, const char* text,
                                               float fontSize, float spacing) const {
    if (!text) {
        return {};
    }

    const float scale = fontSize / static_cast<float>(fd.baseSize);
    const float lineHeight = static_cast<float>(fd.lineHeight) * scale;

    float x = 0.f;
    float maxW = 0.f;
    bool first = true;
    int lines = 1;

    for (const char* c = text; *c != '\0'; ) {
        if (*c == '\n') {
            maxW = std::max(maxW, x);
            x = 0.f;
            first = true;
            ++lines;
            ++c;
            continue;
        }

        const int cp = DecodeUTF8(c);
        const int idx = FindGlyph(fd, cp);
        if (idx < 0) {
            continue;
        }

        const GlyphData& g = fd.glyphs[static_cast<size_t>(idx)];
        if (!first) {
            x += spacing;
        }
        first = false;
        x += g.advanceX * scale;
    }

    return Vec2{ std::max(maxW, x), lineHeight * static_cast<float>(lines) };
}

void QuarkVkRenderer::DrawText(const char* text, int x, int y, int fontSize, Color color) {
    const uint32_t id = EnsureDefaultFont();
    if (id == 0) {
        return;
    }

    DrawTextWithFontData(m_fonts[id], text, Vec2{ static_cast<float>(x), static_cast<float>(y) },
                         static_cast<float>(fontSize), 0.f, color);
}

void QuarkVkRenderer::DrawTextEx(IFont font, const char* text, Vec2 position, float fontSize, float spacing, Color tint) {
    const FontData* fd = GetFontData(font);
    if (!fd) {
        const uint32_t id = EnsureDefaultFont();
        if (id == 0) {
            return;
        }
        fd = &m_fonts[id];
    }

    DrawTextWithFontData(*fd, text, position, fontSize, spacing, tint);
}

Vec2 QuarkVkRenderer::MeasureTextEx(IFont font, const char* text, float fontSize, float spacing) {
    const FontData* fd = GetFontData(font);
    if (!fd) {
        const uint32_t id = EnsureDefaultFont();
        if (id == 0) {
            return {};
        }
        fd = &m_fonts[id];
    }

    return MeasureTextWithFontData(*fd, text, fontSize, spacing);
}

int QuarkVkRenderer::MeasureText(const char* text, int fontSize) {
    const uint32_t id = EnsureDefaultFont();
    if (id == 0) {
        return 0;
    }

    return static_cast<int>(std::round(MeasureTextWithFontData(m_fonts[id], text, static_cast<float>(fontSize), 0.f).x));
}

IFont QuarkVkRenderer::LoadFont(const char* filePath, int fontSize,
                                const int* codepoints, int codepointCount) {
    if (!filePath || fontSize <= 0) {
        TraceLog(LogLevel::Info, "FONT", "[Vulkan] Loading default system font...");
        const uint32_t id = EnsureDefaultFont();
        if (id) TraceLog(LogLevel::Info, "FONT", TextFormat("[Vulkan] Default font loaded (Font ID: %u)", id));
        return IFont{ id };
    }

    TraceLog(LogLevel::Trace, "FONT", TextFormat("[Vulkan] Loading font file: %s (size: %d pt, codepoints: %d)", filePath, fontSize, codepointCount));

    FontData fd{};
    if (!LoadFontInternal(filePath, nullptr, 0, fontSize, codepoints, codepointCount, fd)) {
        TraceLog(LogLevel::Error, "FONT", TextFormat("[Vulkan] Failed to load font: %s", filePath));
        return IFont{};
    }

    const uint32_t id = m_nextFontId++;
    const uint32_t atlasId = fd.atlasTextureId;
    const int baseSize = fd.baseSize;
    m_fonts[id] = std::move(fd);

    TraceLog(LogLevel::Info, "FONT", TextFormat("[Vulkan] Font loaded successfully: %s (BaseSize: %d, Atlas ID: %u, Font ID: %u)",
        filePath, baseSize, atlasId, id));
    return IFont{ id };
}

IFont QuarkVkRenderer::LoadFontFromMemory(const char* fileType, const unsigned char* fileData, int dataSize,
                                          int fontSize, const int* codepoints, int codepointCount) {
    if (!fileData || dataSize <= 0) {
        TraceLog(LogLevel::Error, "FONT", "[Vulkan] LoadFontFromMemory: invalid memory buffer");
        return IFont{};
    }

    TraceLog(LogLevel::Trace, "FONT", TextFormat("[Vulkan] Loading font from memory (type: %s, size: %d pt, %d bytes)", fileType ? fileType : ".ttf", fontSize, dataSize));

    FontData fd{};
    if (!LoadFontInternal(nullptr, fileData, dataSize, fontSize, codepoints, codepointCount, fd)) {
        TraceLog(LogLevel::Error, "FONT", "[Vulkan] Failed to load font from memory");
        return IFont{};
    }

    const uint32_t id = m_nextFontId++;
    const uint32_t atlasId = fd.atlasTextureId;
    m_fonts[id] = std::move(fd);

    TraceLog(LogLevel::Info, "FONT", TextFormat("[Vulkan] Font loaded from memory (BaseSize: %d, Atlas ID: %u, Font ID: %u)",
        fd.baseSize, atlasId, id));
    return IFont{ id };
}

void QuarkVkRenderer::UnloadFont(IFont& font) {
    if (font.id == 0) {
        return;
    }

    const auto it = m_fonts.find(font.id);
    if (it != m_fonts.end()) {
        const uint32_t atlasId = it->second.atlasTextureId;
        if (it->second.atlasTextureId != 0) {
            m_vkResources.DestroyTexture(it->second.atlasTextureId);
        }
        for (GlyphData& g : it->second.glyphs) {
            std::free(g.image.data);
            g.image = Image{};
        }
        if (font.id == m_defaultFontId) {
            m_defaultFontId = 0;
        }
        m_fonts.erase(it);
        TraceLog(LogLevel::Info, "FONT", TextFormat("[Vulkan] Font unloaded (Font ID: %u, Atlas ID: %u)", font.id, atlasId));
    }

    font.id = 0;
}

void QuarkVkRenderer::FillFont(IFont font, Font& out) {
    const FontData* fd = GetFontData(font);
    if (!fd) {
        const uint32_t id = EnsureDefaultFont();
        fd = (id != 0) ? GetFontData(IFont{ id }) : nullptr;
    }
    if (!fd) {
        out.valid = false;
        return;
    }

    out.baseSize     = fd->baseSize;
    out.glyphCount   = fd->glyphCount;
    out.glyphPadding = 2;
    out.valid        = true;
    out._rendererFontId = font.id;
    out.texture = Texture2D{ fd->atlasTextureId, fd->atlasWidth, fd->atlasHeight, 1,
                             PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, true };

    delete[] out.recs;
    delete[] out.glyphs;
    out.recs   = new Rectangle[fd->glyphCount];
    out.glyphs = new GlyphInfo[fd->glyphCount];

    for (int i = 0; i < fd->glyphCount; ++i) {
        const GlyphData& g = fd->glyphs[static_cast<size_t>(i)];
        out.recs[i] = g.rec;
        out.glyphs[i].value     = g.value;
        out.glyphs[i].offsetX   = static_cast<int>(g.offsetX);
        out.glyphs[i].offsetY   = static_cast<int>(g.offsetY);
        out.glyphs[i].advanceX  = static_cast<int>(g.advanceX);
        out.glyphs[i].image     = g.image;
    }
}

}; // namespace qc
