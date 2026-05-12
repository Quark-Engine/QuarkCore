#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/Quark3D.hpp"

#include <GL/glew.h>
#include <png.h>
#include <algorithm>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <array>
#include <chrono>
#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace qc {
namespace {

struct RendererState {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint whiteTexture = 0;
    GLuint currentTexture = 0;
    GLuint currentShaderProgram = 0;
    GLuint defaultShaderProgram = 0;
    GLint screenSizeLocation = -1;
    GLint samplerLocation = -1;
    GLuint currentFbo = 0;
    int width = 0;
    int height = 0;
    int targetFps = 60;
    float frameTime = 0.0f;
    std::uint64_t lastFrameCounter = 0;
    bool drawing = false;
    bool shouldClose = false;
    LogLevel minimumLogLevel = LogLevel::Trace;
    std::vector<Vertex> batchVertices;
    std::array<bool, SDL_SCANCODE_COUNT> currentKeys{};
    std::array<bool, SDL_SCANCODE_COUNT> previousKeys{};
    std::array<bool, 8> mouseButtons{};
    std::array<bool, 8> previousMouseButtons{};
    Vec2 mousePosition{};
    Vec2 mouseWheel{};
    std::vector<Event> events;
    SDL_Event nativeEvent{};
    std::size_t nextEventIndex = 0;
    bool eventsReady = false;
};

Camera2D gCamera2D;
bool gCamera2DActive = false;

struct PngImageData {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

RendererState gRenderer;

namespace {
struct Model3DState {
    std::vector<Model> loadedModels;
    unsigned int nextModelId = 1;
    Mat4 viewMatrix = Mat4::identity();
    Mat4 projectionMatrix = Mat4::identity();
    GLuint shader3D = 0;
    GLint modelLoc = -1;
    GLint viewLoc = -1;
    GLint projLoc = -1;
    GLint samplerLoc = -1;
    GLint lightPosLoc = -1;
    GLint colorLoc = -1;
    GLuint whiteTexture = 0;

    GLuint planeVAO = 0, planeVBO = 0, planeEBO = 0;
    int planeIndexCount = 0;
    GLuint cubeVAO = 0, cubeVBO = 0, cubeEBO = 0;
    int cubeIndexCount = 0;
    GLuint sphereVAO = 0, sphereVBO = 0, sphereEBO = 0;
    int sphereIndexCount = 0;

    GLuint lineVAO = 0, lineVBO = 0;
    std::vector<Vertex3D> lineVertices;
    GLuint triVAO = 0, triVBO = 0;
    std::vector<Vertex3D> triVertices;
    Color currentLineColor = { 255, 255, 255, 255 };
    Color currentTriColor = { 255, 255, 255, 255 };
    Vec3 lightPosition{5.0f, 5.0f, 5.0f};
    bool initialized = false;
};
Model3DState g3DState;

std::vector<Mat4> gMatrixStack;
Mat4 gCurrentMatrix = Mat4::identity();

Font gDefaultFont;
FT_Library gFreeTypeLibrary = nullptr;
bool gFreeTypeInitialized = false;

int gLastKeyPressed = 0;
int gLastCharPressed = 0;
KeyboardKey gExitKey = KeyboardKey::Escape;
Vec2 gMousePreviousPosition = {0.0f, 0.0f};
bool gCursorHidden = false;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable:4611)
#endif
static bool PngSafeInit(png_structp png, FILE* file) {
    if (setjmp(png_jmpbuf(png))) return false;
    png_init_io(png, file);
    return true;
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // namespace

constexpr std::size_t kMaxBatchVertices = 8192;

void RefreshViewport();
void UpdateInputFromEvents();
bool LoadPngImage(const char* filePath, PngImageData& image);
GLuint CreateTextureFromRgba(const std::uint8_t* pixels, int width, int height);
void EnsureBatchTexture(GLuint textureId);
void PushVertex(const Vertex& vertex);

[[noreturn]] void Fail(const std::string& message) {
    throw std::runtime_error(message);
}

const char* ToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::None: return "NONE";
        default: return "UNKNOWN";
    }
}

std::string FormatTimeNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &nowTime);
#else
    localtime_r(&nowTime, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%H:%M:%S");
    return stream.str();
}

void WriteLog(LogLevel level, const char* type, const std::string& message) {
    if (level < gRenderer.minimumLogLevel || level == LogLevel::None) {
        return;
    }

    std::cout
        << '[' << FormatTimeNow() << ']'
        << '[' << ToString(level) << ']'
        << '[' << type << "] "
        << message
        << '\n';
}

void CopyText(char* destination, std::size_t capacity, const char* source) {
    if (destination == nullptr || capacity == 0) {
        return;
    }

    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }

#if defined(_MSC_VER)
    strncpy_s(destination, capacity, source, _TRUNCATE);
#else
    std::snprintf(destination, capacity, "%s", source);
#endif
}

GLuint CompileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE) {
        return shader;
    }

    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(length), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    glDeleteShader(shader);
    Fail("Shader compilation failed: " + log);
}

GLuint CreateProgram() {
    const GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, kVertexShaderSource);
    const GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, kFragmentShaderSource);

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_TRUE) {
        return program;
    }

    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(length), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    glDeleteProgram(program);
    Fail("Program link failed: " + log);
}

void EnsureInitialized() {
    if (gRenderer.window == nullptr) {
        Fail("QuarkCore is not initialized. Call InitWindow() first.");
    }
}

std::array<float, 4> ToNormalizedColor(Color color) {
    constexpr float inv = 1.0f / 255.0f;
    return {color.r * inv, color.g * inv, color.b * inv, color.a * inv};
}

void RefreshViewport() {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(gRenderer.window, &width, &height);
    gRenderer.width = width;
    gRenderer.height = height;
    glViewport(0, 0, width, height);
}

bool FileExists(const char* filePath) {
    if (filePath == nullptr) {
        return false;
    }
    std::ifstream file(filePath, std::ios::binary);
    return file.good();
}

bool InitializeFreeType() {
    if (gFreeTypeInitialized) {
        return true;
    }

    if (FT_Init_FreeType(&gFreeTypeLibrary) != 0) {
        WriteLog(LogLevel::Error, "FREETYPE", "Failed to initialize FreeType library");
        return false;
    }

    gFreeTypeInitialized = true;
    return true;
}

const char* GetDefaultSystemFontPath() {
#if defined(_WIN32)
    static const char* paths[] = {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/verdana.ttf",
    };
#elif defined(__APPLE__)
    static const char* paths[] = {
        "/System/Library/Fonts/SFNS.ttf",
        "/Library/Fonts/Arial.ttf",
        "/Library/Fonts/Helvetica.ttf",
    };
#else
    static const char* paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
    };
#endif

    for (const char* path : paths) {
        if (FileExists(path)) {
            return path;
        }
    }

    return nullptr;
}

bool LoadFontFromFile(Font& font, const char* filePath, int pointSize) {
    if (!InitializeFreeType()) {
        return false;
    }

    FT_Face face = nullptr;
    if (FT_New_Face(gFreeTypeLibrary, filePath, 0, &face) != 0) {
        WriteLog(LogLevel::Error, "FREETYPE", std::string("Failed to open font file: ") + filePath);
        return false;
    }

    if (FT_Select_Charmap(face, FT_ENCODING_UNICODE) != 0) {
        WriteLog(LogLevel::Error, "FREETYPE", "Failed to select Unicode charmap");
        FT_Done_Face(face);
        return false;
    }

    if (FT_Set_Pixel_Sizes(face, 0, pointSize) != 0) {
        WriteLog(LogLevel::Error, "FREETYPE", "Failed to set font pixel size");
        FT_Done_Face(face);
        return false;
    }

    constexpr int textureWidth = 1024;
    constexpr int textureHeight = 1024;
    std::vector<std::uint8_t> pixels(textureWidth * textureHeight * 4, 0);
    int penX = 1;
    int penY = 1;
    int rowHeight = 0;

    for (unsigned char c = 32; c < 127; ++c) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) {
            continue;
        }

        FT_GlyphSlot slot = face->glyph;
        int glyphWidth = static_cast<int>(slot->bitmap.width);
        int glyphHeight = static_cast<int>(slot->bitmap.rows);

        if (penX + glyphWidth + 1 > textureWidth) {
            penX = 1;
            penY += rowHeight + 1;
            rowHeight = 0;
        }

        if (penY + glyphHeight + 1 > textureHeight) {
            WriteLog(LogLevel::Error, "FREETYPE", "Font atlas overflow for default font");
            FT_Done_Face(face);
            return false;
        }

        for (int row = 0; row < glyphHeight; ++row) {
            for (int col = 0; col < glyphWidth; ++col) {
                const std::size_t dst = ((penY + row) * textureWidth + (penX + col)) * 4;
                const std::uint8_t alpha = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
                pixels[dst + 0] = 255;
                pixels[dst + 1] = 255;
                pixels[dst + 2] = 255;
                pixels[dst + 3] = alpha;
            }
        }

        const float u = static_cast<float>(penX) / textureWidth;
        const float v = static_cast<float>(penY) / textureHeight;
        const float uWidth = glyphWidth > 0 ? static_cast<float>(glyphWidth) / textureWidth : 0.0f;
        const float vHeight = glyphHeight > 0 ? static_cast<float>(glyphHeight) / textureHeight : 0.0f;

        FontGlyph& glyph = font.glyphs[c - 32];
        glyph.uv = Rectangle{u, v, uWidth, vHeight};
        glyph.advanceX = static_cast<float>(slot->advance.x) / 64.0f;
        glyph.offsetX = static_cast<float>(slot->bitmap_left);
        glyph.offsetY = static_cast<float>(slot->bitmap_top);
        glyph.width = glyphWidth;
        glyph.height = glyphHeight;

        penX += glyphWidth + 1;
        rowHeight = std::max(rowHeight, glyphHeight);
    }

    const unsigned int textureId = CreateTextureFromRgba(pixels.data(), textureWidth, textureHeight);
    if (textureId == 0) {
        WriteLog(LogLevel::Error, "FREETYPE", "Failed to create font atlas texture");
        FT_Done_Face(face);
        return false;
    }

    font.textureId = textureId;
    font.baseSize = pointSize;
    font.ascent = static_cast<int>(face->size->metrics.ascender / 64);
    font.descent = static_cast<int>(face->size->metrics.descender / 64);
    font.lineHeight = static_cast<int>(face->size->metrics.height / 64);
    font.lineGap = font.lineHeight - (font.ascent - font.descent);
    font.valid = true;

    FT_Done_Face(face);
    return true;
}

bool EnsureDefaultFontLoaded() {
    if (gDefaultFont.valid) {
        return true;
    }

    const char* fontPath = GetDefaultSystemFontPath();
    if (fontPath == nullptr) {
        WriteLog(LogLevel::Warn, "FREETYPE", "Default system font not found");
        return false;
    }

    return LoadFontFromFile(gDefaultFont, fontPath, 32);
}

void PushTexturedQuad(GLuint textureId, Rectangle uv, float x, float y, float width, float height, Color color) {
    EnsureBatchTexture(textureId);
    const auto normalized = ToNormalizedColor(color);

    PushVertex({x, y, uv.x, uv.y, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x + width, y, uv.x + uv.width, uv.y, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x + width, y + height, uv.x + uv.width, uv.y + uv.height, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x, y, uv.x, uv.y, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x + width, y + height, uv.x + uv.width, uv.y + uv.height, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x, y + height, uv.x, uv.y + uv.height, normalized[0], normalized[1], normalized[2], normalized[3]});
}

GLuint CreateTextureFromRgba(const std::uint8_t* pixels, int width, int height) {
    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return textureId;
}

void FlushBatch() {
    if (gRenderer.batchVertices.empty()) {
        return;
    }

    glUseProgram(gRenderer.currentShaderProgram);

    GLint screenSizeLoc = glGetUniformLocation(gRenderer.currentShaderProgram, "uScreenSize");
    GLint samplerLoc    = glGetUniformLocation(gRenderer.currentShaderProgram, "uTexture");

    if (screenSizeLoc >= 0)
        glUniform2f(screenSizeLoc, static_cast<float>(gRenderer.width), static_cast<float>(gRenderer.height));
    if (samplerLoc >= 0)
        glUniform1i(samplerLoc, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gRenderer.currentTexture != 0 ? gRenderer.currentTexture : gRenderer.whiteTexture);
    glBindVertexArray(gRenderer.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gRenderer.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(gRenderer.batchVertices.size() * sizeof(Vertex)),
        gRenderer.batchVertices.data(),
        GL_DYNAMIC_DRAW
    );
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(gRenderer.batchVertices.size()));
    gRenderer.batchVertices.clear();
}

