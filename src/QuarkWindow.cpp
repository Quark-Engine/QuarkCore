#include "QuarkCore/QuarkCore.hpp"
#include "QuarkUtils.hpp"

#include <SDL3/SDL.h>
#include <GL/glew.h>
#include <png.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace qc {

RendererState gRenderer;

std::unordered_map<GLuint, ShaderUniformLocations> gShaderUniformCache;

namespace {

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

bool CheckWindowCall(bool result, const char* operation) {
    if (!result) {
        WriteLog(LogLevel::Warn, "WINDOW", std::string(operation) + " failed: " + SDL_GetError());
    }

    return result;
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

}  // namespace

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

std::array<float, 4> ToNormalizedColor(Color color) {
    constexpr float inv = 1.0f / 255.0f;
    return {color.r * inv, color.g * inv, color.b * inv, color.a * inv};
}

void EnsureInitialized() {
    if (gRenderer.window == nullptr) {
        Fail("QuarkCore is not initialized. Call InitWindow() first.");
    }
}

void RefreshViewport() {
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(gRenderer.window, &width, &height);
    gRenderer.width = width;
    gRenderer.height = height;
    glViewport(0, 0, width, height);
}

const ShaderUniformLocations& GetShaderUniformLocations(GLuint program) {
    if (program == 0) {
        static const ShaderUniformLocations emptyLocations;
        return emptyLocations;
    }

    auto it = gShaderUniformCache.find(program);
    if (it != gShaderUniformCache.end()) {
        return it->second;
    }

    ShaderUniformLocations locations;
    locations.screenSizeLocation = glGetUniformLocation(program, "uScreenSize");
    locations.samplerLocation = glGetUniformLocation(program, "uTexture");
    auto inserted = gShaderUniformCache.emplace(program, locations);
    return inserted.first->second;
}

void EnsureBatchSpace(std::size_t vertexCount, std::size_t indexCount) {
    if (gRenderer.batchVertices.size() + vertexCount > kMaxBatchVertices ||
        gRenderer.batchIndices.size() + indexCount > kMaxBatchIndices) {
        FlushBatch();
    }
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
    if (gRenderer.batchCommands.empty()) {
        return;
    }

    std::vector<BatchCommand> sortedCommands = gRenderer.batchCommands;
    std::stable_sort(sortedCommands.begin(), sortedCommands.end(), [](const BatchCommand& a, const BatchCommand& b) {
        return a.textureId < b.textureId;
    });

    std::vector<Vertex> vertexData;
    std::vector<GLuint> indexData;
    vertexData.reserve(gRenderer.batchVertices.size());
    indexData.reserve(gRenderer.batchIndices.size());

    for (const auto& command : sortedCommands) {
        const std::size_t baseVertex = vertexData.size();
        vertexData.insert(
            vertexData.end(),
            gRenderer.batchVertices.begin() + command.vertexStart,
            gRenderer.batchVertices.begin() + command.vertexStart + command.vertexCount
        );

        for (std::size_t i = 0; i < command.indexCount; ++i) {
            const GLuint originalIndex = gRenderer.batchIndices[command.indexStart + i];
            indexData.push_back(static_cast<GLuint>(baseVertex) + (originalIndex - static_cast<GLuint>(command.vertexStart)));
        }
    }

    glUseProgram(gRenderer.currentShaderProgram);
    const ShaderUniformLocations& uniforms = GetShaderUniformLocations(gRenderer.currentShaderProgram);
    if (uniforms.screenSizeLocation >= 0) {
        glUniform2f(uniforms.screenSizeLocation, static_cast<float>(gRenderer.width), static_cast<float>(gRenderer.height));
    }
    if (uniforms.samplerLocation >= 0) {
        glUniform1i(uniforms.samplerLocation, 0);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(gRenderer.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gRenderer.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertexData.size() * sizeof(Vertex)),
        vertexData.data(),
        GL_DYNAMIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gRenderer.ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indexData.size() * sizeof(GLuint)),
        indexData.data(),
        GL_DYNAMIC_DRAW
    );

