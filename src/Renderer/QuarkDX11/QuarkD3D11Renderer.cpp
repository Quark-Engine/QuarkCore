#include "QuarkD3D11Renderer.hpp"
#include "../../QuarkInternal.hpp"

#if defined(_WIN32)
#include <ft2build.h>
#include FT_FREETYPE_H
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>

namespace qc
{

namespace {

const char* ShaderLocationNames[SHADER_LOC_COUNT] = {
    "POSITION",
    "TEXCOORD0",
    "TEXCOORD1",
    "NORMAL",
    "TANGENT",
    "COLOR",
    "mvp",
    "view",
    "projection",
    "model",
    "normalMatrix",
    "viewPos",
    "colDiffuse",
    "colSpecular",
    "colAmbient",
    "albedo",
    "metalness",
    "normal",
    "roughness",
    "occlusion",
    "emission",
    "height",
    "cubemap",
    "irradiance",
    "prefilter",
    "brdf",
    "boneIds",
    "boneWeights",
    "boneTransforms",
    "instanceTransform"
};

constexpr char kDefaultHlslVertexSource[] = R"(
    struct VSInput {
        float2 position : POSITION;
        float2 texCoord : TEXCOORD0;
        float4 color : COLOR;
    };

    struct VSOutput {
        float4 position : SV_POSITION;
        float2 texCoord : TEXCOORD0;
        float4 color : COLOR;
    };

    VSOutput main(VSInput input) {
        VSOutput output;
        output.position = float4(input.position, 0.0, 1.0);
        output.texCoord = input.texCoord;
        output.color = input.color;
        return output;
    }
)";

constexpr char kDefaultHlslPixelSource[] = R"(
    Texture2D textureMap : register(t0);
    SamplerState textureSampler : register(s0);

    struct PSInput {
        float4 position : SV_POSITION;
        float2 texCoord : TEXCOORD0;
        float4 color : COLOR;
    };

    float4 main(PSInput input) : SV_TARGET {
        return textureMap.Sample(textureSampler, input.texCoord) * input.color;
    }
)";

bool ReadTextFile(const char* path, std::string& out)
{
    if (!path) {
        return false;
    }

    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    out.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return true;
}

UINT AlignTo16(UINT value)
{
    return (value + 15u) & ~15u;
}

UINT CountMaskComponents(BYTE mask)
{
    UINT count = 0;
    for (BYTE bit = 1; bit != 0; bit <<= 1) {
        if (mask & bit) {
            ++count;
        }
    }
    return count > 0 ? count : 1;
}

DXGI_FORMAT SignatureFormat(D3D_REGISTER_COMPONENT_TYPE componentType, UINT components)
{
    switch (componentType) {
        case D3D_REGISTER_COMPONENT_SINT32:
            return components >= 4 ? DXGI_FORMAT_R32G32B32A32_SINT :
                   components == 3 ? DXGI_FORMAT_R32G32B32_SINT :
                   components == 2 ? DXGI_FORMAT_R32G32_SINT : DXGI_FORMAT_R32_SINT;
        case D3D_REGISTER_COMPONENT_UINT32:
            return components >= 4 ? DXGI_FORMAT_R32G32B32A32_UINT :
                   components == 3 ? DXGI_FORMAT_R32G32B32_UINT :
                   components == 2 ? DXGI_FORMAT_R32G32_UINT : DXGI_FORMAT_R32_UINT;
        case D3D_REGISTER_COMPONENT_FLOAT32:
        default:
            return components >= 4 ? DXGI_FORMAT_R32G32B32A32_FLOAT :
                   components == 3 ? DXGI_FORMAT_R32G32B32_FLOAT :
                   components == 2 ? DXGI_FORMAT_R32G32_FLOAT : DXGI_FORMAT_R32_FLOAT;
    }
}

bool SemanticEquals(const char* semantic, const char* name)
{
    return semantic != nullptr && _stricmp(semantic, name) == 0;
}

const char* DefaultFontPath()
{
    static const char* paths[] = {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/verdana.ttf",
        nullptr
    };

    for (int index = 0; paths[index] != nullptr; ++index) {
        std::ifstream file(paths[index], std::ios::binary);
        if (file.good()) {
            return paths[index];
        }
    }

    return nullptr;
}

void DrawThickLine(D3D11CommandContext &commands,
                   Vec2 start,
                   Vec2 end,
                   float lineWidth,
                   Color color,
                   int screenWidth,
                   int screenHeight)
{
    const float deltaX = end.x - start.x;
    const float deltaY = end.y - start.y;
    const float length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    const float halfWidth = std::max(lineWidth, 1.0f) * 0.5f;

    if (length <= 0.0f)
    {
        return;
    }

    const Vec2 normal{-deltaY / length * halfWidth, deltaX / length * halfWidth};
    const Vec2 points[] = {
        {start.x + normal.x, start.y + normal.y},
        {end.x + normal.x, end.y + normal.y},
        {end.x - normal.x, end.y - normal.y},
        {start.x - normal.x, start.y - normal.y}
    };

    commands.DrawTriangle(points[0], points[1], points[2], color, screenWidth, screenHeight);
    commands.DrawTriangle(points[0], points[2], points[3], color, screenWidth, screenHeight);
}

} // namespace

QuarkD3D11Renderer::~QuarkD3D11Renderer()
{
    Shutdown();
}

void QuarkD3D11Renderer::Init(SDL_Window *window, int width, int height)
{
    TraceLog(LogLevel::Info, "D3D11",
             TextFormat("Initializing renderer (Window: %dx%d)", width, height));
    m_window = window;
    SDL_GetWindowSizeInPixels(window, &m_width, &m_height);
    if (m_width <= 0 || m_height <= 0)
    {
        m_width = width;
        m_height = height;
    }
    m_lastFrame = std::chrono::steady_clock::now();

    m_device.Initialize();
    m_resources.Initialize(m_device.Get());
    m_swapChain.Initialize(m_device, window, m_width, m_height);
    m_pipeline.Initialize(m_device.Get(), m_shaderCompiler, m_resources);
    m_commands.Initialize(m_device, m_swapChain, m_pipeline, m_resources, m_width, m_height);
    {
        const uint8_t white[4] = {255, 255, 255, 255};
        m_whiteShaderTexture = m_resources.CreateTexture(m_device.Get(), white, 1, 1);
    }
    RefreshViewport();
    TraceLog(LogLevel::Info, "D3D11", "Renderer initialized successfully.");
}

void QuarkD3D11Renderer::Shutdown()
{
    TraceLog(LogLevel::Info, "D3D11", "Shutting down D3D11 renderer...");

    m_currentShaderId = 0;
    m_shaderPrograms.clear();
    if (m_whiteShaderTexture.IsValid()) {
        m_resources.DestroyTexture(m_whiteShaderTexture.id);
        m_whiteShaderTexture = {};
    }
    m_commands.Shutdown();
    m_pipeline.Shutdown();
    m_resources.Shutdown();
    m_swapChain.Shutdown();
    m_device.Shutdown();
    m_window = nullptr;
    m_drawing = false;

    TraceLog(LogLevel::Info, "D3D11", "D3D11 renderer shut down successfully.");
}