void EnsureBatchTexture(GLuint textureId) {
    const GLuint resolvedTexture = textureId != 0 ? textureId : gRenderer.whiteTexture;
    if (gRenderer.currentTexture == 0) {
        gRenderer.currentTexture = resolvedTexture;
    }

    if (resolvedTexture != gRenderer.currentTexture || gRenderer.batchVertices.size() >= kMaxBatchVertices) {
        FlushBatch();
        gRenderer.currentTexture = resolvedTexture;
    }
}

void PushVertex(const Vertex& vertex) {
    if (gRenderer.batchVertices.size() >= kMaxBatchVertices) {
        FlushBatch();
    }

    Vertex v = vertex;
    if (gCamera2DActive) {
        Vec2 screenPos = GetWorldToScreen2D({v.x, v.y}, gCamera2D);
        v.x = screenPos.x;
        v.y = screenPos.y;
    }
    gRenderer.batchVertices.push_back(v);
}

void PushQuad(GLuint textureId, float x, float y, float width, float height, Color color) {
    EnsureBatchTexture(textureId);
    const auto normalized = ToNormalizedColor(color);

    PushVertex({x, y, 0.0f, 0.0f, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x + width, y, 1.0f, 0.0f, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x + width, y + height, 1.0f, 1.0f, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x, y, 0.0f, 0.0f, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x + width, y + height, 1.0f, 1.0f, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x, y + height, 0.0f, 1.0f, normalized[0], normalized[1], normalized[2], normalized[3]});
}

void PushCircle(float centerX, float centerY, float radius, Color color) {
    EnsureBatchTexture(0);
    const auto normalized = ToNormalizedColor(color);
    constexpr int segments = 48;

    for (int i = 0; i < segments; ++i) {
        const float angleA = static_cast<float>(i) / static_cast<float>(segments) * 6.28318530718f;
        const float angleB = static_cast<float>(i + 1) / static_cast<float>(segments) * 6.28318530718f;

        PushVertex({centerX, centerY, 0.5f, 0.5f, normalized[0], normalized[1], normalized[2], normalized[3]});
        PushVertex({
            centerX + std::cos(angleA) * radius,
            centerY + std::sin(angleA) * radius,
            1.0f,
            0.0f,
            normalized[0],
            normalized[1],
            normalized[2],
            normalized[3],
        });
        PushVertex({
            centerX + std::cos(angleB) * radius,
            centerY + std::sin(angleB) * radius,
            0.0f,
            1.0f,
            normalized[0],
            normalized[1],
            normalized[2],
            normalized[3],
        });
    }
}

Texture2D LoadPngTexture(const char* filePath) {
    PngImageData image;
    if (!LoadPngImage(filePath, image)) {
        return {};
    }

    Texture2D texture;
    texture.id = CreateTextureFromRgba(image.pixels.data(), image.width, image.height);
    texture.width = image.width;
    texture.height = image.height;
    texture.valid = true;
    return texture;
}

bool LoadPngImage(const char* filePath, PngImageData& image) {
    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, filePath, "rb") != 0) {
        return false;
    }
#else
    file = std::fopen(filePath, "rb");
    if (file == nullptr) {
        return false;
    }
#endif

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png == nullptr) {
        std::fclose(file);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (info == nullptr) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        std::fclose(file);
        return false;
    }

    if (!PngSafeInit(png, file)) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(file);
        return false;
    }

    png_read_info(png, info);

    const png_uint_32 width = png_get_image_width(png, info);
    const png_uint_32 height = png_get_image_height(png, info);
    const png_byte colorType = png_get_color_type(png, info);
    const png_byte bitDepth = png_get_bit_depth(png, info);

    if (bitDepth == 16) {
        png_set_strip_16(png);
    }
    if (colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS) != 0) {
        png_set_tRNS_to_alpha(png);
    }
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }

    png_read_update_info(png, info);

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    std::vector<png_bytep> rows(height);
    for (png_uint_32 y = 0; y < height; ++y) {
        rows[y] = pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4U;
    }

    png_read_image(png, rows.data());

    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(file);

    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.pixels = std::move(pixels);
    return true;
}

void UpdateInputFromEvents() {
    gRenderer.previousKeys = gRenderer.currentKeys;
    gRenderer.previousMouseButtons = gRenderer.mouseButtons;

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    const SDL_MouseButtonFlags mouseState = SDL_GetMouseState(&mouseX, &mouseY);
    gRenderer.mousePosition = Vec2{mouseX, mouseY};
    gRenderer.mouseButtons[static_cast<std::size_t>(MouseButton::Left)] = (mouseState & SDL_BUTTON_LMASK) != 0;
    gRenderer.mouseButtons[static_cast<std::size_t>(MouseButton::Middle)] = (mouseState & SDL_BUTTON_MMASK) != 0;
    gRenderer.mouseButtons[static_cast<std::size_t>(MouseButton::Right)] = (mouseState & SDL_BUTTON_RMASK) != 0;

    const bool* keyboardState = SDL_GetKeyboardState(nullptr);
    for (int i = 0; i < static_cast<int>(SDL_SCANCODE_COUNT); ++i) {
        gRenderer.currentKeys[static_cast<std::size_t>(i)] = keyboardState[i];
    }
}

}  // namespace

SDL_GLContext GetNativeContext() {
    EnsureInitialized();
    return gRenderer.context;
}

bool IsTextInputActive() {
    EnsureInitialized();
    return SDL_TextInputActive(gRenderer.window);
}

void SetLogLevel(LogLevel level) {
    gRenderer.minimumLogLevel = level;
}

void TraceLog(LogLevel level, const char* type, const char* message) {
    WriteLog(level, type, message != nullptr ? message : "");
}

const char* TextFormat(const char* format, ...) {
    thread_local char buffer[4096];

    if (format == nullptr) {
        buffer[0] = '\0';
        return buffer;
    }

    va_list args;
    va_start(args, format);
#if defined(_MSC_VER)
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
#else
    std::vsnprintf(buffer, sizeof(buffer), format, args);
#endif
    va_end(args);
    return buffer;
}

float GetFrameTime() {
    return gRenderer.frameTime;
}

float GetDeltaTime() {
    return GetFrameTime();
}

double GetTime() {
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        return 0.0;
    }

    return static_cast<double>(SDL_GetTicks()) / 1000.0;
}

bool IsKeyDown(KeyboardKey key) {
    EnsureInitialized();
    const std::size_t index = static_cast<std::size_t>(key);
    return index < gRenderer.currentKeys.size() ? gRenderer.currentKeys[index] : false;
}

bool IsKeyPressed(KeyboardKey key) {
    EnsureInitialized();
    const std::size_t index = static_cast<std::size_t>(key);
    if (index >= gRenderer.currentKeys.size()) {
        return false;
    }

    return gRenderer.currentKeys[index] && !gRenderer.previousKeys[index];
}

bool IsMouseButtonDown(MouseButton button) {
    EnsureInitialized();
    const std::size_t index = static_cast<std::size_t>(button);
    return index < gRenderer.mouseButtons.size() ? gRenderer.mouseButtons[index] : false;
}

bool IsMouseButtonPressed(MouseButton button) {
    EnsureInitialized();
    const std::size_t index = static_cast<std::size_t>(button);
    if (index >= gRenderer.mouseButtons.size()) return false;
    return gRenderer.mouseButtons[index] && !gRenderer.previousMouseButtons[index];
}

bool IsMouseButtonReleased(MouseButton button) {
    EnsureInitialized();
    const std::size_t index = static_cast<std::size_t>(button);
    if (index >= gRenderer.mouseButtons.size()) return false;
    return !gRenderer.mouseButtons[index] && gRenderer.previousMouseButtons[index];
}

bool IsMouseButtonUp(MouseButton button) {
    return !IsMouseButtonDown(button);
}

Vec2 GetMousePosition() {
    EnsureInitialized();
    return gRenderer.mousePosition;
}

Vec2 GetMouseWheelMoveV() {
    EnsureInitialized();
    return gRenderer.mouseWheel;
}

float GetMouseWheelMove() {
    EnsureInitialized();
    return gRenderer.mouseWheel.y;
}

void BeginDrawing() {
    EnsureInitialized();
    gRenderer.drawing = true;
    gRenderer.currentTexture = gRenderer.whiteTexture;
    gRenderer.batchVertices.clear();
    gRenderer.eventsReady = false;
    RefreshViewport();
}

void EndDrawing() {
    EnsureInitialized();
    if (!gRenderer.drawing) {
        return;
    }

    FlushBatch();
    SDL_GL_SwapWindow(gRenderer.window);

    const std::uint64_t freq = SDL_GetPerformanceFrequency();

    if (gRenderer.targetFps > 0) {
        const std::uint64_t targetTicks = freq / static_cast<std::uint64_t>(gRenderer.targetFps);
        while (true) {
            const std::uint64_t now = SDL_GetPerformanceCounter();
            const std::uint64_t elapsed = now - gRenderer.lastFrameCounter;
            if (elapsed >= targetTicks) {
                break;
            }
            const std::uint64_t remaining = targetTicks - elapsed;
            if (remaining > freq / 500) {
                SDL_Delay(1);
            }
        }
    }

    const std::uint64_t frameEnd = SDL_GetPerformanceCounter();
    gRenderer.frameTime = static_cast<float>(frameEnd - gRenderer.lastFrameCounter) / static_cast<float>(freq);
    gRenderer.lastFrameCounter = frameEnd;
    gRenderer.drawing = false;
}

void ClearBackground(Color color) {
    EnsureInitialized();
    const auto normalized = ToNormalizedColor(color);
    glClearColor(normalized[0], normalized[1], normalized[2], normalized[3]);
    glClear(GL_COLOR_BUFFER_BIT);
}

void DrawRectangle(float x, float y, float width, float height, Color color) {
    EnsureInitialized();
    PushQuad(0, x, y, width, height, color);
}

void DrawRectangle(const Rectangle& rectangle, Color color) {
    DrawRectangle(rectangle.x, rectangle.y, rectangle.width, rectangle.height, color);
}

void DrawRectangleV(Vec2 position, Vec2 size, Color color) {
    DrawRectangle(position.x, position.y, size.x, size.y, color);
}

void DrawCircle(float centerX, float centerY, float radius, Color color) {
    EnsureInitialized();
    PushCircle(centerX, centerY, radius, color);
}

void DrawTexture(const Texture2D& texture, float x, float y, Color tint) {
    EnsureInitialized();
    if (!texture.valid || texture.id == 0) {
        return;
    }

    PushQuad(texture.id, x, y, static_cast<float>(texture.width), static_cast<float>(texture.height), tint);
}

void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint) {
    EnsureInitialized();
    if (!texture.valid || texture.id == 0) return;

    EnsureBatchTexture(texture.id);
    const auto normalized = ToNormalizedColor(tint);

    float tw = (float)texture.width;
    float th = (float)texture.height;

    float u0 = source.x / tw;
    float v0 = source.y / th;
    float u1 = (source.x + source.width) / tw;
    float v1 = (source.y + source.height) / th;

    Vec2 v[4] = {
        { -origin.x, -origin.y },
        { dest.width - origin.x, -origin.y },
        { dest.width - origin.x, dest.height - origin.y },
        { -origin.x, dest.height - origin.y }
    };

    if (rotation != 0.0f) {
        float rad = rotation * (3.1415926535f / 180.0f);
        float cosA = std::cos(rad);
        float sinA = std::sin(rad);

        for (int i = 0; i < 4; i++) {
            float rx = v[i].x * cosA - v[i].y * sinA;
            float ry = v[i].x * sinA + v[i].y * cosA;
            v[i].x = rx;
            v[i].y = ry;
        }
    }

    for (int i = 0; i < 4; i++) {
        v[i].x += dest.x;
        v[i].y += dest.y;
    }

    PushVertex({ v[0].x, v[0].y, u0, v0, normalized[0], normalized[1], normalized[2], normalized[3] });
    PushVertex({ v[1].x, v[1].y, u1, v0, normalized[0], normalized[1], normalized[2], normalized[3] });
    PushVertex({ v[2].x, v[2].y, u1, v1, normalized[0], normalized[1], normalized[2], normalized[3] });

    PushVertex({ v[0].x, v[0].y, u0, v0, normalized[0], normalized[1], normalized[2], normalized[3] });
    PushVertex({ v[2].x, v[2].y, u1, v1, normalized[0], normalized[1], normalized[2], normalized[3] });
    PushVertex({ v[3].x, v[3].y, u0, v1, normalized[0], normalized[1], normalized[2], normalized[3] });
}