    GLuint activeTexture = 0;
    std::size_t indexOffset = 0;
    std::size_t groupStart = 0;
    std::size_t groupCount = 0;
    for (const auto& command : sortedCommands) {
        if (command.textureId != activeTexture) {
            if (groupCount > 0) {
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(groupCount), GL_UNSIGNED_INT, reinterpret_cast<const void*>(groupStart * sizeof(GLuint)));
            }
            activeTexture = command.textureId;
            glBindTexture(GL_TEXTURE_2D, activeTexture);
            groupStart = indexOffset;
            groupCount = command.indexCount;
        } else {
            groupCount += command.indexCount;
        }
        indexOffset += command.indexCount;
    }

    if (groupCount > 0) {
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(groupCount), GL_UNSIGNED_INT, reinterpret_cast<const void*>(groupStart * sizeof(GLuint)));
    }

    gRenderer.batchVertices.clear();
    gRenderer.batchIndices.clear();
    gRenderer.batchCommands.clear();
}

void PushVertex(const Vertex& vertex) {
    gRenderer.batchVertices.push_back(vertex);
}

void PushQuad(GLuint textureId, float x, float y, float width, float height, Color color) {
    const GLuint resolvedTexture = textureId != 0 ? textureId : gRenderer.whiteTexture;
    const auto normalized = ToNormalizedColor(color);
    EnsureBatchSpace(4, 6);

    const std::size_t vertexStart = gRenderer.batchVertices.size();
    PushVertex({x, y, 0.0f, 0.0f, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x + width, y, 1.0f, 0.0f, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x + width, y + height, 1.0f, 1.0f, normalized[0], normalized[1], normalized[2], normalized[3]});
    PushVertex({x, y + height, 0.0f, 1.0f, normalized[0], normalized[1], normalized[2], normalized[3]});

    const GLuint baseIndex = static_cast<GLuint>(vertexStart);
    const std::size_t indexStart = gRenderer.batchIndices.size();
    gRenderer.batchIndices.push_back(baseIndex);
    gRenderer.batchIndices.push_back(baseIndex + 1);
    gRenderer.batchIndices.push_back(baseIndex + 2);
    gRenderer.batchIndices.push_back(baseIndex);
    gRenderer.batchIndices.push_back(baseIndex + 2);
    gRenderer.batchIndices.push_back(baseIndex + 3);

    gRenderer.batchCommands.push_back({resolvedTexture, vertexStart, 4, indexStart, 6});
}

void PushCircle(float centerX, float centerY, float radius, Color color) {
    const GLuint resolvedTexture = gRenderer.whiteTexture;
    const auto normalized = ToNormalizedColor(color);
    constexpr int segments = 48;
    EnsureBatchSpace(static_cast<std::size_t>(segments) + 1, static_cast<std::size_t>(segments) * 3);

    const std::size_t vertexStart = gRenderer.batchVertices.size();
    PushVertex({centerX, centerY, 0.5f, 0.5f, normalized[0], normalized[1], normalized[2], normalized[3]});

    for (int i = 0; i < segments; ++i) {
        const float angle = static_cast<float>(i) / static_cast<float>(segments) * 6.28318530718f;
        const float x = centerX + std::cos(angle) * radius;
        const float y = centerY + std::sin(angle) * radius;
        const float u = 0.5f + std::cos(angle) * 0.5f;
        const float v = 0.5f + std::sin(angle) * 0.5f;
        PushVertex({x, y, u, v, normalized[0], normalized[1], normalized[2], normalized[3]});
    }

    const GLuint baseIndex = static_cast<GLuint>(vertexStart);
    const std::size_t indexStart = gRenderer.batchIndices.size();
    for (int i = 1; i <= segments; ++i) {
        gRenderer.batchIndices.push_back(baseIndex);
        gRenderer.batchIndices.push_back(baseIndex + static_cast<GLuint>(i));
        gRenderer.batchIndices.push_back(baseIndex + static_cast<GLuint>(i % segments) + 1);
    }

    gRenderer.batchCommands.push_back({resolvedTexture, vertexStart, static_cast<std::size_t>(segments) + 1, indexStart, static_cast<std::size_t>(segments) * 3});
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

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(file);
        return false;
    }

    png_init_io(png, file);
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

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    const SDL_MouseButtonFlags mouseState = SDL_GetMouseState(&mouseX, &mouseY);
    gRenderer.mousePosition = Vec2{mouseX, mouseY};
    gRenderer.mouseButtons[static_cast<std::size_t>(MouseButton::Left)] = (mouseState & SDL_BUTTON_LMASK) != 0;
    gRenderer.mouseButtons[static_cast<std::size_t>(MouseButton::Middle)] = (mouseState & SDL_BUTTON_MMASK) != 0;
    gRenderer.mouseButtons[static_cast<std::size_t>(MouseButton::Right)] = (mouseState & SDL_BUTTON_RMASK) != 0;

    const bool* keyboardState = SDL_GetKeyboardState(nullptr);
    for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) {
        gRenderer.currentKeys[static_cast<std::size_t>(i)] = keyboardState[i];
    }
}