void QuarkD3D11Renderer::RefreshViewport()
{
    if (m_window)
    {
        int pixelWidth = 0;
        int pixelHeight = 0;
        SDL_GetWindowSizeInPixels(m_window, &pixelWidth, &pixelHeight);

        if (pixelWidth > 0 && pixelHeight > 0 &&
            (pixelWidth != m_width || pixelHeight != m_height))
        {
            m_swapChain.Resize(m_device, pixelWidth, pixelHeight);
            m_width = pixelWidth;
            m_height = pixelHeight;
        }
    }

    m_commands.SetDefaultViewportSize(m_width, m_height);
    m_commands.RefreshViewport(m_width, m_height);
}

void QuarkD3D11Renderer::BeginDrawing()
{
    const auto now = std::chrono::steady_clock::now();
    m_frameTime = std::chrono::duration<float>(now - m_lastFrame).count();
    m_lastFrame = now;
    m_drawing = true;
    m_commands.BeginDrawing();
}

void QuarkD3D11Renderer::EndDrawing()
{
    m_commands.EndDrawing(m_vsync);
    m_drawing = false;
}

void QuarkD3D11Renderer::ClearBackground(Color color)
{
    m_commands.Clear(color);
}

void QuarkD3D11Renderer::DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color)
{
    m_commands.DrawTriangle(v1, v2, v3, color, m_width, m_height);
}

void QuarkD3D11Renderer::DrawRectangle(float x,
                                       float y,
                                       float width,
                                       float height,
                                       Color color)
{
    DrawRectangle(Rectangle{x, y, width, height}, color);
}

void QuarkD3D11Renderer::DrawRectangle(const Rectangle &rectangle, Color color)
{
    DrawRectangleV(
        Vec2{rectangle.x, rectangle.y},
        Vec2{rectangle.width, rectangle.height},
        color);
}

void QuarkD3D11Renderer::DrawRectangleV(Vec2 position, Vec2 size, Color color)
{
    const Vec2 topLeft{position.x, position.y};
    const Vec2 topRight{position.x + size.x, position.y};
    const Vec2 bottomRight{position.x + size.x, position.y + size.y};
    const Vec2 bottomLeft{position.x, position.y + size.y};

    m_commands.DrawTriangle(topLeft, topRight, bottomRight, color, m_width, m_height);
    m_commands.DrawTriangle(topLeft, bottomRight, bottomLeft, color, m_width, m_height);
}

void QuarkD3D11Renderer::DrawRectangleLines(Rectangle rectangle,
                                             float lineWidth,
                                             Color color)
{
    const Vec2 topLeft{rectangle.x, rectangle.y};
    const Vec2 topRight{rectangle.x + rectangle.width, rectangle.y};
    const Vec2 bottomRight{rectangle.x + rectangle.width, rectangle.y + rectangle.height};
    const Vec2 bottomLeft{rectangle.x, rectangle.y + rectangle.height};

    DrawThickLine(m_commands, topLeft, topRight, lineWidth, color, m_width, m_height);
    DrawThickLine(m_commands, topRight, bottomRight, lineWidth, color, m_width, m_height);
    DrawThickLine(m_commands, bottomRight, bottomLeft, lineWidth, color, m_width, m_height);
    DrawThickLine(m_commands, bottomLeft, topLeft, lineWidth, color, m_width, m_height);
}

void QuarkD3D11Renderer::DrawRectangleRounded(Rectangle rectangle,
                                                float roundness,
                                                int segments,
                                                Color color)
{
    const float shortestSide = std::min(std::abs(rectangle.width), std::abs(rectangle.height));
    const float radius = std::clamp(roundness * shortestSide, 0.0f, shortestSide * 0.5f);
    const int cornerSegments = std::max(segments, 1);

    if (radius <= 0.0f)
    {
        DrawRectangle(rectangle, color);
        return;
    }

    const Vec2 centers[] = {
        {rectangle.x + radius, rectangle.y + radius},
        {rectangle.x + rectangle.width - radius, rectangle.y + radius},
        {rectangle.x + rectangle.width - radius, rectangle.y + rectangle.height - radius},
        {rectangle.x + radius, rectangle.y + rectangle.height - radius}
    };
    const Vec2 center{rectangle.x + rectangle.width * 0.5f,
                      rectangle.y + rectangle.height * 0.5f};

    std::vector<Vec2> points;
    points.reserve(static_cast<size_t>(cornerSegments * 4));

    for (int corner = 0; corner < 4; ++corner)
    {
        const float startAngle = -PI * 0.5f + corner * PI * 0.5f;

        for (int segment = 0; segment < cornerSegments; ++segment)
        {
            const float angle = startAngle + PI * 0.5f * segment / cornerSegments;
            points.push_back({
                centers[corner].x + std::cos(angle) * radius,
                centers[corner].y + std::sin(angle) * radius
            });
        }
    }

    for (size_t index = 1; index + 1 < points.size(); ++index)
    {
        m_commands.DrawTriangle(center, points[0], points[index], color, m_width, m_height);
    }
}

void QuarkD3D11Renderer::DrawCircle(float centerX,
                                    float centerY,
                                    float radius,
                                    Color color)
{
    DrawEllipse(centerX, centerY, radius, radius, color);
}

void QuarkD3D11Renderer::DrawCircleLines(float centerX,
                                          float centerY,
                                          float radius,
                                          Color color)
{
    constexpr int segments = 32;
    Vec2 previous{centerX + radius, centerY};

    for (int index = 1; index <= segments; ++index)
    {
        const float angle = 2.0f * PI * index / segments;
        const Vec2 current{centerX + std::cos(angle) * radius,
                           centerY + std::sin(angle) * radius};
        DrawThickLine(m_commands, previous, current, 1.0f, color, m_width, m_height);
        previous = current;
    }
}

void QuarkD3D11Renderer::DrawEllipse(float centerX,
                                     float centerY,
                                     float radiusH,
                                     float radiusV,
                                     Color color)
{
    constexpr int segments = 32;
    const Vec2 center{centerX, centerY};

    for (int index = 0; index < segments; ++index)
    {
        const float firstAngle = 2.0f * PI * index / segments;
        const float secondAngle = 2.0f * PI * (index + 1) / segments;
        const Vec2 first{centerX + std::cos(firstAngle) * radiusH,
                         centerY + std::sin(firstAngle) * radiusV};
        const Vec2 second{centerX + std::cos(secondAngle) * radiusH,
                          centerY + std::sin(secondAngle) * radiusV};

        m_commands.DrawTriangle(center, first, second, color, m_width, m_height);
    }
}

void QuarkD3D11Renderer::DrawLine(float x1, float y1, float x2, float y2, Color color)
{
    DrawLineV(Vec2{x1, y1}, Vec2{x2, y2}, color);
}