Font GetDefaultFont() {
    EnsureInitialized();
    EnsureDefaultFontLoaded();
    return gDefaultFont;
}

void DrawText(const char* text, int x, int y, int fontSize, Color color) {
    DrawTextEx(GetDefaultFont(), text, Vec2{static_cast<float>(x), static_cast<float>(y)}, static_cast<float>(fontSize), 0.0f, color);
}

void DrawTextEx(Font font, const char* text, Vec2 position, float fontSize, float spacing, Color tint) {
    EnsureInitialized();
    if (text == nullptr || !font.valid) {
        return;
    }

    const float scale = fontSize / static_cast<float>(font.baseSize);
    const float lineHeight = static_cast<float>(font.lineHeight) * scale;
    const float baselineOffset = static_cast<float>(font.ascent) * scale;

    float x = position.x;
    float y = position.y;
    bool firstChar = true;

    for (const char* c = text; *c != '\0'; ++c) {
        if (*c == '\n') {
            x = position.x;
            y += lineHeight;
            firstChar = true;
            continue;
        }

        unsigned char uc = static_cast<unsigned char>(*c);
        if (uc < 32 || uc >= 127) {
            continue;
        }

        const FontGlyph& glyph = font.glyphs[uc - 32];
        if (!firstChar) {
            x += spacing;
        }
        firstChar = false;

        const float glyphX = x + glyph.offsetX * scale;
        const float glyphY = y + baselineOffset - glyph.offsetY * scale;
        const float glyphWidth = static_cast<float>(glyph.width) * scale;
        const float glyphHeight = static_cast<float>(glyph.height) * scale;

        if (glyphWidth > 0.0f && glyphHeight > 0.0f) {
            PushTexturedQuad(font.textureId, glyph.uv, glyphX, glyphY, glyphWidth, glyphHeight, tint);
        }

        x += glyph.advanceX * scale;
    }
}

Vec2 MeasureTextEx(Font font, const char* text, float fontSize, float spacing) {
    Vec2 result{0.0f, 0.0f};
    if (text == nullptr || !font.valid) {
        return result;
    }

    const float scale = fontSize / static_cast<float>(font.baseSize);
    const float lineHeight = static_cast<float>(font.lineHeight) * scale;
    float x = 0.0f;
    float maxWidth = 0.0f;
    bool firstChar = true;

    for (const char* c = text; *c != '\0'; ++c) {
        if (*c == '\n') {
            maxWidth = std::max(maxWidth, x);
            x = 0.0f;
            firstChar = true;
            continue;
        }

        unsigned char uc = static_cast<unsigned char>(*c);
        if (uc < 32 || uc >= 127) {
            continue;
        }

        const FontGlyph& glyph = font.glyphs[uc - 32];
        if (!firstChar) {
            x += spacing;
        }
        firstChar = false;
        x += glyph.advanceX * scale;
    }

    maxWidth = std::max(maxWidth, x);
    result.x = maxWidth;
    result.y = lineHeight * (1 + static_cast<int>(std::count(text, text + std::strlen(text), '\n')));
    return result;
}

int MeasureText(const char* text, int fontSize) {
    const Font font = GetDefaultFont();
    const Vec2 measure = MeasureTextEx(font, text, static_cast<float>(fontSize), 0.0f);
    return static_cast<int>(std::round(measure.x));
}

Texture2D LoadTexture(const char* filePath) {
    EnsureInitialized();
    Texture2D texture = LoadPngTexture(filePath);
    if (texture.valid) {
        WriteLog(LogLevel::Info, "ASSETS", std::string("Loaded texture: ") + filePath);
    } else {
        WriteLog(LogLevel::Warn, "ASSETS", std::string("Failed to load texture: ") + filePath);
    }
    return texture;
}

RenderTexture2D LoadRenderTexture(int width, int height) {
    EnsureInitialized();
    RenderTexture2D target = {};

    glGenFramebuffers(1, &target.id);
    glBindFramebuffer(GL_FRAMEBUFFER, target.id);

    glGenTextures(1, &target.texture.id);
    glBindTexture(GL_TEXTURE_2D, target.texture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture.id, 0);

    glGenRenderbuffers(1, &target.depthId);
    glBindRenderbuffer(GL_RENDERBUFFER, target.depthId);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, target.depthId);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        WriteLog(LogLevel::Error, "RENDERER", "Failed to create render texture");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    target.texture.width = width;
    target.texture.height = height;
    target.texture.valid = true;

    return target;
}

void UnloadRenderTexture(RenderTexture2D target) {
    if (target.id != 0) glDeleteFramebuffers(1, &target.id);
    if (target.depthId != 0) glDeleteRenderbuffers(1, &target.depthId);
    UnloadTexture(target.texture);
}

Texture2D GenCheckerTexture(int width, int height, int cellSize, Color colorA, Color colorB) {
    EnsureInitialized();
    if (width <= 0 || height <= 0 || cellSize <= 0) {
        WriteLog(LogLevel::Warn, "ASSETS", "Invalid checker texture parameters");
        return {};
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool evenCell = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            const Color color = evenCell ? colorA : colorB;
            const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
            pixels[index + 0] = color.r;
            pixels[index + 1] = color.g;
            pixels[index + 2] = color.b;
            pixels[index + 3] = color.a;
        }
    }

    Texture2D texture;
    texture.id = CreateTextureFromRgba(pixels.data(), width, height);
    texture.width = width;
    texture.height = height;
    texture.valid = true;
    WriteLog(LogLevel::Info, "ASSETS", "Generated checker texture");
    return texture;
}

void UnloadTexture(Texture2D& texture) {
    if (texture.id != 0) {
        glDeleteTextures(1, &texture.id);
    }

    texture = {};
}

Font LoadFont(const char* filePath, int fontSize) {
    Font font{};
    if (filePath == nullptr || fontSize <= 0) {
        WriteLog(LogLevel::Error, "ASSETS", "Invalid font parameters");
        return font;
    }

    if (!FileExists(filePath)) {
        WriteLog(LogLevel::Error, "ASSETS", std::string("Font file not found: ") + filePath);
        return font;
    }

    if (LoadFontFromFile(font, filePath, fontSize)) {
        WriteLog(LogLevel::Info, "ASSETS", std::string("Font loaded: ") + filePath);
    } else {
        WriteLog(LogLevel::Error, "ASSETS", std::string("Failed to load font: ") + filePath);
    }

    return font;
}

void UnloadFont(Font& font) {
    if (font.textureId != 0) {
        glDeleteTextures(1, &font.textureId);
        WriteLog(LogLevel::Info, "ASSETS", "Font unloaded");
    }

    font = {};
}

namespace {

std::string ReadShaderFile(const char* filePath) {
    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, filePath, "r") != 0) {
        return "";
    }
#else
    file = std::fopen(filePath, "r");
    if (file == nullptr) {
        return "";
    }
#endif

    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    std::string content(static_cast<std::size_t>(size), '\0');
    if (size > 0) {
        std::fread(content.data(), 1, static_cast<std::size_t>(size), file);
    }
    std::fclose(file);

    return content;
}

GLuint CompileShaderCustom(GLenum type, const char* source) {
    if (source == nullptr) {
        return 0;
    }

    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE) {
        return shader;
    }

    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(length), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    glDeleteShader(shader);

    WriteLog(LogLevel::Error, "SHADER", std::string("Compilation failed: ") + log);
    return 0;
}

}  // namespace

Shader LoadShader(const char* vsFileName, const char* fsFileName) {
    EnsureInitialized();

    std::string vsSource;
    std::string fsSource;

    if (vsFileName != nullptr) {
        vsSource = ReadShaderFile(vsFileName);
        if (vsSource.empty()) {
            WriteLog(LogLevel::Error, "SHADER", std::string("Failed to load vertex shader: ") + vsFileName);
            return {};
        }
    } else {
        vsSource = kVertexShaderSource;
    }

    if (fsFileName != nullptr) {
        fsSource = ReadShaderFile(fsFileName);
        if (fsSource.empty()) {
            WriteLog(LogLevel::Error, "SHADER", std::string("Failed to load fragment shader: ") + fsFileName);
            return {};
        }
    } else {
        fsSource = kFragmentShaderSource;
    }

    return LoadShaderFromMemory(vsSource.c_str(), fsSource.c_str());
}

Shader LoadShaderFromMemory(const char* vsSource, const char* fsSource) {
    EnsureInitialized();

    if (vsSource == nullptr || fsSource == nullptr) {
        WriteLog(LogLevel::Error, "SHADER", "Invalid shader sources");
        return {};
    }

    const GLuint vertex = CompileShaderCustom(GL_VERTEX_SHADER, vsSource);
    if (vertex == 0) {
        return {};
    }

    const GLuint fragment = CompileShaderCustom(GL_FRAGMENT_SHADER, fsSource);
    if (fragment == 0) {
        glDeleteShader(vertex);
        return {};
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success != GL_TRUE) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        glDeleteProgram(program);
        WriteLog(LogLevel::Error, "SHADER", std::string("Link failed: ") + log);
        return {};
    }

    Shader shader;
    shader.id = program;
    shader.locs = nullptr;
    shader.locCount = 0;

    WriteLog(LogLevel::Info, "SHADER", "Shader loaded successfully");
    return shader;
}

bool IsShaderValid(const Shader& shader) {
    return shader.id != 0;
}

int GetShaderLocation(const Shader& shader, const char* uniformName) {
    if (shader.id == 0 || uniformName == nullptr) {
        return -1;
    }

    const GLint location = glGetUniformLocation(shader.id, uniformName);
    return location;
}

int GetShaderAttributeLocation(const Shader& shader, const char* attribName) {
    if (shader.id == 0 || attribName == nullptr) {
        return -1;
    }

    const GLint location = glGetAttribLocation(shader.id, attribName);
    return location;
}

void SetShaderValue([[maybe_unused]] const Shader& shader, int locIndex, float value) {
    if (locIndex >= 0) {
        glUniform1f(locIndex, value);
    }
}

void SetShaderValue([[maybe_unused]] const Shader& shader, int locIndex, int value) {
    if (locIndex >= 0) {
        glUniform1i(locIndex, value);
    }
}

void SetShaderValue([[maybe_unused]] const Shader& shader, int locIndex, const Vec2& value) {
    if (locIndex >= 0) {
        glUniform2f(locIndex, value.x, value.y);
    }
}

void SetShaderValue([[maybe_unused]] const Shader& shader, int locIndex, const qc::Vec3& value) {
    if (locIndex >= 0) {
        glUniform3f(locIndex, value.x, value.y, value.z);
    }
}

void SetShaderValue([[maybe_unused]] const Shader& shader, int locIndex, const Color& value) {
    if (locIndex >= 0) {
        const float r = static_cast<float>(value.r) / 255.0f;
        const float g = static_cast<float>(value.g) / 255.0f;
        const float b = static_cast<float>(value.b) / 255.0f;
        const float a = static_cast<float>(value.a) / 255.0f;
        glUniform4f(locIndex, r, g, b, a);
    }
}

void SetShaderValueMatrix([[maybe_unused]] const Shader& shader, int locIndex, const float* mat) {
    if (locIndex >= 0 && mat != nullptr) {
        glUniformMatrix4fv(locIndex, 1, GL_FALSE, mat);
    }
}

void SetShaderValueSampler([[maybe_unused]] const Shader& shader, int locIndex, int textureUnit) {
    if (locIndex >= 0) {
        glUniform1i(locIndex, textureUnit);
    }
}

void BeginShaderMode(const Shader& shader) {
    if (shader.id != 0) {
        gRenderer.currentShaderProgram = shader.id;
        glUseProgram(shader.id);
    }
}