void InitWindow(int width, int height, const char* title) {
    WriteLog(LogLevel::Info, "CORE", "Starting QuarkCore " + std::to_string(QC_VERSION_MAJOR) + "." + std::to_string(QC_VERSION_MINOR) + "." + std::to_string(QC_VERSION_PATCH));

    if (gRenderer.window != nullptr) {
        return;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        Fail("SDL_Init failed");
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    gRenderer.window = SDL_CreateWindow(title, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (gRenderer.window == nullptr) {
        const std::string error = SDL_GetError();
        SDL_Quit();
        Fail("SDL_CreateWindow failed: " + error);
    }

    gRenderer.context = SDL_GL_CreateContext(gRenderer.window);
    if (gRenderer.context == nullptr) {
        const std::string error = SDL_GetError();
        SDL_DestroyWindow(gRenderer.window);
        gRenderer.window = nullptr;
        SDL_Quit();
        Fail("SDL_GL_CreateContext failed: " + error);
    }

    if (!SDL_GL_MakeCurrent(gRenderer.window, gRenderer.context)) {
        const std::string error = SDL_GetError();
        CloseWindow();
        Fail("SDL_GL_MakeCurrent failed: " + error);
    }

    SDL_GL_SetSwapInterval(0);

    glewExperimental = GL_TRUE;
    const GLenum glewStatus = glewInit();
    if (glewStatus != GLEW_OK) {
        const std::string error = reinterpret_cast<const char*>(glewGetErrorString(glewStatus));
        CloseWindow();
        Fail("glewInit failed: " + error);
    }

    gRenderer.program = CreateProgram();
    gRenderer.defaultShaderProgram = gRenderer.program;
    gRenderer.currentShaderProgram = gRenderer.program;
    glGenVertexArrays(1, &gRenderer.vao);
    glGenBuffers(1, &gRenderer.vbo);
    glGenBuffers(1, &gRenderer.ebo);
    glBindVertexArray(gRenderer.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gRenderer.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gRenderer.ebo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));

    gRenderer.screenSizeLocation = glGetUniformLocation(gRenderer.program, "uScreenSize");
    gRenderer.samplerLocation = glGetUniformLocation(gRenderer.program, "uTexture");

    const std::array<std::uint8_t, 4> whitePixel{255, 255, 255, 255};
    gRenderer.whiteTexture = CreateTextureFromRgba(whitePixel.data(), 1, 1);
    gRenderer.currentTexture = gRenderer.whiteTexture;
    gRenderer.batchVertices.reserve(kMaxBatchVertices);
    gRenderer.batchIndices.reserve(kMaxBatchIndices);
    gRenderer.batchCommands.reserve(256);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    RefreshViewport();
    gRenderer.lastFrameCounter = SDL_GetPerformanceCounter();
    gRenderer.minimumLogLevel = LogLevel::Trace;
    gRenderer.eventsReady = false;

    int version = SDL_GetVersion();

    WriteLog(LogLevel::Info, "CORE", "SDL Version: " + std::to_string(SDL_VERSIONNUM_MAJOR(version)) + "." +
                                                       std::to_string(SDL_VERSIONNUM_MINOR(version)) + "." +
                                                       std::to_string(SDL_VERSIONNUM_MICRO(version)));

    WriteLog(LogLevel::Info, "CORE", "OpenGL Version: " + std::string(reinterpret_cast<const char*>(glGetString(GL_VERSION))));
    WriteLog(LogLevel::Info, "CORE", "OpenGL Renderer: " + std::string(reinterpret_cast<const char*>(glGetString(GL_RENDERER))));
    WriteLog(LogLevel::Info, "CORE", "OpenGL Vendor: " + std::string(reinterpret_cast<const char*>(glGetString(GL_VENDOR))));
    WriteLog(LogLevel::Info, "CORE", "OpenGL Shading Language Version: " + std::string(reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION))));

    WriteLog(LogLevel::Info, "CORE", "Window and renderer initialized");
}