void QuarkD3D11Renderer::DrawLineV(Vec2 start, Vec2 end, Color color)
{
    DrawThickLine(m_commands, start, end, 1.0f, color, m_width, m_height);
}

void QuarkD3D11Renderer::DrawPoly(Vec2 center,
                                   int sides,
                                   float radius,
                                   float rotation,
                                   Color color)
{
    if (sides < 3 || radius <= 0.0f)
    {
        return;
    }

    const float rotationRadians = rotation * PI / 180.0f;

    for (int index = 0; index < sides; ++index)
    {
        const float firstAngle = rotationRadians + 2.0f * PI * index / sides;
        const float secondAngle = rotationRadians + 2.0f * PI * (index + 1) / sides;
        const Vec2 first{center.x + std::cos(firstAngle) * radius,
                         center.y + std::sin(firstAngle) * radius};
        const Vec2 second{center.x + std::cos(secondAngle) * radius,
                          center.y + std::sin(secondAngle) * radius};

        m_commands.DrawTriangle(center, first, second, color, m_width, m_height);
    }
}

bool QuarkD3D11Renderer::LoadFontData(const char* filePath,
                                      int fontSize,
                                      FontData& fontData)
{
    TraceLog(LogLevel::Trace,
             "FONT",
             TextFormat("[D3D11] FreeType initializing font: %s (size: %d px)",
                        filePath ? filePath : "<null>",
                        fontSize));

    if (!filePath || fontSize <= 0) {
        TraceLog(LogLevel::Error, "FONT", "[D3D11] Invalid font path or point size.");
        return false;
    }

    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) {
        TraceLog(LogLevel::Error, "FONT", "[D3D11] Failed to initialize FreeType library.");
        return false;
    }

    FT_Face face = nullptr;
    if (FT_New_Face(library, filePath, 0, &face) != 0) {
        TraceLog(LogLevel::Error,
                 "FONT",
                 TextFormat("[D3D11] Failed to open font file: %s", filePath));
        FT_Done_FreeType(library);
        return false;
    }

    FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(fontSize));

    constexpr int atlasWidth = 1024;
    constexpr int atlasHeight = 1024;
    std::vector<uint8_t> atlas(static_cast<size_t>(atlasWidth) * atlasHeight * 4, 0);
    int penX = 1;
    int penY = 1;
    int rowHeight = 0;
    int renderedGlyphs = 0;

    for (unsigned char character = 32; character < 127; ++character) {
        if (FT_Load_Char(face, character, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) {
            continue;
        }

        FT_GlyphSlot glyph = face->glyph;
        const int glyphWidth = static_cast<int>(glyph->bitmap.width);
        const int glyphHeight = static_cast<int>(glyph->bitmap.rows);

        if (penX + glyphWidth + 1 > atlasWidth) {
            penX = 1;
            penY += rowHeight + 1;
            rowHeight = 0;
        }

        if (penY + glyphHeight + 1 > atlasHeight) {
            TraceLog(LogLevel::Error, "FONT", "[D3D11] Font atlas capacity exceeded.");
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            return false;
        }

        for (int row = 0; row < glyphHeight; ++row) {
            for (int column = 0; column < glyphWidth; ++column) {
                const size_t destination =
                    (static_cast<size_t>(penY + row) * atlasWidth + penX + column) * 4;
                atlas[destination] = 255;
                atlas[destination + 1] = 255;
                atlas[destination + 2] = 255;
                atlas[destination + 3] = glyph->bitmap.buffer[row * glyph->bitmap.pitch + column];
            }
        }

        GlyphData& data = fontData.glyphs[character - 32];
        data.uv = {
            static_cast<float>(penX) / atlasWidth,
            static_cast<float>(penY) / atlasHeight,
            static_cast<float>(glyphWidth) / atlasWidth,
            static_cast<float>(glyphHeight) / atlasHeight
        };
        data.advanceX = static_cast<float>(glyph->advance.x) / 64.0f;
        data.offsetX = static_cast<float>(glyph->bitmap_left);
        data.offsetY = static_cast<float>(glyph->bitmap_top);
        data.width = glyphWidth;
        data.height = glyphHeight;

        penX += glyphWidth + 1;
        rowHeight = std::max(rowHeight, glyphHeight);
        ++renderedGlyphs;
    }

    fontData.atlasTexture = m_resources.CreateTexture(
        m_device.Get(), atlas.data(), atlasWidth, atlasHeight);
    fontData.baseSize = fontSize;
    fontData.ascent = static_cast<int>(face->size->metrics.ascender / 64);
    fontData.descent = static_cast<int>(face->size->metrics.descender / 64);
    fontData.lineHeight = static_cast<int>(face->size->metrics.height / 64);

    TraceLog(LogLevel::Info,
             "FONT",
             TextFormat("[D3D11] Font rasterized: %s (%d glyphs, Atlas: %dx%d, Ascent: %d, "
                        "Descent: %d, LineHeight: %d)",
                        filePath,
                        renderedGlyphs,
                        atlasWidth,
                        atlasHeight,
                        fontData.ascent,
                        fontData.descent,
                        fontData.lineHeight));

    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return fontData.atlasTexture.IsValid();
}

const QuarkD3D11Renderer::FontData* QuarkD3D11Renderer::GetFontData(IFont font) const
{
    const auto iterator = m_fonts.find(font.id);
    return iterator == m_fonts.end() ? nullptr : &iterator->second;
}

uint32_t QuarkD3D11Renderer::EnsureDefaultFont()
{
    if (m_defaultFontId != 0) {
        return m_defaultFontId;
    }

    const char* path = DefaultFontPath();
    if (!path) {
        TraceLog(LogLevel::Error, "FONT", "[D3D11] No default system font was found.");
        return 0;
    }

    FontData fontData{};
    if (!LoadFontData(path, 32, fontData)) {
        return 0;
    }

    m_defaultFontId = m_nextFontId++;
    m_fonts.emplace(m_defaultFontId, std::move(fontData));
    TraceLog(LogLevel::Info,
             "FONT",
             TextFormat("[D3D11] Default font loaded (Font ID: %u)", m_defaultFontId));
    return m_defaultFontId;
}