void EndShaderMode() {
    gRenderer.currentShaderProgram = gRenderer.defaultShaderProgram;
    glUseProgram(gRenderer.defaultShaderProgram);
}

void UnloadShader(Shader& shader) {
    if (shader.id != 0) {
        glDeleteProgram(shader.id);
    }

    if (shader.locs != nullptr) {
        delete[] shader.locs;
        shader.locs = nullptr;
    }

    shader.locCount = 0;
    shader.id = 0;
}

namespace {
const char* kVertex3DShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 vFragPos;
out vec3 vNormal;
out vec2 vTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    vFragPos = vec3(uModel * vec4(aPosition, 1.0));
    vNormal = mat3(uModel) * aNormal;
    vTexCoord = aTexCoord;
    gl_Position = uProjection * uView * vec4(vFragPos, 1.0);
}
)";

const char* kFragment3DShaderSource = R"(
#version 330 core
in vec3 vFragPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec3 uLightPos;
uniform vec4 uColor;

void main() {
    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(uLightPos - vFragPos);
    
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * vec3(1.0, 1.0, 1.0);
    
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * vec3(1.0, 1.0, 1.0);
    
    vec4 texColor = texture(uTexture, vTexCoord);
    vec3 result = (ambient + diffuse) * texColor.rgb * uColor.rgb;
    FragColor = vec4(result, texColor.a * uColor.a);
}
)";


GLuint Compile3DShader() {
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &kVertex3DShaderSource, nullptr);
    glCompileShader(vertex);

    GLint vStatus;
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &vStatus);
    if (!vStatus) {
        char log[512];
        glGetShaderInfoLog(vertex, 512, nullptr, log);
        WriteLog(LogLevel::Error, "GLSL", TextFormat("Vertex shader error: %s", log));
    }

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &kFragment3DShaderSource, nullptr);
    glCompileShader(fragment);

    GLint fStatus;
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &fStatus);
    if (!fStatus) {
        char log[512];
        glGetShaderInfoLog(fragment, 512, nullptr, log);
        WriteLog(LogLevel::Error, "GLSL", TextFormat("Fragment shader error: %s", log));
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    GLint lStatus;
    glGetProgramiv(program, GL_LINK_STATUS, &lStatus);
    if (!lStatus) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        WriteLog(LogLevel::Error, "GLSL", TextFormat("Program link error: %s", log));
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return program;
}

Mesh ProcessMesh(const aiMesh* aiMesh, const aiScene* scene) {
    (void)scene;

    Mesh mesh{};
    mesh.vertexCount = static_cast<int>(aiMesh->mNumVertices);
    mesh.triangleCount = static_cast<int>(aiMesh->mNumFaces);

    if (mesh.vertexCount > 0) {
        mesh.vertices = new float[mesh.vertexCount * 3];
        mesh.normals = new float[mesh.vertexCount * 3];
        mesh.texcoords = new float[mesh.vertexCount * 2];
        mesh.texcoords2 = nullptr;
        mesh.tangents = nullptr;
        mesh.colors = nullptr;
        mesh.boneCount = 0;
        mesh.boneIndices = nullptr;
        mesh.boneWeights = nullptr;
        mesh.animVertices = nullptr;
        mesh.animNormals = nullptr;

        for (int i = 0; i < mesh.vertexCount; ++i) {
            mesh.vertices[i * 3 + 0] = aiMesh->mVertices[i].x;
            mesh.vertices[i * 3 + 1] = aiMesh->mVertices[i].y;
            mesh.vertices[i * 3 + 2] = aiMesh->mVertices[i].z;

            if (aiMesh->HasNormals()) {
                Vec3 normal = Vec3(aiMesh->mNormals[i].x, aiMesh->mNormals[i].y, aiMesh->mNormals[i].z).normalized();
                mesh.normals[i * 3 + 0] = normal.x;
                mesh.normals[i * 3 + 1] = normal.y;
                mesh.normals[i * 3 + 2] = normal.z;
            } else {
                mesh.normals[i * 3 + 0] = 0.0f;
                mesh.normals[i * 3 + 1] = 1.0f;
                mesh.normals[i * 3 + 2] = 0.0f;
            }

            if (aiMesh->mTextureCoords[0]) {
                mesh.texcoords[i * 2 + 0] = aiMesh->mTextureCoords[0][i].x;
                mesh.texcoords[i * 2 + 1] = aiMesh->mTextureCoords[0][i].y;
            } else {
                mesh.texcoords[i * 2 + 0] = 0.0f;
                mesh.texcoords[i * 2 + 1] = 0.0f;
            }
        }
    }

    if (mesh.triangleCount > 0) {
        mesh.indices = new unsigned short[mesh.triangleCount * 3];
        for (int i = 0; i < mesh.triangleCount; ++i) {
            const aiFace& face = aiMesh->mFaces[i];
            for (int j = 0; j < 3; ++j) {
                unsigned int index = (j < face.mNumIndices) ? face.mIndices[j] : 0u;
                mesh.indices[i * 3 + j] = static_cast<unsigned short>(index);
            }
        }
    }

    mesh.vboId = new unsigned int[2]{0, 0};
    glGenVertexArrays(1, &mesh.vaoId);
    glGenBuffers(2, mesh.vboId);

    std::vector<float> vertexData;
    vertexData.reserve(mesh.vertexCount * 8);
    for (int i = 0; i < mesh.vertexCount; ++i) {
        vertexData.push_back(mesh.vertices[i * 3 + 0]);
        vertexData.push_back(mesh.vertices[i * 3 + 1]);
        vertexData.push_back(mesh.vertices[i * 3 + 2]);
        vertexData.push_back(mesh.normals[i * 3 + 0]);
        vertexData.push_back(mesh.normals[i * 3 + 1]);
        vertexData.push_back(mesh.normals[i * 3 + 2]);
        vertexData.push_back(mesh.texcoords[i * 2 + 0]);
        vertexData.push_back(mesh.texcoords[i * 2 + 1]);
    }

    glBindVertexArray(mesh.vaoId);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vboId[0]);
    glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STATIC_DRAW);

    if (mesh.indices) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.vboId[1]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.triangleCount * 3 * sizeof(unsigned short), mesh.indices, GL_STATIC_DRAW);
    }

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return mesh;
}

void ProcessMaterials(const aiScene* scene, std::vector<Material>& materials, const char* directory) {
    materials.clear();

    const int MAX_MATERIAL_MAPS = 11;

    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        const aiMaterial* mat = scene->mMaterials[i];
        Material material{};

        material.shader = nullptr;
        material.maps = new MaterialMap[MAX_MATERIAL_MAPS];
        
        for (int j = 0; j < MAX_MATERIAL_MAPS; ++j) {
            material.maps[j].texture = Texture2D{};
            material.maps[j].color = Color{255, 255, 255, 255};
            material.maps[j].value = 0.0f;
        }

        material.params[0] = 0.0f;
        material.params[1] = 0.0f;
        material.params[2] = 0.0f;
        material.params[3] = 0.0f;

        if (mat->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            aiString texPath;
            mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath);
            std::string fullPath = std::string(directory) + "/" + std::string(texPath.C_Str());
            Texture2D tex = LoadTexture(fullPath.c_str());
            if (tex.valid) {
                material.maps[MATERIAL_MAP_ALBEDO].texture = tex;
            }
        }

        if (mat->GetTextureCount(aiTextureType_NORMALS) > 0) {
            aiString texPath;
            mat->GetTexture(aiTextureType_NORMALS, 0, &texPath);
            std::string fullPath = std::string(directory) + "/" + std::string(texPath.C_Str());
            Texture2D tex = LoadTexture(fullPath.c_str());
            if (tex.valid) {
                material.maps[MATERIAL_MAP_NORMAL].texture = tex;
            }
        }

        if (mat->GetTextureCount(aiTextureType_SHININESS) > 0) {
            aiString texPath;
            mat->GetTexture(aiTextureType_SHININESS, 0, &texPath);
            std::string fullPath = std::string(directory) + "/" + std::string(texPath.C_Str());
            Texture2D tex = LoadTexture(fullPath.c_str());
            if (tex.valid) {
                material.maps[MATERIAL_MAP_ROUGHNESS].texture = tex;
            }
        }

        aiColor3D diffuse(1.0f, 1.0f, 1.0f);
        mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
        material.maps[MATERIAL_MAP_ALBEDO].color = Color{
            static_cast<unsigned char>(diffuse.r * 255),
            static_cast<unsigned char>(diffuse.g * 255),
            static_cast<unsigned char>(diffuse.b * 255),
            255
        };

        float shininess = 32.0f;
        mat->Get(AI_MATKEY_SHININESS, shininess);
        material.params[0] = shininess / 100.0f;

        materials.push_back(material);
    }
}

void ProcessNode(const aiNode* node, const aiScene* scene, std::vector<Mesh>& meshes, std::vector<int>& meshMaterial) {
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh* aiMesh = scene->mMeshes[node->mMeshes[i]];
        meshes.push_back(ProcessMesh(aiMesh, scene));
        meshMaterial.push_back(static_cast<int>(aiMesh->mMaterialIndex));
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        ProcessNode(node->mChildren[i], scene, meshes, meshMaterial);
    }
}

}  // namespace

Model LoadModel(const char* filePath) {
    Model model{};
    model.id = g3DState.nextModelId++;
    model.transform = Matrix::identity();
    model.skeleton.boneCount = 0;
    model.skeleton.bones = nullptr;
    model.skeleton.bindPose.transform = nullptr;
    model.currentPose.transform = nullptr;
    model.boneMatrices = nullptr;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filePath,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace |
        aiProcess_GenNormals);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        WriteLog(LogLevel::Error, "ASSIMP", importer.GetErrorString());
        return model;
    }

    std::string fullPath = filePath;
    size_t lastSlash = fullPath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        model.directory = fullPath.substr(0, lastSlash);
    }

    std::vector<Material> tempMaterials;
    std::vector<Mesh> tempMeshes;
    std::vector<int> tempMeshMaterial;

    ProcessMaterials(scene, tempMaterials, model.directory.c_str());
    ProcessNode(scene->mRootNode, scene, tempMeshes, tempMeshMaterial);

    model.materialCount = static_cast<int>(tempMaterials.size());
    if (model.materialCount > 0) {
        model.materials = new Material[model.materialCount];
        for (int i = 0; i < model.materialCount; ++i) {
            model.materials[i] = std::move(tempMaterials[i]);
        }
    }

    model.meshCount = static_cast<int>(tempMeshes.size());
    if (model.meshCount > 0) {
        model.meshes = new Mesh[model.meshCount];
        model.meshMaterial = new int[model.meshCount];
        for (int i = 0; i < model.meshCount; ++i) {
            model.meshes[i] = std::move(tempMeshes[i]);
            model.meshMaterial[i] = tempMeshMaterial[i];
        }
    }

    WriteLog(LogLevel::Info, "ASSIMP", TextFormat("Loaded model '%s' with %d meshes", filePath, model.meshCount));
    return model;
}

Vec2 GetScreenToWorld2D(Vec2 position, Camera2D camera) {
    float cosA = std::cos(camera.rotation * 3.14159265359f / 180.0f);
    float sinA = std::sin(camera.rotation * 3.14159265359f / 180.0f);
    
    float scale = camera.zoom;
    float tx = camera.offset.x - camera.target.x * scale * cosA - camera.target.y * scale * sinA;
    float ty = camera.offset.y + camera.target.x * scale * sinA - camera.target.y * scale * cosA;
    
    float x = (position.x - tx) * scale * cosA - (position.y - ty) * scale * sinA;
    float y = (position.x - tx) * scale * sinA + (position.y - ty) * scale * cosA;
    
    return Vec2{camera.target.x + x / (scale * scale), camera.target.y + y / (scale * scale)};
}

Vec2 GetWorldToScreen2D(Vec2 position, Camera2D camera) {
    float dx = position.x - camera.target.x;
    float dy = position.y - camera.target.y;
    
    float cosA = std::cos(camera.rotation * 3.14159265359f / 180.0f);
    float sinA = std::sin(camera.rotation * 3.14159265359f / 180.0f);
    
    float screenX = camera.offset.x + (dx * cosA - dy * sinA) * camera.zoom;
    float screenY = camera.offset.y + (dx * sinA + dy * cosA) * camera.zoom;
    
    return Vec2{screenX, screenY};
}

