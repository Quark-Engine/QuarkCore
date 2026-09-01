#ifndef __QUARK_GL_FONT__
#define __QUARK_GL_FONT__

#include "../QuarkIRenderer.hpp"

#include <glad/glad.h>

#include <vector>
#include <unordered_map>

namespace qc {

struct GlyphData {
    int value = 0;
    Rectangle uv{};
    Rectangle rec{};
    Image image{};
    float advanceX = 0.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    int width = 0;
    int height = 0;
};

struct FontData {
    GLuint atlasTexture = 0;
    int atlasWidth = 0;
    int atlasHeight = 0;
    int baseSize = 0;
    int ascent = 0;
    int descent = 0;
    int lineHeight = 0;
    int lineGap = 0;
    int glyphCount = 0;
    std::vector<GlyphData> glyphs;
};

class QuarkGLFont {
public:
    QuarkGLFont() = default;

    bool LoadFontInternal(const char* filePath, const unsigned char* fileData, int dataSize,
                          int pointSize, const int* codepoints, int codepointCount, FontData& out);
    uint32_t EnsureDefaultFont();
    const FontData* GetFontData(IFont font) const;
    void DrawTextWithFontData(const FontData& fontData, const char* text,
                              Vec2 pos, float fontSize, float spacing, Color tint);
    Vec2 MeasureTextWithFontData(const FontData& fontData, const char* text,
                                 float fontSize, float spacing) const;

    uint32_t AddFont(FontData&& font);
    void RemoveFont(uint32_t fontId);
    void Clear();

    std::unordered_map<uint32_t, FontData>& Fonts() { return m_fonts; }
    const std::unordered_map<uint32_t, FontData>& Fonts() const { return m_fonts; }

    uint32_t DefaultFontId() const { return m_defaultFontId; }
    uint32_t NextFontId() const { return m_nextFontId; }

private:
    static int DecodeUTF8(const char*& p);
    static int DefaultCodepointCount();
    static int FindGlyph(const FontData& fd, int codepoint);

    std::unordered_map<uint32_t, FontData> m_fonts;
    uint32_t m_nextFontId = 1;
    uint32_t m_defaultFontId = 0;
};

} // namespace qc

#endif // __QUARK_GL_FONT__