void QuarkD3D11Renderer::DrawTextWithFontData(const FontData& fontData,
                                              const char* text,
                                              Vec2 position,
                                              float fontSize,
                                              float spacing,
                                              Color tint)
{
    if (!text || fontData.baseSize <= 0 || fontSize <= 0.0f) {
        return;
    }

    const float scale = fontSize / fontData.baseSize;
    const float baseline = fontData.ascent * scale;
    const float lineHeight = fontData.lineHeight * scale;
    float cursorX = position.x;
    float cursorY = position.y;
    bool firstGlyph = true;

    for (const char* character = text; *character != '\0'; ++character) {
        if (*character == '\n') {
            cursorX = position.x;
            cursorY += lineHeight;
            firstGlyph = true;
            continue;
        }

        const unsigned char code = static_cast<unsigned char>(*character);
        if (code < 32 || code >= 127) {
            continue;
        }

        const GlyphData& glyph = fontData.glyphs[code - 32];
        if (!firstGlyph) {
            cursorX += spacing;
        }
        firstGlyph = false;

        const float glyphX = cursorX + glyph.offsetX * scale;
        const float glyphY = cursorY + baseline - glyph.offsetY * scale;
        const float glyphWidth = glyph.width * scale;
        const float glyphHeight = glyph.height * scale;

        if (glyphWidth > 0.0f && glyphHeight > 0.0f) {
            const Rectangle sourcePixels{
                glyph.uv.x * fontData.atlasTexture.width,
                glyph.uv.y * fontData.atlasTexture.height,
                glyph.uv.width * fontData.atlasTexture.width,
                glyph.uv.height * fontData.atlasTexture.height
            };

            m_commands.DrawTextureQuad(
                fontData.atlasTexture,
                sourcePixels,
                {glyphX, glyphY, glyphWidth, glyphHeight},
                {0.0f, 0.0f},
                0.0f,
                tint,
                m_width,
                m_height);
        }

        cursorX += glyph.advanceX * scale;
    }
}

Vec2 QuarkD3D11Renderer::MeasureTextWithFontData(const FontData& fontData,
                                                 const char* text,
                                                 float fontSize,
                                                 float spacing) const
{
    if (!text || fontData.baseSize <= 0 || fontSize <= 0.0f) {
        return {};
    }

    const float scale = fontSize / fontData.baseSize;
    const float lineHeight = fontData.lineHeight * scale;
    float width = 0.0f;
    float maximumWidth = 0.0f;
    bool firstGlyph = true;
    int lineCount = 1;

    for (const char* character = text; *character != '\0'; ++character) {
        if (*character == '\n') {
            maximumWidth = std::max(maximumWidth, width);
            width = 0.0f;
            firstGlyph = true;
            ++lineCount;
            continue;
        }

        const unsigned char code = static_cast<unsigned char>(*character);
        if (code < 32 || code >= 127) {
            continue;
        }

        if (!firstGlyph) {
            width += spacing;
        }
        firstGlyph = false;
        width += fontData.glyphs[code - 32].advanceX * scale;
    }

    return {std::max(maximumWidth, width), lineHeight * lineCount};
}

IFont QuarkD3D11Renderer::LoadFont(const char* filePath, int fontSize)
{
    if (!filePath) {
        return IFont{EnsureDefaultFont()};
    }

    FontData fontData{};
    if (!LoadFontData(filePath, fontSize, fontData)) {
        TraceLog(LogLevel::Error, "FONT", TextFormat("[D3D11] Failed to load font: %s", filePath));
        return {};
    }

    const uint32_t id = m_nextFontId++;
    m_fonts.emplace(id, std::move(fontData));
    TraceLog(LogLevel::Info,
             "FONT",
             TextFormat("[D3D11] Font loaded successfully: %s (Font ID: %u)", filePath, id));
    return IFont{id};
}

void QuarkD3D11Renderer::UnloadFont(IFont& font)
{
    const auto iterator = m_fonts.find(font.id);
    if (iterator != m_fonts.end()) {
        m_resources.DestroyTexture(iterator->second.atlasTexture.id);
        TraceLog(LogLevel::Info,
                 "FONT",
                 TextFormat("[D3D11] Font unloaded (Font ID: %u, Atlas ID: %u)",
                            font.id,
                            iterator->second.atlasTexture.id));
        m_fonts.erase(iterator);
    }

    if (font.id == m_defaultFontId) {
        m_defaultFontId = 0;
    }
    font = {};
}

void QuarkD3D11Renderer::DrawText(const char* text, int x, int y, int fontSize, Color color)
{
    const uint32_t fontId = EnsureDefaultFont();
    const FontData* fontData = GetFontData(IFont{fontId});
    if (fontData) {
        DrawTextWithFontData(*fontData, text, {static_cast<float>(x), static_cast<float>(y)},
                             static_cast<float>(fontSize), 0.0f, color);
    }
}

void QuarkD3D11Renderer::DrawTextEx(IFont font,
                                    const char* text,
                                    Vec2 position,
                                    float fontSize,
                                    float spacing,
                                    Color tint)
{
    const FontData* fontData = GetFontData(font);
    if (fontData) {
        DrawTextWithFontData(*fontData, text, position, fontSize, spacing, tint);
    }
}

Vec2 QuarkD3D11Renderer::MeasureTextEx(IFont font,
                                       const char* text,
                                       float fontSize,
                                       float spacing)
{
    const FontData* fontData = GetFontData(font);
    return fontData ? MeasureTextWithFontData(*fontData, text, fontSize, spacing) : Vec2{};
}

int QuarkD3D11Renderer::MeasureText(const char* text, int fontSize)
{
    const uint32_t fontId = EnsureDefaultFont();
    const FontData* fontData = GetFontData(IFont{fontId});
    return fontData
               ? static_cast<int>(std::round(
                     MeasureTextWithFontData(*fontData, text, static_cast<float>(fontSize), 0.0f).x))
               : 0;
}

ITexture QuarkD3D11Renderer::LoadTexture(const char *filePath)
{
    TraceLog(LogLevel::Trace, "TEXTURE",
             TextFormat("[D3D11] Loading texture from: %s", filePath ? filePath : "<null>"));

    ImageFileData image;
    ITexture texture{};

    if (!LoadImageFile(filePath, image, 4)) {
        TraceLog(LogLevel::Error, "TEXTURE",
                 TextFormat("[D3D11] Failed to load texture: %s", filePath ? filePath : "<null>"));
        return texture;
    }

    texture = m_resources.CreateTexture(
        m_device.Get(), image.pixels.data(), image.width, image.height);

    if (texture.IsValid()) {
        TraceLog(LogLevel::Info, "TEXTURE",
                 TextFormat("[D3D11] Texture loaded successfully: %s (%dx%d, %zu bytes, ID: %u)",
                            filePath ? filePath : "<null>",
                            texture.width,
                            texture.height,
                            image.pixels.size(),
                            texture.id));
    } else {
        TraceLog(LogLevel::Error, "TEXTURE",
                 TextFormat("[D3D11] Failed to upload texture: %s",
                            filePath ? filePath : "<null>"));
    }

    return texture;
}

ITexture QuarkD3D11Renderer::GetRenderTextureTexture(IRenderTexture target)
{
    return target.texture;
}

void QuarkD3D11Renderer::UnloadTexture(ITexture &texture)
{
    if (texture.id != 0) {
        TraceLog(LogLevel::Info, "TEXTURE",
                 TextFormat("[D3D11] Texture unloaded (ID: %u, %dx%d)",
                            texture.id,
                            texture.width,
                            texture.height));
        m_resources.DestroyTexture(texture.id);
    }
    texture = {};
}