Vec3 GetWorldToScreen(Vec3 position, Camera3D camera) {
    Vec3 forward = camera.target - camera.position;
    forward = forward.normalized();

    Vec3 right = forward.cross(camera.up);
    right = right.normalized();

    Vec3 up = right.cross(forward);

    Vec3 relative = position - camera.position;
    float cameraX = relative.dot(right);
    float cameraY = relative.dot(up);
    float cameraZ = relative.dot(forward);

    const float screenWidth = static_cast<float>(GetScreenWidth());
    const float screenHeight = static_cast<float>(GetScreenHeight());
    if (screenWidth <= 0.0f || screenHeight <= 0.0f) {
        return Vec3{0.0f, 0.0f, cameraZ};
    }

    const float aspect = screenWidth / screenHeight;
    const float fovRad = camera.fovy * 3.14159265359f / 180.0f;
    const float halfHeight = std::tan(fovRad * 0.5f);
    const float halfWidth = halfHeight * aspect;

    float ndcX;
    float ndcY;

    if (camera.projection == CAMERA_ORTHOGRAPHIC) {
        const float orthoScale = camera.fovy > 0.0f ? camera.fovy : 1.0f;
        ndcX = cameraX / (orthoScale * aspect);
        ndcY = cameraY / orthoScale;
    } else {
        if (cameraZ == 0.0f) {
            cameraZ = 1e-6f;
        }
        ndcX = cameraX / (cameraZ * halfWidth);
        ndcY = cameraY / (cameraZ * halfHeight);
    }

    float screenX = (ndcX * 0.5f + 0.5f) * screenWidth;
    float screenY = (0.5f - ndcY * 0.5f) * screenHeight;

    return Vec3{screenX, screenY, cameraZ};
}

Ray GetScreenToWorldRay(Vec2 mousePosition, Camera3D camera) {
    Ray ray;
    ray.origin = camera.position;
    
    Vec3 forward = camera.target - camera.position;
    float forwardLen = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    if (forwardLen > 0) forward = Vec3{forward.x / forwardLen, forward.y / forwardLen, forward.z / forwardLen};
    
    Vec3 right = Vec3{forward.y * camera.up.z - forward.z * camera.up.y,
                      forward.z * camera.up.x - forward.x * camera.up.z,
                      forward.x * camera.up.y - forward.y * camera.up.x};
    float rightLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    if (rightLen > 0) right = Vec3{right.x / rightLen, right.y / rightLen, right.z / rightLen};
    
    Vec3 up = Vec3{right.y * forward.z - right.z * forward.y,
                   right.z * forward.x - right.x * forward.z,
                   right.x * forward.y - right.y * forward.x};
    
    float screenWidth = static_cast<float>(GetScreenWidth());
    float screenHeight = static_cast<float>(GetScreenHeight());
    float aspect = screenWidth / screenHeight;
    float fovRad = camera.fovy * 3.14159265359f / 180.0f;
    float fovHeight = 2.0f * std::tan(fovRad / 2.0f);
    float fovWidth = fovHeight * aspect;
    
    float x = (mousePosition.x / screenWidth - 0.5f) * fovWidth;
    float y = (0.5f - mousePosition.y / screenHeight) * fovHeight;
    
    ray.direction = Vec3{
        forward.x + right.x * x + up.x * y,
        forward.y + right.y * x + up.y * y,
        forward.z + right.z * x + up.z * y
    };
    float rayLen = std::sqrt(ray.direction.x * ray.direction.x + ray.direction.y * ray.direction.y + ray.direction.z * ray.direction.z);
    if (rayLen > 0) ray.direction = Vec3{ray.direction.x / rayLen, ray.direction.y / rayLen, ray.direction.z / rayLen};
    
    return ray;
}

bool IsKeyReleased(KeyboardKey key) {
    int scancode = static_cast<int>(key);
    if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT) return false;
    
    return gRenderer.previousKeys[scancode] && !gRenderer.currentKeys[scancode];
}

bool IsKeyUp(KeyboardKey key) {
    int scancode = static_cast<int>(key);
    if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT) return false;
    
    return !gRenderer.currentKeys[scancode];
}

int GetKeyPressed() {
    int key = gLastKeyPressed;
    gLastKeyPressed = 0;
    return key;
}

int GetCharPressed() {
    int ch = gLastCharPressed;
    gLastCharPressed = 0;
    return ch;
}

void SetExitKey(KeyboardKey key) {
    gExitKey = key;
}

Vec2 GetMouseDelta() {
    Vec2 delta = Vec2{gRenderer.mousePosition.x - gMousePreviousPosition.x, 
                      gRenderer.mousePosition.y - gMousePreviousPosition.y};
    gMousePreviousPosition = gRenderer.mousePosition;
    return delta;
}

void SetMousePosition(int x, int y) {
    if (gRenderer.window) {
        SDL_WarpMouseInWindow(gRenderer.window, x, y);
        gRenderer.mousePosition = Vec2{static_cast<float>(x), static_cast<float>(y)};
        gMousePreviousPosition = gRenderer.mousePosition;
    }
}

void DisableCursor() {
    if (gRenderer.window) {
        SDL_HideCursor();
        gCursorHidden = true;
    }
}

void EnableCursor() {
    if (gRenderer.window) {
        SDL_ShowCursor();
        gCursorHidden = false;
    }
}

bool IsCursorHidden() {
    return gCursorHidden;
}

void SetMouseCursor(MouseCursor cursor) {
    if (!gRenderer.window) return;
    
    SDL_SystemCursor sdlCursor;
    switch (cursor) {
        case MouseCursor::Arrow: sdlCursor = SDL_SYSTEM_CURSOR_DEFAULT; break;
        case MouseCursor::Ibeam: sdlCursor = SDL_SYSTEM_CURSOR_TEXT; break;
        case MouseCursor::Crosshair: sdlCursor = SDL_SYSTEM_CURSOR_CROSSHAIR; break;
        case MouseCursor::PointingHand: sdlCursor = SDL_SYSTEM_CURSOR_POINTER; break;
        case MouseCursor::ResizeEW: sdlCursor = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
        case MouseCursor::ResizeNS: sdlCursor = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
        case MouseCursor::ResizeNWSE: sdlCursor = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
        case MouseCursor::ResizeNESW: sdlCursor = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
        case MouseCursor::ResizeAll: sdlCursor = SDL_SYSTEM_CURSOR_MOVE; break;
        case MouseCursor::NotAllowed: sdlCursor = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
        default: sdlCursor = SDL_SYSTEM_CURSOR_DEFAULT; break;
    }
    
    SDL_Cursor* sdlCustomCursor = SDL_CreateSystemCursor(sdlCursor);
    if (sdlCustomCursor) {
        SDL_SetCursor(sdlCustomCursor);
        SDL_DestroyCursor(sdlCustomCursor);
    }
}

bool IsGamepadAvailable(int gamepad) {
    SDL_Joystick* joy = SDL_OpenJoystick(gamepad);
    if (joy) {
        SDL_CloseJoystick(joy);
        return true;
    }
    return false;
}

const char* GetGamepadName(int gamepad) {
    SDL_Joystick* joy = SDL_OpenJoystick(gamepad);
    if (!joy) return "UNKNOWN";
    
    const char* name = SDL_GetJoystickName(joy);
    SDL_CloseJoystick(joy);
    return name ? name : "UNKNOWN";
}

float GetGamepadAxisMovement(int gamepad, int axis) {
    SDL_Joystick* joy = SDL_OpenJoystick(gamepad);
    if (!joy) return 0.0f;
    
    float value = 0.0f;
    if (axis >= 0 && axis < SDL_GetNumJoystickAxes(joy)) {
        Sint16 axisValue = SDL_GetJoystickAxis(joy, axis);
        value = axisValue / 32768.0f;
    }
    
    SDL_CloseJoystick(joy);
    return value;
}

bool IsGamepadButtonPressed(int gamepad, int button) {
    SDL_Joystick* joy = SDL_OpenJoystick(gamepad);
    if (!joy) return false;
    
    bool pressed = SDL_GetJoystickButton(joy, button) != 0;
    SDL_CloseJoystick(joy);
    return pressed;
}

bool IsTextureValid(Texture2D texture) {
    return texture.valid && texture.id != 0;
}

void DrawTextureV(Texture2D texture, Vec2 position, Color tint) {
    DrawTexture(texture, position.x, position.y, tint);
}

void DrawTextureEx(Texture2D texture, Vec2 position, float rotation, float scale, Color tint) {
    Rectangle source = {0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)};
    Rectangle dest = {position.x, position.y, static_cast<float>(texture.width) * scale, static_cast<float>(texture.height) * scale};
    Vec2 origin = {static_cast<float>(texture.width) * scale / 2.0f, static_cast<float>(texture.height) * scale / 2.0f};
    DrawTexturePro(texture, source, dest, origin, rotation, tint);
}

void DrawTextureRec(Texture2D texture, Rectangle source, Vec2 position, Color tint) {
    Rectangle dest = {position.x, position.y, source.width, source.height};
    DrawTexturePro(texture, source, dest, Vec2{0.0f, 0.0f}, 0.0f, tint);
}

void DrawTextureTiled(Texture2D texture, float scale, Vec2 offset, Color tint) {
    if (!IsTextureValid(texture)) return;
    
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    
    int tilesX = static_cast<int>(std::ceil(screenWidth / (texture.width * scale))) + 1;
    int tilesY = static_cast<int>(std::ceil(screenHeight / (texture.height * scale))) + 1;
    
    for (int y = -1; y < tilesY; ++y) {
        for (int x = -1; x < tilesX; ++x) {
            float posX = offset.x + x * texture.width * scale;
            float posY = offset.y + y * texture.height * scale;
            DrawTexture(texture, posX, posY, tint);
        }
    }
}

void DrawTextureNPatch(Texture2D texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint) {
    DrawTexturePro(texture, source, dest, origin, rotation, tint);
}

bool IsRenderTextureValid(RenderTexture2D target) {
    return target.id != 0 && IsTextureValid(target.texture);
}

Texture2D GetRenderTextureTexture(RenderTexture2D target) {
    return target.texture;
}

void DrawLine(float x1, float y1, float x2, float y2, Color color) {
    DrawLineV(Vec2{x1, y1}, Vec2{x2, y2}, color);
}

void DrawLineV(Vec2 start, Vec2 end, Color color) {
    if (!gRenderer.drawing) return;
    
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_LINES);
    glColor4ub(color.r, color.g, color.b, color.a);
    glVertex2f(start.x, start.y);
    glVertex2f(end.x, end.y);
    glEnd();
    glEnable(GL_TEXTURE_2D);
    
    if (gRenderer.drawing && gRenderer.batchVertices.size() > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, gRenderer.vbo);
        glBufferData(GL_ARRAY_BUFFER, gRenderer.batchVertices.size() * sizeof(Vertex), gRenderer.batchVertices.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(gRenderer.batchVertices.size()));
        gRenderer.batchVertices.clear();
    }
}

void DrawRectangleLines(Rectangle rectangle, float lineWidth, Color color) {
    (void)lineWidth;

    DrawLine(rectangle.x, rectangle.y, rectangle.x + rectangle.width, rectangle.y, color);
    DrawLine(rectangle.x + rectangle.width, rectangle.y, rectangle.x + rectangle.width, rectangle.y + rectangle.height, color);
    DrawLine(rectangle.x + rectangle.width, rectangle.y + rectangle.height, rectangle.x, rectangle.y + rectangle.height, color);
    DrawLine(rectangle.x, rectangle.y + rectangle.height, rectangle.x, rectangle.y, color);
}

void DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color) {
    if (!gRenderer.drawing) return;
    
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_TRIANGLES);
    glColor4ub(color.r, color.g, color.b, color.a);
    glVertex2f(v1.x, v1.y);
    glVertex2f(v2.x, v2.y);
    glVertex2f(v3.x, v3.y);
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

void DrawCircleLines(float centerX, float centerY, float radius, Color color) {
    constexpr int segments = 36;
    constexpr float pi2 = 6.28318530718f;
    
    if (!gRenderer.drawing) return;
    
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_LINE_LOOP);
    glColor4ub(color.r, color.g, color.b, color.a);
    
    for (int i = 0; i < segments; ++i) {
        float angle = (i / static_cast<float>(segments)) * pi2;
        glVertex2f(centerX + radius * std::cos(angle), centerY + radius * std::sin(angle));
    }
    
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