void CloseWindow() {
    if (gRenderer.whiteTexture != 0) {
        glDeleteTextures(1, &gRenderer.whiteTexture);
        gRenderer.whiteTexture = 0;
    }
    if (gRenderer.vbo != 0) {
        glDeleteBuffers(1, &gRenderer.vbo);
        gRenderer.vbo = 0;
    }
    if (gRenderer.ebo != 0) {
        glDeleteBuffers(1, &gRenderer.ebo);
        gRenderer.ebo = 0;
    }
    if (gRenderer.vao != 0) {
        glDeleteVertexArrays(1, &gRenderer.vao);
        gRenderer.vao = 0;
    }
    if (gRenderer.program != 0) {
        glDeleteProgram(gRenderer.program);
        gRenderer.program = 0;
    }
    if (gRenderer.context != nullptr) {
        SDL_GL_DestroyContext(gRenderer.context);
        gRenderer.context = nullptr;
    }
    if (gRenderer.window != nullptr) {
        SDL_DestroyWindow(gRenderer.window);
        gRenderer.window = nullptr;
    }

    SDL_Quit();
    gRenderer = {};
}

bool SetWindowTitle(const char* title) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowTitle(gRenderer.window, title != nullptr ? title : ""), "SDL_SetWindowTitle");
}

const char* GetWindowTitle() {
    EnsureInitialized();
    return SDL_GetWindowTitle(gRenderer.window);
}

bool SetWindowPosition(int x, int y) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowPosition(gRenderer.window, x, y), "SDL_SetWindowPosition");
}

IVec2 GetWindowPosition() {
    EnsureInitialized();
    IVec2 position{};
    SDL_GetWindowPosition(gRenderer.window, &position.x, &position.y);
    return position;
}

bool SetWindowSize(int width, int height) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowSize(gRenderer.window, width, height), "SDL_SetWindowSize");
}

IVec2 GetWindowSize() {
    EnsureInitialized();
    IVec2 size{};
    SDL_GetWindowSize(gRenderer.window, &size.x, &size.y);
    return size;
}

IVec2 GetWindowSizeInPixels() {
    EnsureInitialized();
    IVec2 size{};
    SDL_GetWindowSizeInPixels(gRenderer.window, &size.x, &size.y);
    return size;
}

bool SetWindowMinimumSize(int width, int height) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowMinimumSize(gRenderer.window, width, height), "SDL_SetWindowMinimumSize");
}

IVec2 GetWindowMinimumSize() {
    EnsureInitialized();
    IVec2 size{};
    SDL_GetWindowMinimumSize(gRenderer.window, &size.x, &size.y);
    return size;
}

bool SetWindowMaximumSize(int width, int height) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowMaximumSize(gRenderer.window, width, height), "SDL_SetWindowMaximumSize");
}

IVec2 GetWindowMaximumSize() {
    EnsureInitialized();
    IVec2 size{};
    SDL_GetWindowMaximumSize(gRenderer.window, &size.x, &size.y);
    return size;
}

bool SetWindowResizable(bool resizable) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowResizable(gRenderer.window, resizable), "SDL_SetWindowResizable");
}

bool SetWindowBordered(bool bordered) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowBordered(gRenderer.window, bordered), "SDL_SetWindowBordered");
}

bool SetWindowFullscreen(bool fullscreen) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowFullscreen(gRenderer.window, fullscreen), "SDL_SetWindowFullscreen");
}

bool ToggleFullscreen() {
    EnsureInitialized();
    return SetWindowFullscreen(!IsWindowFullscreen());
}

bool ShowWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_ShowWindow(gRenderer.window), "SDL_ShowWindow");
}

bool HideWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_HideWindow(gRenderer.window), "SDL_HideWindow");
}

bool RaiseWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_RaiseWindow(gRenderer.window), "SDL_RaiseWindow");
}

bool MaximizeWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_MaximizeWindow(gRenderer.window), "SDL_MaximizeWindow");
}

bool MinimizeWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_MinimizeWindow(gRenderer.window), "SDL_MinimizeWindow");
}