IRenderTexture QuarkD3D11Renderer::LoadRenderTexture(int width, int height)
{
    TraceLog(LogLevel::Trace, "RENDER_TARGET",
             TextFormat("[D3D11] Creating render texture: %dx%d", width, height));

    IRenderTexture target = m_resources.CreateRenderTexture(m_device.Get(), width, height);
    if (isRenderTextureValid(target)) {
        TraceLog(LogLevel::Info, "RENDER_TARGET",
                 TextFormat("[D3D11] Render texture created: %dx%d (Target ID: %u, Color Tex ID: %u)",
                            width,
                            height,
                            target.id,
                            target.texture.id));
    }

    return target;
}

void QuarkD3D11Renderer::UnloadRenderTexture(IRenderTexture target)
{
    if (target.id != 0) {
        TraceLog(LogLevel::Info, "RENDER_TARGET",
                 TextFormat("[D3D11] Render texture unloaded (Target ID: %u, Color Tex ID: %u)",
                            target.id,
                            target.texture.id));
        m_resources.DestroyRenderTexture(target.id);
    }
}

ITexture QuarkD3D11Renderer::GenCheckerTexture(int width,
                                               int height,
                                               int cellSize,
                                               Color colorA,
                                               Color colorB)
{
    if (width <= 0 || height <= 0 || cellSize <= 0) {
        return {};
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Color color = ((x / cellSize + y / cellSize) % 2 == 0) ? colorA : colorB;
            const size_t index = (static_cast<size_t>(y) * width + x) * 4;
            pixels[index] = color.r;
            pixels[index + 1] = color.g;
            pixels[index + 2] = color.b;
            pixels[index + 3] = color.a;
        }
    }

    ITexture texture = m_resources.CreateTexture(
        m_device.Get(), pixels.data(), width, height);

    if (texture.IsValid()) {
        TraceLog(LogLevel::Info, "TEXTURE",
                 TextFormat("[D3D11] Generated checker texture: %dx%d (Cell: %dpx, ID: %u)",
                            width,
                            height,
                            cellSize,
                            texture.id));
    }

    return texture;
}

bool QuarkD3D11Renderer::isTextureValid(ITexture &texture)
{
    return texture.IsValid() && m_resources.ShaderResource(texture.id) != nullptr;
}

bool QuarkD3D11Renderer::isRenderTextureValid(IRenderTexture &target)
{
    return target.id != 0 && target.texture.IsValid() &&
           m_resources.RenderTarget(target.id) != nullptr;
}

void QuarkD3D11Renderer::DrawTexture(const ITexture &texture,
                                      float x,
                                      float y,
                                      Color tint)
{
    DrawTexturePro(texture,
                   {0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)},
                   {x, y, static_cast<float>(texture.width), static_cast<float>(texture.height)},
                   {0.0f, 0.0f},
                   0.0f,
                   tint);
}

void QuarkD3D11Renderer::DrawTextureV(const ITexture &texture, Vec2 position, Color tint)
{
    DrawTexture(texture, position.x, position.y, tint);
}

void QuarkD3D11Renderer::DrawTextureEx(const ITexture &texture,
                                       Vec2 position,
                                       float rotation,
                                       float scale,
                                       Color tint)
{
    DrawTexturePro(texture,
                   {0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)},
                   {position.x, position.y, texture.width * scale, texture.height * scale},
                   {texture.width * scale * 0.5f, texture.height * scale * 0.5f},
                   rotation,
                   tint);
}

void QuarkD3D11Renderer::DrawTextureRec(const ITexture &texture,
                                        Rectangle source,
                                        Vec2 position,
                                        Color tint)
{
    DrawTexturePro(texture, source, {position.x, position.y, source.width, source.height},
                   {0.0f, 0.0f}, 0.0f, tint);
}

void QuarkD3D11Renderer::DrawTexturePro(ITexture texture,
                                        Rectangle source,
                                        Rectangle destination,
                                        Vec2 origin,
                                        float rotation,
                                        Color tint)
{
    if (isTextureValid(texture)) {
        m_commands.DrawTextureQuad(texture,
                                   source,
                                   destination,
                                   origin,
                                   rotation,
                                   tint,
                                   m_width,
                                   m_height);
    }
}

void QuarkD3D11Renderer::DrawTextureTiled(ITexture texture,
                                          float scale,
                                          Vec2 offset,
                                          Color tint)
{
    if (!isTextureValid(texture) || scale <= 0.0f) {
        return;
    }

    const int columns = static_cast<int>(std::ceil(m_width / (texture.width * scale))) + 1;
    const int rows = static_cast<int>(std::ceil(m_height / (texture.height * scale))) + 1;

    for (int y = -1; y < rows; ++y) {
        for (int x = -1; x < columns; ++x) {
            DrawTexture(texture,
                        offset.x + x * texture.width * scale,
                        offset.y + y * texture.height * scale,
                        tint);
        }
    }
}

void QuarkD3D11Renderer::DrawTextureNPatch(ITexture texture,
                                           Rectangle source,
                                           Rectangle destination,
                                           Vec2 origin,
                                           float rotation,
                                           Color tint)
{
    DrawTexturePro(texture, source, destination, origin, rotation, tint);
}

void QuarkD3D11Renderer::BeginMode2D(const Camera2D& camera)
{
    m_commands.BeginMode2D(camera);
}

void QuarkD3D11Renderer::EndMode2D()
{
    m_commands.EndMode2D();
}

void QuarkD3D11Renderer::BeginTextureMode(IRenderTexture target)
{
    m_commands.BeginTextureMode(target);
}

void QuarkD3D11Renderer::EndTextureMode()
{
    m_commands.EndTextureMode(m_width, m_height);
}

QuarkD3D11Renderer::ShaderProgramData *QuarkD3D11Renderer::GetShaderProgram(uint32_t shaderId)
{
    if (shaderId == 0) {
        return nullptr;
    }

    const auto iterator = m_shaderPrograms.find(shaderId);
    return iterator == m_shaderPrograms.end() ? nullptr : &iterator->second;
}