void DrawEllipse(float centerX, float centerY, float radiusH, float radiusV, Color color) {
    constexpr int segments = 36;
    constexpr float pi2 = 6.28318530718f;
    
    if (!gRenderer.drawing) return;
    
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_POLYGON);
    glColor4ub(color.r, color.g, color.b, color.a);
    
    for (int i = 0; i < segments; ++i) {
        float angle = (i / static_cast<float>(segments)) * pi2;
        glVertex2f(centerX + radiusH * std::cos(angle), centerY + radiusV * std::sin(angle));
    }
    
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

void DrawPoly(Vec2 center, int sides, float radius, float rotation, Color color) {
    if (sides < 3 || !gRenderer.drawing) return;
    
    constexpr float pi2 = 6.28318530718f;
    
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_POLYGON);
    glColor4ub(color.r, color.g, color.b, color.a);
    
    for (int i = 0; i < sides; ++i) {
        float angle = (i / static_cast<float>(sides)) * pi2 + rotation * 3.14159265359f / 180.0f;
        glVertex2f(center.x + radius * std::cos(angle), center.y + radius * std::sin(angle));
    }
    
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

void DrawRectangleRounded(Rectangle rectangle, float roundness, int segments, Color color) {
    (void)segments;

    if (!gRenderer.drawing) return;
    
    float radius = roundness * std::min(rectangle.width, rectangle.height) / 2.0f;
    
    DrawCircle(rectangle.x + radius, rectangle.y + radius, radius, color);
    DrawCircle(rectangle.x + rectangle.width - radius, rectangle.y + radius, radius, color);
    DrawCircle(rectangle.x + rectangle.width - radius, rectangle.y + rectangle.height - radius, radius, color);
    DrawCircle(rectangle.x + radius, rectangle.y + rectangle.height - radius, radius, color);
    
    DrawRectangle(rectangle.x + radius, rectangle.y, rectangle.width - 2.0f * radius, rectangle.height, color);
    DrawRectangle(rectangle.x, rectangle.y + radius, rectangle.width, rectangle.height - 2.0f * radius, color);
}

Color Fade(Color color, float alpha) {
    Color result = color;
    result.a = static_cast<unsigned char>(color.a * alpha);
    return result;
}

Color ColorAlpha(Color color, float alpha) {
    color.a = static_cast<unsigned char>(255.0f * alpha);
    return color;
}

Color ColorTint(Color color, Color tint) {
    Color result;
    result.r = static_cast<unsigned char>((color.r / 255.0f) * (tint.r / 255.0f) * 255.0f);
    result.g = static_cast<unsigned char>((color.g / 255.0f) * (tint.g / 255.0f) * 255.0f);
    result.b = static_cast<unsigned char>((color.b / 255.0f) * (tint.b / 255.0f) * 255.0f);
    result.a = color.a;
    return result;
}

Color ColorBrightness(Color color, float factor) {
    Color result;
    result.r = static_cast<unsigned char>(std::clamp(color.r * factor, 0.0f, 255.0f));
    result.g = static_cast<unsigned char>(std::clamp(color.g * factor, 0.0f, 255.0f));
    result.b = static_cast<unsigned char>(std::clamp(color.b * factor, 0.0f, 255.0f));
    result.a = color.a;
    return result;
}

Color ColorContrast(Color color, float contrast) {
    float r = color.r / 255.0f;
    float g = color.g / 255.0f;
    float b = color.b / 255.0f;
    
    r = (r - 0.5f) * contrast + 0.5f;
    g = (g - 0.5f) * contrast + 0.5f;
    b = (b - 0.5f) * contrast + 0.5f;
    
    Color result;
    result.r = static_cast<unsigned char>(std::clamp(r * 255.0f, 0.0f, 255.0f));
    result.g = static_cast<unsigned char>(std::clamp(g * 255.0f, 0.0f, 255.0f));
    result.b = static_cast<unsigned char>(std::clamp(b * 255.0f, 0.0f, 255.0f));
    result.a = color.a;
    return result;
}

Color GetColor(unsigned int hexValue) {
    Color color;
    color.r = (hexValue >> 16) & 0xFF;
    color.g = (hexValue >> 8) & 0xFF;
    color.b = hexValue & 0xFF;
    color.a = 255;
    return color;
}

bool CheckCollisionRecs(Rectangle a, Rectangle b) {
    return !(a.x + a.width < b.x || b.x + b.width < a.x ||
             a.y + a.height < b.y || b.y + b.height < a.y);
}

bool CheckCollisionCircles(Vec2 center1, float radius1, Vec2 center2, float radius2) {
    float dx = center2.x - center1.x;
    float dy = center2.y - center1.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    return distance < (radius1 + radius2);
}

bool CheckCollisionPointRec(Vec2 point, Rectangle rect) {
    return point.x >= rect.x && point.x <= rect.x + rect.width &&
           point.y >= rect.y && point.y <= rect.y + rect.height;
}

bool CheckCollisionPointCircle(Vec2 point, Vec2 center, float radius) {
    float dx = point.x - center.x;
    float dy = point.y - center.y;
    float distance = std::sqrt(dx * dx + dy * dy);
    return distance <= radius;
}

void WaitTime(double seconds) {
    std::chrono::milliseconds duration(static_cast<int>(seconds * 1000.0));
    std::this_thread::sleep_for(duration);
}

int GetRandomValue(int min, int max) {
    if (min > max) std::swap(min, max);
    return min + (std::rand() % (max - min + 1));
}

void SetRandomSeed(unsigned int seed) {
    std::srand(seed);
}

bool IsWindowReady() {
    return gRenderer.window != nullptr && gRenderer.context != nullptr;
}

bool IsTextureReady(Texture2D texture) {
    return IsTextureValid(texture);
}

bool IsShaderReady(Shader shader) {
    return IsShaderValid(shader);
}

void UnloadModel(Model& model) {
    for (int i = 0; i < model.meshCount; ++i) {
        Mesh& mesh = model.meshes[i];
        if (mesh.vaoId != 0) {
            glDeleteVertexArrays(1, &mesh.vaoId);
        }
        if (mesh.vboId != nullptr) {
            glDeleteBuffers(2, mesh.vboId);
            delete[] mesh.vboId;
            mesh.vboId = nullptr;
        }
        delete[] mesh.vertices;
        mesh.vertices = nullptr;
        delete[] mesh.texcoords;
        mesh.texcoords = nullptr;
        delete[] mesh.texcoords2;
        mesh.texcoords2 = nullptr;
        delete[] mesh.normals;
        mesh.normals = nullptr;
        delete[] mesh.tangents;
        mesh.tangents = nullptr;
        delete[] mesh.colors;
        mesh.colors = nullptr;
        delete[] mesh.indices;
        mesh.indices = nullptr;
        delete[] mesh.boneIndices;
        mesh.boneIndices = nullptr;
        delete[] mesh.boneWeights;
        mesh.boneWeights = nullptr;
        delete[] mesh.animVertices;
        mesh.animVertices = nullptr;
        delete[] mesh.animNormals;
        mesh.animNormals = nullptr;
    }

    for (int i = 0; i < model.materialCount; ++i) {
        if (model.materials[i].maps != nullptr) {
            delete[] model.materials[i].maps;
            model.materials[i].maps = nullptr;
        }
    }

    delete[] model.meshes;
    model.meshes = nullptr;
    model.meshCount = 0;

    delete[] model.materials;
    model.materials = nullptr;
    model.materialCount = 0;

    delete[] model.meshMaterial;
    model.meshMaterial = nullptr;

    delete[] model.skeleton.bones;
    model.skeleton.bones = nullptr;
    model.skeleton.boneCount = 0;

    delete[] model.skeleton.bindPose.transform;
    model.skeleton.bindPose.transform = nullptr;

    delete[] model.currentPose.transform;
    model.currentPose.transform = nullptr;

    delete[] model.boneMatrices;
    model.boneMatrices = nullptr;

    model.directory.clear();
    model.id = 0;
}

void BeginMode3D(const Camera3D& camera) {
    if (!g3DState.initialized) {
        g3DState.shader3D = Compile3DShader();
        g3DState.modelLoc = glGetUniformLocation(g3DState.shader3D, "uModel");
        g3DState.viewLoc = glGetUniformLocation(g3DState.shader3D, "uView");
        g3DState.projLoc = glGetUniformLocation(g3DState.shader3D, "uProjection");
        g3DState.samplerLoc = glGetUniformLocation(g3DState.shader3D, "uTexture");
        g3DState.lightPosLoc = glGetUniformLocation(g3DState.shader3D, "uLightPos");
        g3DState.colorLoc = glGetUniformLocation(g3DState.shader3D, "uColor");
        g3DState.initialized = true;

        std::vector<std::uint8_t> whitePixels(4, 255);
        glGenTextures(1, &g3DState.whiteTexture);
        glBindTexture(GL_TEXTURE_2D, g3DState.whiteTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    
    if (g3DState.planeVAO == 0) {
        float plane_v[] = { 
            -0.5f, 0, -0.5f, 0,1,0, 0,0,  
             0.5f, 0, -0.5f, 0,1,0, 1,0,  
             0.5f, 0,  0.5f, 0,1,0, 1,1, 
            -0.5f, 0,  0.5f, 0,1,0, 0,1 
        };
        unsigned int plane_i[] = { 0, 1, 2, 0, 2, 3 };
        g3DState.planeIndexCount = 6;
        glGenVertexArrays(1, &g3DState.planeVAO); glGenBuffers(1, &g3DState.planeVBO); glGenBuffers(1, &g3DState.planeEBO);
        glBindVertexArray(g3DState.planeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, g3DState.planeVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(plane_v), plane_v, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g3DState.planeEBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(plane_i), plane_i, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*4, 0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*4, (void*)(3*4));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*4, (void*)(6*4));
        glBindVertexArray(0);

        float cube_v[] = {
            -0.5f, -0.5f,  0.5f,  0, 0, 1,  0, 0,   0.5f, -0.5f,  0.5f,  0, 0, 1,  1, 0,   0.5f,  0.5f,  0.5f,  0, 0, 1,  1, 1,  -0.5f,  0.5f,  0.5f,  0, 0, 1,  0, 1,
            -0.5f, -0.5f, -0.5f,  0, 0,-1,  0, 0,  -0.5f,  0.5f, -0.5f,  0, 0,-1,  1, 0,   0.5f,  0.5f, -0.5f,  0, 0,-1,  1, 1,   0.5f, -0.5f, -0.5f,  0, 0,-1,  0, 1,
            -0.5f,  0.5f, -0.5f,  0, 1, 0,  0, 0,  -0.5f,  0.5f,  0.5f,  0, 1, 0,  1, 0,   0.5f,  0.5f,  0.5f,  0, 1, 0,  1, 1,   0.5f,  0.5f, -0.5f,  0, 1, 0,  0, 1,
            -0.5f, -0.5f, -0.5f,  0,-1, 0,  0, 0,   0.5f, -0.5f, -0.5f,  0,-1, 0,  1, 0,   0.5f, -0.5f,  0.5f,  0,-1, 0,  1, 1,  -0.5f, -0.5f,  0.5f,  0,-1, 0,  0, 1,
             0.5f, -0.5f, -0.5f,  1, 0, 0,  0, 0,   0.5f,  0.5f, -0.5f,  1, 0, 0,  1, 0,   0.5f,  0.5f,  0.5f,  1, 0, 0,  1, 1,   0.5f, -0.5f,  0.5f,  1, 0, 0,  0, 1,
            -0.5f, -0.5f, -0.5f, -1, 0, 0,  0, 0,  -0.5f, -0.5f,  0.5f, -1, 0, 0,  1, 0,  -0.5f,  0.5f,  0.5f, -1, 0, 0,  1, 1,  -0.5f,  0.5f, -0.5f, -1, 0, 0,  0, 1
        };
        unsigned int cube_i[] = {
            0, 1, 2, 0, 2, 3,      4, 5, 6, 4, 6, 7,      8, 9, 10, 8, 10, 11,
            12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23
        };
        g3DState.cubeIndexCount = 36;
        glGenVertexArrays(1, &g3DState.cubeVAO); glGenBuffers(1, &g3DState.cubeVBO); glGenBuffers(1, &g3DState.cubeEBO);
        glBindVertexArray(g3DState.cubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, g3DState.cubeVBO); glBufferData(GL_ARRAY_BUFFER, sizeof(cube_v), cube_v, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g3DState.cubeEBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cube_i), cube_i, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*4, 0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*4, (void*)(3*4));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*4, (void*)(6*4));
        glBindVertexArray(0);

        std::vector<float> sphere_vertices;
        std::vector<unsigned int> sphere_indices;
        const int rings = 16, sectors = 16;
        for (int r = 0; r <= rings; ++r) {
            float phi = 3.14159f * (float)r / rings;
            for (int s = 0; s <= sectors; ++s) {
                float theta = 2.0f * 3.14159f * (float)s / sectors;
                float x = std::sin(phi) * std::cos(theta), y = std::cos(phi), z = std::sin(phi) * std::sin(theta);
                sphere_vertices.insert(sphere_vertices.end(), { x, y, z, x, y, z, (float)s/sectors, (float)r/rings });
            }
        }
        for (int r = 0; r < rings; ++r) {
            for (int s = 0; s < sectors; ++s) {
                sphere_indices.push_back(r * (sectors + 1) + s); sphere_indices.push_back((r + 1) * (sectors + 1) + s); sphere_indices.push_back((r + 1) * (sectors + 1) + (s + 1));
                sphere_indices.push_back(r * (sectors + 1) + s); sphere_indices.push_back((r + 1) * (sectors + 1) + (s + 1)); sphere_indices.push_back(r * (sectors + 1) + (s + 1));
            }
        }
        g3DState.sphereIndexCount = (int)sphere_indices.size();
        glGenVertexArrays(1, &g3DState.sphereVAO); glGenBuffers(1, &g3DState.sphereVBO); glGenBuffers(1, &g3DState.sphereEBO);
        glBindVertexArray(g3DState.sphereVAO);
        glBindBuffer(GL_ARRAY_BUFFER, g3DState.sphereVBO); glBufferData(GL_ARRAY_BUFFER, sphere_vertices.size()*4, sphere_vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g3DState.sphereEBO); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sphere_indices.size()*4, sphere_indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8*4, 0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8*4, (void*)(3*4));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8*4, (void*)(6*4));
        glBindVertexArray(0);

        glGenVertexArrays(1, &g3DState.lineVAO);
        glGenBuffers(1, &g3DState.lineVBO);
        glBindVertexArray(g3DState.lineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, g3DState.lineVBO);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, position));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)offsetof(Vertex3D, texCoord));
        glBindVertexArray(0);
    }

    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(g3DState.shader3D);

    Mat4 view = Mat4::lookAt(camera.position, camera.target, camera.up);
    float aspect = (float)gRenderer.width / (float)gRenderer.height;
    Mat4 proj = Mat4::perspective(camera.fovy * 3.14159f / 180.0f, aspect, 0.1f, 1000.0f);

    Set3DView(view, proj);

    if (g3DState.samplerLoc >= 0) {
        glUniform1i(g3DState.samplerLoc, 0);
    }

    if (g3DState.colorLoc >= 0) {
        glUniform4f(g3DState.colorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    }
    
    if (g3DState.lightPosLoc >= 0) {
        glUniform3f(g3DState.lightPosLoc, g3DState.lightPosition.x, g3DState.lightPosition.y, g3DState.lightPosition.z);
    }
}

