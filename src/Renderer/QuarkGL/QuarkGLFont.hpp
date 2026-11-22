#ifndef __QUARK_GL_FONT__
#define __QUARK_GL_FONT__

#include "../QuarkIRenderer.hpp"

#include <glad/glad.h>

#include <vector>
#include <unordered_map>

namespace qci
{
    struct GlyphData
    {
        int m_Value = 0;
        Rectangle m_Uv{};
        Rectangle m_Rec{};
        Image m_Image{};
        float m_AdvanceX = 0.0f;
        float m_OffsetX = 0.0f;
        float m_OffsetY = 0.0f;
        int m_Width = 0;
        int m_Height = 0;
    };

    struct FontData
    {
        GLuint m_AtlasTexture = 0;
        int m_AtlasWidth = 0;
        int m_AtlasHeight = 0;
        int m_BaseSize = 0;
        int m_Ascent = 0;
        int m_Descent = 0;
        int m_LineHeight = 0;
        int m_LineGap = 0;
        int m_GlyphCount = 0;
        std::vector<GlyphData> m_vGlyphs;
    };

    class QuarkGLFont
    {
    public:
        QuarkGLFont() = default;

        bool LoadFontInternal(const char* pFilePath, const unsigned char* pFileData, int dataSize,
                              int pointSize, const int* pCodepoints, int codepointCount, FontData& out);
        uint32_t EnsureDefaultFont();
        const FontData* GetFontData(IFont font) const;
        void DrawTextWithFontData(const FontData& fontData, const char* pText,
                                  Vec2 pos, float fontSize, float spacing, Color tint);
        Vec2 MeasureTextWithFontData(const FontData& fontData, const char* pText,
                                     float fontSize, float spacing) const;

        uint32_t AddFont(FontData&& font);
        void RemoveFont(uint32_t fontId);
        void Clear();

        std::unordered_map<uint32_t, FontData>& Fonts()
        {
            return m_Fonts;
        }

        const std::unordered_map<uint32_t, FontData>& Fonts() const
        {
            return m_Fonts;
        }

        uint32_t DefaultFontId() const
        {
            return m_DefaultFontId;
        }

        uint32_t NextFontId() const
        {
            return m_NextFontId;
        }

    private:
        static int DecodeUTF8(const char*& pText);
        static int DefaultCodepointCount();
        static int FindGlyph(const FontData& fd, int codepoint);

        std::unordered_map<uint32_t, FontData> m_Fonts;
        uint32_t m_NextFontId = 1;
        uint32_t m_DefaultFontId = 0;
    };
} // qci

#endif // __QUARK_GL_FONT__