void QuarkD3D11Renderer::BuildShaderProgram(ShaderProgramData &program,
                                            const char *vsSource,
                                            const char *fsSource)
{
    ID3D11Device *device = m_device.Get();

    const auto vertexBytecode = m_shaderCompiler.Compile(vsSource, "main", "vs_5_0");
    const auto pixelBytecode = m_shaderCompiler.Compile(fsSource, "main", "ps_5_0");

    d3d11::ThrowIfFailed(device->CreateVertexShader(vertexBytecode->GetBufferPointer(),
                                                    vertexBytecode->GetBufferSize(), nullptr,
                                                    &program.vertexShader),
                         "ID3D11Device::CreateVertexShader");
    d3d11::ThrowIfFailed(device->CreatePixelShader(pixelBytecode->GetBufferPointer(),
                                                   pixelBytecode->GetBufferSize(), nullptr,
                                                   &program.pixelShader),
                         "ID3D11Device::CreatePixelShader");

    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> vertexReflector = nullptr;
    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> pixelReflector = nullptr;

    d3d11::ThrowIfFailed(D3DReflect(vertexBytecode->GetBufferPointer(),
                                    vertexBytecode->GetBufferSize(),
                                    IID_ID3D11ShaderReflection,
                                    reinterpret_cast<void **>(vertexReflector.GetAddressOf())),
                         "D3DReflect");
    d3d11::ThrowIfFailed(D3DReflect(pixelBytecode->GetBufferPointer(),
                                    pixelBytecode->GetBufferSize(),
                                    IID_ID3D11ShaderReflection,
                                    reinterpret_cast<void **>(pixelReflector.GetAddressOf())),
                         "D3DReflect");

    D3D11_SHADER_DESC vertexDescription{};
    vertexReflector->GetDesc(&vertexDescription);

    std::vector<D3D11_INPUT_ELEMENT_DESC> inputElements;
    inputElements.reserve(vertexDescription.InputParameters);

    UINT byteOffset = 0;
    for (UINT index = 0; index < vertexDescription.InputParameters; ++index) {
        D3D11_SIGNATURE_PARAMETER_DESC signature{};
        vertexReflector->GetInputParameterDesc(index, &signature);

        const UINT components = CountMaskComponents(signature.Mask);
        D3D11_INPUT_ELEMENT_DESC element{};
        element.SemanticName = signature.SemanticName;
        element.SemanticIndex = signature.SemanticIndex;
        element.Format = SignatureFormat(signature.ComponentType, components);
        element.InputSlot = 0;
        element.AlignedByteOffset = byteOffset;
        element.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        element.InstanceDataStepRate = 0;
        inputElements.push_back(element);

        program.attributes[signature.SemanticName] = static_cast<int>(index);

        if (SemanticEquals(signature.SemanticName, "POSITION")) {
            program.positionOffset = byteOffset;
            program.hasPosition = true;
        } else if (SemanticEquals(signature.SemanticName, "TEXCOORD") &&
                   program.texCoordOffset == 0xFFFFFFFFu) {
            program.texCoordOffset = byteOffset;
        } else if (SemanticEquals(signature.SemanticName, "COLOR")) {
            program.colorOffset = byteOffset;
        }

        byteOffset += components * sizeof(float);
    }

    program.strideBytes = byteOffset;

    d3d11::ThrowIfFailed(
        device->CreateInputLayout(inputElements.data(),
                                  static_cast<UINT>(inputElements.size()),
                                  vertexBytecode->GetBufferPointer(),
                                  vertexBytecode->GetBufferSize(),
                                  &program.inputLayout),
        "ID3D11Device::CreateInputLayout shader");

    std::unordered_map<std::string, UINT> constantBufferBases;
    UINT totalConstantSize = 0;

    const auto reflectStage =
        [&](ID3D11ShaderReflection *reflector) {
            if (!reflector) {
                return;
            }

            D3D11_SHADER_DESC stageDescription{};
            reflector->GetDesc(&stageDescription);

            for (UINT bufferIndex = 0; bufferIndex < stageDescription.ConstantBuffers;
                 ++bufferIndex) {
                auto *constantBuffer = reflector->GetConstantBufferByIndex(bufferIndex);
                D3D11_SHADER_BUFFER_DESC bufferDescription{};
                if (FAILED(constantBuffer->GetDesc(&bufferDescription)) ||
                    bufferDescription.Type != D3D11_CT_CBUFFER) {
                    continue;
                }

                const std::string bufferName =
                    bufferDescription.Name ? bufferDescription.Name : "";
                UINT baseOffset = 0;
                const auto baseIterator = constantBufferBases.find(bufferName);
                if (baseIterator != constantBufferBases.end()) {
                    baseOffset = baseIterator->second;
                } else {
                    baseOffset = AlignTo16(totalConstantSize);
                    totalConstantSize = baseOffset + AlignTo16(bufferDescription.Size);
                    constantBufferBases.emplace(bufferName, baseOffset);
                }

                for (UINT variableIndex = 0; variableIndex < bufferDescription.Variables;
                     ++variableIndex) {
                    auto *variable = constantBuffer->GetVariableByIndex(variableIndex);
                    D3D11_SHADER_VARIABLE_DESC variableDescription{};
                    if (FAILED(variable->GetDesc(&variableDescription))) {
                        continue;
                    }

                    const std::string uniformName =
                        variableDescription.Name ? variableDescription.Name : "";
                    if (uniformName.empty() || program.uniforms.count(uniformName)) {
                        continue;
                    }

                    ShaderUniformInfo info{};
                    info.name = uniformName;
                    info.offset = baseOffset + variableDescription.StartOffset;
                    info.size = variableDescription.Size;
                    program.uniformInfos.push_back(std::move(info));
                    program.uniforms.emplace(uniformName,
                                             static_cast<int>(program.uniformInfos.size()) - 1);
                }
            }
        };

    reflectStage(vertexReflector.Get());
    reflectStage(pixelReflector.Get());

    if (totalConstantSize > 0) {
        EnsureConstantCapacity(program, AlignTo16(totalConstantSize));
        TraceLog(LogLevel::Trace, "SHADER",
                 TextFormat("[D3D11] Reflected shader constants (%u bytes, %zu uniforms)",
                            program.constantBufferSize,
                            program.uniformInfos.size()));
    }
}

void QuarkD3D11Renderer::EnsureConstantCapacity(ShaderProgramData &program, size_t byteCount)
{
    const size_t aligned = static_cast<size_t>(AlignTo16(static_cast<UINT>(
        byteCount > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<UINT>(byteCount))));

    if (aligned > program.constantStaging.size()) {
        const size_t grown = std::max(aligned, program.constantStaging.size() * 2);
        program.constantStaging.resize(grown, 0);
    }

    if (program.constantStaging.size() > program.constantBufferSize &&
        m_device.Get() != nullptr) {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(program.constantStaging.size());
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        d3d11::ThrowIfFailed(m_device.Get()->CreateBuffer(&description, nullptr,
                                                          &program.constantBuffer),
                             "ID3D11Device::CreateBuffer shader constants");
        program.constantBufferSize = description.ByteWidth;
        program.dirty = true;
    }
}

void QuarkD3D11Renderer::UploadConstantBuffer(ShaderProgramData &program)
{
    ID3D11DeviceContext *context = m_device.Context();
    if (!context || !program.constantBuffer || !program.dirty || program.constantStaging.empty()) {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(program.constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                            &mapped))) {
        TraceLog(LogLevel::Warn, "SHADER", "[D3D11] Failed to map shader constant buffer.");
        return;
    }

    const size_t bytes = std::min<size_t>(program.constantStaging.size(),
                                          program.constantBufferSize);
    std::memcpy(mapped.pData, program.constantStaging.data(), bytes);
    context->Unmap(program.constantBuffer.Get(), 0);
    program.dirty = false;
}