void FlushLines3D();
void FlushTriangles3D();

void EndMode3D() {
    FlushLines3D();
    FlushTriangles3D();
    glDisable(GL_DEPTH_TEST);
    glUseProgram(0);
}

void PushMatrix() {
    gMatrixStack.push_back(gCurrentMatrix);
}

void PopMatrix() {
    if (!gMatrixStack.empty()) {
        gCurrentMatrix = gMatrixStack.back();
        gMatrixStack.pop_back();
    } else {
        gCurrentMatrix = Mat4::identity();
    }
}

void Translate(const Vec3& translation) {
    gCurrentMatrix = gCurrentMatrix * Mat4::translation(translation.x, translation.y, translation.z);
}

void Translate(float x, float y, float z) {
    Translate(Vec3{x, y, z});
}

void Rotate(float angle, const Vec3& axis) {
    Vec3 normalizedAxis = axis;
    float length = normalizedAxis.length();
    if (length <= 0.0f) {
        return;
    }

    normalizedAxis = normalizedAxis * (1.0f / length);
    float c = std::cos(angle);
    float s = std::sin(angle);
    float t = 1.0f - c;

    Mat4 rotation = Mat4::identity();
    rotation.m[0] = c + normalizedAxis.x * normalizedAxis.x * t;
    rotation.m[1] = normalizedAxis.x * normalizedAxis.y * t + normalizedAxis.z * s;
    rotation.m[2] = normalizedAxis.x * normalizedAxis.z * t - normalizedAxis.y * s;
    rotation.m[4] = normalizedAxis.y * normalizedAxis.x * t - normalizedAxis.z * s;
    rotation.m[5] = c + normalizedAxis.y * normalizedAxis.y * t;
    rotation.m[6] = normalizedAxis.y * normalizedAxis.z * t + normalizedAxis.x * s;
    rotation.m[8] = normalizedAxis.z * normalizedAxis.x * t + normalizedAxis.y * s;
    rotation.m[9] = normalizedAxis.z * normalizedAxis.y * t - normalizedAxis.x * s;
    rotation.m[10] = c + normalizedAxis.z * normalizedAxis.z * t;

    gCurrentMatrix = gCurrentMatrix * rotation;
}

void Rotate(float angle) {
    Rotate(angle, Vec3{0.0f, 0.0f, 1.0f});
}

void Scale(const Vec3& scale) {
    gCurrentMatrix = gCurrentMatrix * Mat4::scale(scale.x, scale.y, scale.z);
}

void Scale(float scale) {
    Scale(Vec3{scale, scale, scale});
}

void MultMatrix(const Mat4& matrix) {
    gCurrentMatrix = gCurrentMatrix * matrix;
}

void EnableBackfaceCulling() {
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
}

void DisableBackfaceCulling() {
    glDisable(GL_CULL_FACE);
}

Vec3 TransformPoint(const Mat4& matrix, const Vec3& point) {
    float x = matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12];
    float y = matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13];
    float z = matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14];
    float w = matrix.m[3] * point.x + matrix.m[7] * point.y + matrix.m[11] * point.z + matrix.m[15];
    if (w != 0.0f) {
        x /= w;
        y /= w;
        z /= w;
    }
    return Vec3{x, y, z};
}

Mat4 ApplyCurrentMatrix(const Mat4& transform) {
    return gCurrentMatrix * transform;
}

void Set3DView(const Mat4& view, const Mat4& projection) {
    g3DState.viewMatrix = view;
    g3DState.projectionMatrix = projection;

    if (g3DState.initialized) {
        glUniformMatrix4fv(g3DState.viewLoc, 1, GL_FALSE, g3DState.viewMatrix.m);
        glUniformMatrix4fv(g3DState.projLoc, 1, GL_FALSE, g3DState.projectionMatrix.m);
    }
}

void DrawModel(const Model& model, const Vec3& position, float scale,
               float rotationX, float rotationY, float rotationZ) {
    Mat4 transform = Mat4::translation(position.x, position.y, position.z);
    transform = transform * Mat4::rotationY(rotationY);
    transform = transform * Mat4::rotationX(rotationX);
    transform = transform * Mat4::rotationZ(rotationZ);
    transform = transform * Mat4::scale(scale, scale, scale);

    DrawModelEx(model, transform);
}

void DrawModelEx(const Model& model, const Mat4& transform) {
    Mat4 finalTransform = ApplyCurrentMatrix(transform);
    if (g3DState.modelLoc >= 0) {
        glUniformMatrix4fv(g3DState.modelLoc, 1, GL_FALSE, finalTransform.m);
    }

    if (g3DState.colorLoc >= 0) {
        glUniform4f(g3DState.colorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    for (int i = 0; i < model.meshCount; ++i) {
        const Mesh& mesh = model.meshes[i];
        glActiveTexture(GL_TEXTURE0);

        GLuint textureId = g3DState.whiteTexture;
        if (model.meshMaterial && model.meshMaterial[i] >= 0 && model.meshMaterial[i] < model.materialCount) {
            const Material& mat = model.materials[model.meshMaterial[i]];
            if (mat.maps != nullptr && mat.maps[MATERIAL_MAP_ALBEDO].texture.valid) {
                textureId = mat.maps[MATERIAL_MAP_ALBEDO].texture.id;
            }
        }
        glBindTexture(GL_TEXTURE_2D, textureId);

        glBindVertexArray(mesh.vaoId);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.triangleCount * 3), GL_UNSIGNED_SHORT, nullptr);
        glBindVertexArray(0);
    }
}

void FlushLines3D() {
    if (g3DState.lineVertices.empty()) return;

    Mat4 identity = Mat4::identity();
    if (g3DState.modelLoc >= 0) glUniformMatrix4fv(g3DState.modelLoc, 1, GL_FALSE, identity.m);
    glBindTexture(GL_TEXTURE_2D, g3DState.whiteTexture);

    glBindVertexArray(g3DState.lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g3DState.lineVBO);
    glBufferData(GL_ARRAY_BUFFER, g3DState.lineVertices.size() * sizeof(Vertex3D), g3DState.lineVertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(g3DState.lineVertices.size()));
    glBindVertexArray(0);

    g3DState.lineVertices.clear();
}

void FlushTriangles3D() {
    if (g3DState.triVertices.empty()) return;

    Mat4 identity = Mat4::identity();
    if (g3DState.modelLoc >= 0) glUniformMatrix4fv(g3DState.modelLoc, 1, GL_FALSE, identity.m);
    glBindTexture(GL_TEXTURE_2D, g3DState.whiteTexture);

    glBindVertexArray(g3DState.triVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g3DState.triVBO);
    glBufferData(GL_ARRAY_BUFFER, g3DState.triVertices.size() * sizeof(Vertex3D), g3DState.triVertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(g3DState.triVertices.size()));
    glBindVertexArray(0);

    g3DState.triVertices.clear();
}