bool RestoreWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_RestoreWindow(gRenderer.window), "SDL_RestoreWindow");
}

bool SyncWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_SyncWindow(gRenderer.window), "SDL_SyncWindow");
}

bool IsWindowFullscreen() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_FULLSCREEN) != 0;
}

bool IsWindowHidden() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_HIDDEN) != 0;
}

bool IsWindowMinimized() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_MINIMIZED) != 0;
}

bool IsWindowMaximized() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_MAXIMIZED) != 0;
}

bool IsWindowFocused() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

bool IsWindowMouseFocused() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_MOUSE_FOCUS) != 0;
}

bool IsWindowResizable() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_RESIZABLE) != 0;
}

bool IsWindowBorderless() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gRenderer.window) & SDL_WINDOW_BORDERLESS) != 0;
}

float GetWindowDisplayScale() {
    EnsureInitialized();
    return SDL_GetWindowDisplayScale(gRenderer.window);
}

float GetWindowPixelDensity() {
    EnsureInitialized();
    return SDL_GetWindowPixelDensity(gRenderer.window);
}

bool SetWindowIcon(const char* filePath) {
    EnsureInitialized();

    PngImageData image;
    if (!LoadPngImage(filePath, image)) {
        WriteLog(LogLevel::Warn, "WINDOW", std::string("Failed to load window icon: ") + (filePath != nullptr ? filePath : ""));
        return false;
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        image.width,
        image.height,
        SDL_PIXELFORMAT_RGBA8888,
        image.pixels.data(),
        image.width * 4
    );
    if (surface == nullptr) {
        WriteLog(LogLevel::Warn, "WINDOW", std::string("SDL_CreateSurfaceFrom failed: ") + SDL_GetError());
        return false;
    }

    const bool ok = CheckWindowCall(SDL_SetWindowIcon(gRenderer.window, surface), "SDL_SetWindowIcon");
    SDL_DestroySurface(surface);
    return ok;
}

bool StartTextInput() {
    EnsureInitialized();
    return CheckWindowCall(SDL_StartTextInput(gRenderer.window), "SDL_StartTextInput");
}

bool StopTextInput() {
    EnsureInitialized();
    return CheckWindowCall(SDL_StopTextInput(gRenderer.window), "SDL_StopTextInput");
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

void SetTargetFPS(int fps) {
    EnsureInitialized();
    gRenderer.targetFps = fps >= 0 ? fps : 0;
}

float GetFrameTime() {
    return gRenderer.frameTime;
}

float GetDeltaTime() {
    return GetFrameTime();
}

int GetFPS() {
    if (gRenderer.frameTime <= 0.0f) {
        return 0;
    }

    return static_cast<int>(std::round(1.0f / gRenderer.frameTime));
}

double GetTime() {
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        return 0.0;
    }

    return static_cast<double>(SDL_GetTicks()) / 1000.0;
}

int GetScreenWidth() {
    EnsureInitialized();
    return gRenderer.width;
}

int GetScreenHeight() {
    EnsureInitialized();
    return gRenderer.height;
}

float GetCurrentMonitorRefreshRate() {
    EnsureInitialized();

    const SDL_DisplayID displayId = SDL_GetDisplayForWindow(gRenderer.window);
    if (displayId == 0) {
        return 0.0f;
    }

    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displayId);
    if (mode == nullptr) {
        return 0.0f;
    }

    if (mode->refresh_rate > 0.0f) {
        return mode->refresh_rate;
    }

    if (mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0) {
        return static_cast<float>(mode->refresh_rate_numerator) /
               static_cast<float>(mode->refresh_rate_denominator);
    }

    return 0.0f;
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
            const Uint32 delayMs = static_cast<Uint32>((remaining * 1000 + freq - 1) / freq);
            SDL_Delay(delayMs > 0 ? delayMs : 0);
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
    if (shader.id != 0 && gRenderer.currentShaderProgram != shader.id) {
        FlushBatch();
        gRenderer.currentShaderProgram = shader.id;
        glUseProgram(shader.id);
    }
}

void EndShaderMode() {
    if (gRenderer.currentShaderProgram != gRenderer.defaultShaderProgram) {
        FlushBatch();
        gRenderer.currentShaderProgram = gRenderer.defaultShaderProgram;
        glUseProgram(gRenderer.defaultShaderProgram);
    }
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

}  // namespace qc