void QuarkD3D11Renderer::StoreUniformValue(ShaderProgramData &program, int locIndex,
                                           int uniformType, const void *value,
                                           size_t elementBytes, int count)
{
    if (locIndex < 0 || value == nullptr || count <= 0 || elementBytes == 0) {
        return;
    }

    if (locIndex >= static_cast<int>(program.uniformInfos.size())) {
        TraceLog(LogLevel::Warn, "SHADER",
                 TextFormat("[D3D11] Ignoring uniform write to unknown location %d.", locIndex));
        return;
    }

    const ShaderUniformInfo &info = program.uniformInfos[static_cast<size_t>(locIndex)];
    const size_t totalBytes = static_cast<size_t>(count) * elementBytes;

    EnsureConstantCapacity(program, static_cast<size_t>(info.offset) + totalBytes);
    std::memcpy(program.constantStaging.data() + info.offset, value, totalBytes);

    auto &storage = program.uniformValues[locIndex];
    storage.assign(static_cast<const uint8_t *>(value),
                   static_cast<const uint8_t *>(value) + totalBytes);
    program.uniformTypes[locIndex] = uniformType;
    program.dirty = true;
}

void QuarkD3D11Renderer::RegisterShaderTexture(ShaderProgramData &program, int locIndex,
                                               uint32_t textureId)
{
    if (locIndex < 0) {
        return;
    }

    if (textureId != 0 && program.textureSlots.find(locIndex) == program.textureSlots.end()) {
        constexpr UINT maxShaderSlots = 7;
        if (program.nextTextureSlot > maxShaderSlots) {
            TraceLog(LogLevel::Warn, "SHADER",
                     "[D3D11] Shader texture slot limit reached (slots 1..7 available).");
            program.textureSlots[locIndex] = maxShaderSlots;
        } else {
            program.textureSlots[locIndex] = program.nextTextureSlot++;
        }
    }

    program.textureIds[locIndex] = textureId;
    program.dirty = true;
}

D3D11CommandContext::ShaderOverride QuarkD3D11Renderer::BuildShaderOverride(uint32_t shaderId)
{
    D3D11CommandContext::ShaderOverride shaderOverride{};

    ShaderProgramData *program = GetShaderProgram(shaderId);
    if (!program || !program->vertexShader || !program->pixelShader ||
        !program->inputLayout || !program->hasPosition) {
        return shaderOverride;
    }

    UploadConstantBuffer(*program);

    shaderOverride.vertexShader = program->vertexShader.Get();
    shaderOverride.pixelShader = program->pixelShader.Get();
    shaderOverride.inputLayout = program->inputLayout.Get();
    shaderOverride.constantBuffer =
        program->constantBuffer ? program->constantBuffer.Get() : nullptr;
    shaderOverride.strideBytes = program->strideBytes;
    shaderOverride.positionOffset = program->positionOffset;
    shaderOverride.texCoordOffset = program->texCoordOffset;
    shaderOverride.colorOffset = program->colorOffset;

    for (const auto &[location, textureId] : program->textureIds) {
        if (textureId == 0) {
            continue;
        }

        UINT slot = 0;
        const auto slotIterator = program->textureSlots.find(location);
        if (slotIterator != program->textureSlots.end()) {
            slot = slotIterator->second;
        } else if (shaderOverride.shaderResources[0] == nullptr) {
            slot = 0;
        } else {
            continue;
        }

        if (slot < 8) {
            shaderOverride.shaderResources[slot] = m_resources.ShaderResource(textureId);
        }
    }

    if (!shaderOverride.shaderResources[0] && m_whiteShaderTexture.IsValid()) {
        shaderOverride.shaderResources[0] = m_resources.ShaderResource(m_whiteShaderTexture.id);
    }

    return shaderOverride;
}

void QuarkD3D11Renderer::BeginShaderMode(const Shader &shader)
{
    if (shader.id == 0) {
        EndShaderMode();
        return;
    }

    if (m_shaderPrograms.find(shader.id) == m_shaderPrograms.end()) {
        TraceLog(LogLevel::Warn, "SHADER",
                 TextFormat("[D3D11] BeginShaderMode ignored unknown shader (ID: %u)", shader.id));
        return;
    }

    m_currentShaderId = shader.id;
    const D3D11CommandContext::ShaderOverride shaderOverride = BuildShaderOverride(shader.id);
    if (!shaderOverride.Active()) {
        m_currentShaderId = 0;
        m_commands.SetShaderOverride({});
        TraceLog(LogLevel::Warn, "SHADER",
                 TextFormat("[D3D11] BeginShaderMode ignored incomplete shader (ID: %u)",
                            shader.id));
        return;
    }

    m_commands.SetShaderOverride(shaderOverride);
}

void QuarkD3D11Renderer::EndShaderMode()
{
    if (m_currentShaderId == 0) {
        return;
    }

    m_currentShaderId = 0;
    m_commands.SetShaderOverride({});
}

Shader QuarkD3D11Renderer::LoadShader(const char *vsFileName, const char *fsFileName)
{
    TraceLog(LogLevel::Trace, "SHADER",
             TextFormat("[D3D11] Loading shader files: VS='%s', FS='%s'",
                        vsFileName ? vsFileName : "<default>",
                        fsFileName ? fsFileName : "<default>"));

    std::string vsSource;
    std::string fsSource;

    if (vsFileName && !ReadTextFile(vsFileName, vsSource)) {
        TraceLog(LogLevel::Error, "SHADER",
                 TextFormat("[D3D11] Failed to open vertex shader file: %s", vsFileName));
        return Shader{};
    }

    if (fsFileName && !ReadTextFile(fsFileName, fsSource)) {
        TraceLog(LogLevel::Error, "SHADER",
                 TextFormat("[D3D11] Failed to open fragment shader file: %s", fsFileName));
        return Shader{};
    }

    if (!vsFileName && !fsFileName) {
        return Shader{};
    }

    return LoadShaderFromMemory(vsFileName ? vsSource.c_str() : kDefaultHlslVertexSource,
                                fsFileName ? fsSource.c_str() : kDefaultHlslPixelSource);
}

Shader QuarkD3D11Renderer::LoadShaderFromMemory(const char *vsSource, const char *fsSource)
{
    if (!vsSource && !fsSource) {
        return Shader{};
    }

    const uint32_t shaderId = m_nextShaderId++;
    ShaderProgramData &program = m_shaderPrograms[shaderId];

    try {
        BuildShaderProgram(program,
                           vsSource ? vsSource : kDefaultHlslVertexSource,
                           fsSource ? fsSource : kDefaultHlslPixelSource);
    } catch (const std::exception &exception) {
        TraceLog(LogLevel::Error, "SHADER",
                 TextFormat("[D3D11] Shader creation failed (ID: %u): %s",
                            shaderId,
                            exception.what()));
        m_shaderPrograms.erase(shaderId);
        return Shader{};
    }

    TraceLog(LogLevel::Info, "SHADER",
             TextFormat("[D3D11] Shader created successfully (ID: %u, Attributes: %zu, "
                        "Uniforms: %zu)",
                        shaderId,
                        program.attributes.size(),
                        program.uniforms.size()));

    return Shader{shaderId};
}