void DrawTriangle3D(Vertex3D v1, Vertex3D v2, Vertex3D v3, Color color) {
    if (g3DState.triVertices.size() > 0 && 
        (color.r != g3DState.currentTriColor.r || color.g != g3DState.currentTriColor.g || 
         color.b != g3DState.currentTriColor.b || color.a != g3DState.currentTriColor.a)) {
        FlushTriangles3D();
    }

    g3DState.currentTriColor = color;

    if (g3DState.colorLoc >= 0) {
        glUniform4f(g3DState.colorLoc, color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
    }
    g3DState.triVertices.push_back({TransformPoint(gCurrentMatrix, v1.position), v1.normal, v1.texCoord});
    g3DState.triVertices.push_back({TransformPoint(gCurrentMatrix, v2.position), v2.normal, v2.texCoord});
    g3DState.triVertices.push_back({TransformPoint(gCurrentMatrix, v3.position), v3.normal, v3.texCoord});
}

void DrawPlane(Vec3 center, Vec2 size, Color color) {
    Mat4 transform = Mat4::translation(center.x, center.y, center.z);
    transform = transform * Mat4::scale(size.x, 1.0f, size.y);
    transform = ApplyCurrentMatrix(transform);
    if (g3DState.modelLoc >= 0) glUniformMatrix4fv(g3DState.modelLoc, 1, GL_FALSE, transform.m);
    
    if (g3DState.colorLoc >= 0) {
        glUniform4f(g3DState.colorLoc, color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
    }

    glBindTexture(GL_TEXTURE_2D, g3DState.whiteTexture);
    glBindVertexArray(g3DState.planeVAO);
    glDrawElements(GL_TRIANGLES, g3DState.planeIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void DrawCubeV(Vec3 position, Vec3 size, Color color) {
    DrawCube(position, size.x, size.y, size.z, color);
}

void DrawCube(Vec3 position, float width, float height, float length, Color color) {
    Mat4 transform = Mat4::translation(position.x, position.y, position.z);
    transform = transform * Mat4::scale(width, height, length);
    transform = ApplyCurrentMatrix(transform);
    if (g3DState.modelLoc >= 0) glUniformMatrix4fv(g3DState.modelLoc, 1, GL_FALSE, transform.m);
    
    if (g3DState.colorLoc >= 0) {
        glUniform4f(g3DState.colorLoc, color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
    }

    glBindTexture(GL_TEXTURE_2D, g3DState.whiteTexture);
    glBindVertexArray(g3DState.cubeVAO);
    glDrawElements(GL_TRIANGLES, g3DState.cubeIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void DrawCubeWiresV(Vec3 position, Vec3 size, Color color) {
    DrawCubeWires(position, size.x, size.y, size.z, color);
}

void DrawCubeWires(Vec3 position, float width, float height, float length, Color color) {
    float hw = width / 2.0f;
    float hh = height / 2.0f;
    float hl = length / 2.0f;
    
    Vec3 v0 = position + Vec3{-hw, -hh, -hl};
    Vec3 v1 = position + Vec3{hw, -hh, -hl};
    Vec3 v2 = position + Vec3{hw, hh, -hl};
    Vec3 v3 = position + Vec3{-hw, hh, -hl};
    Vec3 v4 = position + Vec3{-hw, -hh, hl};
    Vec3 v5 = position + Vec3{hw, -hh, hl};
    Vec3 v6 = position + Vec3{hw, hh, hl};
    Vec3 v7 = position + Vec3{-hw, hh, hl};
    
    DrawLine3D(v0, v1, color);
    DrawLine3D(v1, v2, color);
    DrawLine3D(v2, v3, color);
    DrawLine3D(v3, v0, color);
    
    DrawLine3D(v4, v5, color);
    DrawLine3D(v5, v6, color);
    DrawLine3D(v6, v7, color);
    DrawLine3D(v7, v4, color);
    
    DrawLine3D(v0, v4, color);
    DrawLine3D(v1, v5, color);
    DrawLine3D(v2, v6, color);
    DrawLine3D(v3, v7, color);
}

void DrawSphere(Vec3 centerPos, float radius, Color color) {
    Mat4 transform = Mat4::translation(centerPos.x, centerPos.y, centerPos.z);
    transform = transform * Mat4::scale(radius, radius, radius);
    transform = ApplyCurrentMatrix(transform);
    if (g3DState.modelLoc >= 0) glUniformMatrix4fv(g3DState.modelLoc, 1, GL_FALSE, transform.m);
    if (g3DState.colorLoc >= 0) {
        glUniform4f(g3DState.colorLoc, color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
    }
    glBindTexture(GL_TEXTURE_2D, g3DState.whiteTexture);

    glBindVertexArray(g3DState.sphereVAO);
    glDrawElements(GL_TRIANGLES, g3DState.sphereIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void DrawSphereEx(Vec3 centerPos, float radius, int rings, int slices, Color color) {
    if (rings < 3 || slices < 3) return;
    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < slices; s++) {
            float phi1 = PI * (float)r / rings;
            float phi2 = PI * (float)(r + 1) / rings;
            float theta1 = 2.0f * PI * (float)s / slices;
            float theta2 = 2.0f * PI * (float)(s + 1) / slices;
            Vec3 p1{ radius * sinf(phi1) * cosf(theta1), radius * cosf(phi1), radius * sinf(phi1) * sinf(theta1) };
            Vec3 p2{ radius * sinf(phi1) * cosf(theta2), radius * cosf(phi1), radius * sinf(phi1) * sinf(theta2) };
            Vec3 p3{ radius * sinf(phi2) * cosf(theta2), radius * cosf(phi2), radius * sinf(phi2) * sinf(theta2) };
            Vec3 p4{ radius * sinf(phi2) * cosf(theta1), radius * cosf(phi2), radius * sinf(phi2) * sinf(theta1) };
            DrawTriangle3D({ centerPos + p1, p1.normalized(), {0,0} }, { centerPos + p2, p2.normalized(), {0,0} }, { centerPos + p3, p3.normalized(), {0,0} }, color);
            DrawTriangle3D({ centerPos + p1, p1.normalized(), {0,0} }, { centerPos + p3, p3.normalized(), {0,0} }, { centerPos + p4, p4.normalized(), {0,0} }, color);
        }
    }
}

void DrawSphereWires(Vec3 centerPos, float radius, int rings, int slices, Color color) {
    if (rings < 2 || slices < 3) return;
    for (int r = 0; r <= rings; r++) {
        float phi = PI * (float)r / rings;
        for (int s = 0; s < slices; s++) {
            float theta1 = 2.0f * PI * (float)s / slices;
            float theta2 = 2.0f * PI * (float)(s + 1) / slices;
            Vec3 p1{ radius * sinf(phi) * cosf(theta1), radius * cosf(phi), radius * sinf(phi) * sinf(theta1) };
            Vec3 p2{ radius * sinf(phi) * cosf(theta2), radius * cosf(phi), radius * sinf(phi) * sinf(theta2) };
            DrawLine3D(centerPos + p1, centerPos + p2, color);
        }
    }
    for (int s = 0; s < slices; s++) {
        float theta = 2.0f * PI * (float)s / slices;
        for (int r = 0; r < rings; r++) {
            float phi1 = PI * (float)r / rings;
            float phi2 = PI * (float)(r + 1) / rings;
            Vec3 p1{ radius * sinf(phi1) * cosf(theta), radius * cosf(phi1), radius * sinf(phi1) * sinf(theta) };
            Vec3 p2{ radius * sinf(phi2) * cosf(theta), radius * cosf(phi2), radius * sinf(phi2) * sinf(theta) };
            DrawLine3D(centerPos + p1, centerPos + p2, color);
        }
    }
}

void DrawCylinder(Vec3 position, float radiusTop, float radiusBottom, float height, int slices, Color color) {
    DrawCylinderEx(position + Vec3{0, -height/2, 0}, position + Vec3{0, height/2, 0}, radiusBottom, radiusTop, slices, color);
}

void DrawCylinderEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius, int sides, Color color) {
    if (sides < 3) return;
    Vec3 direction = (endPos - startPos);
    float length = direction.length();
    if (length < EPSILON) return;
    direction = direction * (1.0f / length);
    Vec3 up{0, 1, 0};
    if (fabsf(direction.dot(up)) > 0.99f) up = {1, 0, 0};
    Vec3 xDir = direction.cross(up).normalized();
    Vec3 yDir = direction.cross(xDir).normalized();
    for (int i = 0; i < sides; i++) {
        float angle1 = 2.0f * PI * (float)i / sides;
        float angle2 = 2.0f * PI * (float)(i + 1) / sides;
        Vec3 p1 = startPos + xDir * cosf(angle1) * startRadius + yDir * sinf(angle1) * startRadius;
        Vec3 p2 = startPos + xDir * cosf(angle2) * startRadius + yDir * sinf(angle2) * startRadius;
        Vec3 p3 = endPos + xDir * cosf(angle2) * endRadius + yDir * sinf(angle2) * endRadius;
        Vec3 p4 = endPos + xDir * cosf(angle1) * endRadius + yDir * sinf(angle1) * endRadius;
        DrawTriangle3D({p1, (p1-startPos).normalized(), {0,0}}, {p2, (p2-startPos).normalized(), {0,0}}, {p3, (p3-endPos).normalized(), {0,0}}, color);
        DrawTriangle3D({p1, (p1-startPos).normalized(), {0,0}}, {p3, (p3-endPos).normalized(), {0,0}}, {p4, (p4-endPos).normalized(), {0,0}}, color);
        DrawTriangle3D({startPos, direction*-1.0f, {0,0}}, {p2, direction*-1.0f, {0,0}}, {p1, direction*-1.0f, {0,0}}, color);
        DrawTriangle3D({endPos, direction, {0,0}}, {p3, direction, {0,0}}, {p4, direction, {0,0}}, color);
    }
}

void DrawCylinderWires(Vec3 position, float radiusTop, float radiusBottom, float height, int slices, Color color) {
    DrawCylinderWiresEx(position + Vec3{0, -height/2, 0}, position + Vec3{0, height/2, 0}, radiusBottom, radiusTop, slices, color);
}

void DrawCylinderWiresEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius, int slices, Color color) {
    if (slices < 3) return;
    Vec3 direction = (endPos - startPos);
    float length = direction.length();
    if (length < EPSILON) return;
    direction = direction * (1.0f / length);
    Vec3 up{0, 1, 0};
    if (fabsf(direction.dot(up)) > 0.99f) up = {1, 0, 0};
    Vec3 xDir = direction.cross(up).normalized();
    Vec3 yDir = direction.cross(xDir).normalized();
    for (int i = 0; i < slices; i++) {
        float angle1 = 2.0f * PI * (float)i / slices;
        float angle2 = 2.0f * PI * (float)(i + 1) / slices;
        Vec3 p1 = startPos + xDir * cosf(angle1) * startRadius + yDir * sinf(angle1) * startRadius;
        Vec3 p2 = startPos + xDir * cosf(angle2) * startRadius + yDir * sinf(angle2) * startRadius;
        Vec3 p3 = endPos + xDir * cosf(angle1) * endRadius + yDir * sinf(angle1) * endRadius;
        Vec3 p4 = endPos + xDir * cosf(angle2) * endRadius + yDir * sinf(angle2) * endRadius;
        DrawLine3D(p1, p2, color); DrawLine3D(p3, p4, color); DrawLine3D(p1, p3, color);
    }
}

void DrawLine3D(Vec3 startPos, Vec3 endPos, Color color) {
    if (g3DState.lineVertices.size() > 0 && 
        (color.r != g3DState.currentLineColor.r || color.g != g3DState.currentLineColor.g || 
         color.b != g3DState.currentLineColor.b || color.a != g3DState.currentLineColor.a)) {
        FlushLines3D();
    }

    g3DState.currentLineColor = color;

    if (g3DState.colorLoc >= 0) {
        glUniform4f(g3DState.colorLoc, color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
    }

    Vec3 transformedStart = TransformPoint(gCurrentMatrix, startPos);
    Vec3 transformedEnd = TransformPoint(gCurrentMatrix, endPos);
    g3DState.lineVertices.push_back({transformedStart, {0,1,0}, {0,0}});
    g3DState.lineVertices.push_back({transformedEnd, {0,1,0}, {0,0}});
}

void DrawGrid(int slices, float spacing) {
    if (gCamera2DActive) {
    } else {
        float halfSize = (float)slices * spacing / 2.0f;
        for (int i = 0; i <= slices; ++i) {
            float f = -halfSize + (float)i * spacing;
            DrawLine3D({ f, 0.0f, -halfSize }, { f, 0.0f, halfSize }, DARKGRAY);
            DrawLine3D({ -halfSize, 0.0f, f }, { halfSize, 0.0f, f }, DARKGRAY);
        }
    }
}

Camera2D CreateCamera2D() {
    Camera2D camera{};
    camera.offset = {0.0f, 0.0f};
    camera.target = {0.0f, 0.0f};
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;
    return camera;
}

void BeginMode2D(const Camera2D& camera) {
    gCamera2D = camera;
    gCamera2DActive = true;
}

void EndMode2D() {
    gCamera2DActive = false;
}

void BeginTextureMode(RenderTexture2D target) {
    EnsureInitialized();
    FlushBatch();
    glBindFramebuffer(GL_FRAMEBUFFER, target.id);
    gRenderer.currentFbo = target.id;
    gRenderer.width = target.texture.width;
    gRenderer.height = target.texture.height;
    glViewport(0, 0, gRenderer.width, gRenderer.height);
}

void EndTextureMode() {
    EnsureInitialized();
    FlushBatch();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gRenderer.currentFbo = 0;
    RefreshViewport();
}

Camera2D GetCamera2D() {
    return gCamera2D;
}

void UpdateCamera2D(Camera2D& camera, float targetX, float targetY, float smoothness) {
    smoothness = std::clamp(smoothness, 0.0f, 1.0f);
    camera.target.x = camera.target.x * (1.0f - smoothness) + targetX * smoothness;
    camera.target.y = camera.target.y * (1.0f - smoothness) + targetY * smoothness;
}

namespace {
    Camera3D gCamera3D;
    bool gCamera3DActive = false;
}  // namespace

Camera3D CreateCamera3D() {
    Camera3D camera{};
    camera.position = {0.0f, 0.0f, 10.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    return camera;
}

const float* GetMatrixModelview() {
    return g3DState.viewMatrix.m;
}

const float* GetMatrixProjection() {
    return g3DState.projectionMatrix.m;
}

}  // namespace qc