void QuarkD3D11Renderer::UnloadShader(Shader &shader)
{
    if (shader.id == 0) {
        return;
    }

    if (m_shaderPrograms.erase(shader.id) > 0) {
        TraceLog(LogLevel::Info, "SHADER",
                 TextFormat("[D3D11] Shader unloaded (ID: %u)", shader.id));
    }

    if (m_currentShaderId == shader.id) {
        m_currentShaderId = 0;
        m_commands.SetShaderOverride({});
    }

    shader = Shader{};
}

bool QuarkD3D11Renderer::isShaderValid(Shader &shader)
{
    return shader.id != 0 && m_shaderPrograms.find(shader.id) != m_shaderPrograms.end();
}

int QuarkD3D11Renderer::GetShaderLocation(const Shader &shader, const char *uniformName)
{
    if (!uniformName || shader.id == 0) {
        return -1;
    }

    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return -1;
    }

    const auto locationIterator = program->uniforms.find(uniformName);
    if (locationIterator != program->uniforms.end()) {
        return locationIterator->second;
    }

    ShaderUniformInfo info{};
    info.name = uniformName;
    info.offset = AlignTo16(static_cast<UINT>(program->constantStaging.size()));
    info.size = sizeof(float) * 4;
    program->uniformInfos.push_back(std::move(info));

    const int location = static_cast<int>(program->uniformInfos.size()) - 1;
    program->uniforms.emplace(uniformName, location);

    TraceLog(LogLevel::Trace, "SHADER",
             TextFormat("[D3D11] Allocated virtual uniform '%s' (Location: %d)",
                        uniformName,
                        location));

    return location;
}

int QuarkD3D11Renderer::GetShaderLocation(const Shader &shader, ShaderLocationIndex locIndex)
{
    if (locIndex < SHADER_LOC_VERTEX_POSITION || locIndex >= SHADER_LOC_COUNT) {
        return -1;
    }

    return GetShaderLocation(shader, ShaderLocationNames[locIndex]);
}

int QuarkD3D11Renderer::GetShaderAttributeLocation(const Shader &shader, const char *attribName)
{
    if (!attribName || shader.id == 0) {
        return -1;
    }

    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return -1;
    }

    const auto attributeIterator = program->attributes.find(attribName);
    if (attributeIterator != program->attributes.end()) {
        return attributeIterator->second;
    }

    const int newLocation = static_cast<int>(program->attributes.size());
    program->attributes.emplace(attribName, newLocation);
    return newLocation;
}

void QuarkD3D11Renderer::SetShaderValue(const Shader &shader, int locIndex, float value)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_FLOAT, &value, sizeof(value), 1);
    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValue(const Shader &shader, int locIndex, int value)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_INT, &value, sizeof(value), 1);
    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValue(const Shader &shader, int locIndex, const Color &value)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    const float rgba[4] = {value.r / 255.0f, value.g / 255.0f, value.b / 255.0f,
                           value.a / 255.0f};
    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_VEC4, rgba, sizeof(float), 4);
    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValue(const Shader &shader, int locIndex, const Vec2 &value)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    const float vec[2] = {value.x, value.y};
    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_VEC2, vec, sizeof(float), 2);
    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValue(const Shader &shader, int locIndex, const Vec3 &value)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    const float vec[3] = {value.x, value.y, value.z};
    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_VEC3, vec, sizeof(float), 3);
    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValue(const Shader &shader, int locIndex, const Vec4 &value)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    const float vec[4] = {value.x, value.y, value.z, value.w};
    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_VEC4, vec, sizeof(float), 4);
    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValueMatrix(const Shader &shader, int locIndex,
                                               const float *mat)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program || mat == nullptr) {
        return;
    }

    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_FLOAT, mat, sizeof(float), 16);
    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValueMatrix(const Shader &shader, int locIndex,
                                              const Matrix &mat)
{
    SetShaderValueMatrix(shader, locIndex, mat.m);
}

void QuarkD3D11Renderer::SetShaderValueSampler(const Shader &shader, int locIndex,
                                               int textureUnit)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureUnit,
                      sizeof(int), 1);
    RegisterShaderTexture(*program, locIndex, static_cast<uint32_t>(textureUnit));
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
    UploadConstantBuffer(*program);
}

void QuarkD3D11Renderer::SetShaderValue(const Shader &shader, int locIndex, const void *value,
                                        int uniformType)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program || value == nullptr) {
        return;
    }

    switch (uniformType) {
        case SHADER_UNIFORM_FLOAT:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(float), 1);
            break;
        case SHADER_UNIFORM_VEC2:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(float), 2);
            break;
        case SHADER_UNIFORM_VEC3:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(float), 3);
            break;
        case SHADER_UNIFORM_VEC4:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(float), 4);
            break;
        case SHADER_UNIFORM_INT:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int), 1);
            break;
        case SHADER_UNIFORM_SAMPLER2D: {
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int), 1);
            int textureId = 0;
            std::memcpy(&textureId, value, sizeof(textureId));
            RegisterShaderTexture(*program, locIndex, static_cast<uint32_t>(textureId));
            break;
        }
        case SHADER_UNIFORM_IVEC2:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int), 2);
            break;
        case SHADER_UNIFORM_IVEC3:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int), 3);
            break;
        case SHADER_UNIFORM_IVEC4:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int), 4);
            break;
        default:
            break;
    }

    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValueV(const Shader &shader, int locIndex, const void *value,
                                         int uniformType, int count)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program || value == nullptr || count <= 0) {
        return;
    }

    switch (uniformType) {
        case SHADER_UNIFORM_FLOAT:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(float), count);
            break;
        case SHADER_UNIFORM_VEC2:
            StoreUniformValue(*program, locIndex, uniformType, value,
                              sizeof(float) * 2, count);
            break;
        case SHADER_UNIFORM_VEC3:
            StoreUniformValue(*program, locIndex, uniformType, value,
                              sizeof(float) * 3, count);
            break;
        case SHADER_UNIFORM_VEC4:
            StoreUniformValue(*program, locIndex, uniformType, value,
                              sizeof(float) * 4, count);
            break;
        case SHADER_UNIFORM_INT:
        case SHADER_UNIFORM_SAMPLER2D:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int), count);
            break;
        case SHADER_UNIFORM_IVEC2:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int) * 2, count);
            break;
        case SHADER_UNIFORM_IVEC3:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int) * 3, count);
            break;
        case SHADER_UNIFORM_IVEC4:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int) * 4, count);
            break;
        default:
            break;
    }

    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValueTexture(const Shader &shader, int locIndex,
                                               const ITexture &texture)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    const int textureUnit = static_cast<int>(texture.id);
    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureUnit,
                      sizeof(int), 1);
    RegisterShaderTexture(*program, locIndex, texture.id);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValueTextureUnit(const Shader &shader, int locIndex,
                                                   const ITexture &texture, int textureUnit)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    const int textureId = texture.valid ? static_cast<int>(texture.id) : textureUnit;
    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureId,
                      sizeof(int), 1);
    RegisterShaderTexture(*program, locIndex, static_cast<uint32_t>(textureId));
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

} // namespace qc
#endif
