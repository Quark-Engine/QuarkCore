#pragma once

#include <SDL3/SDL.h>
#include <GL/glew.h>
#include <array>
#include <vector>
#include <cstddef>
#include <string>

#include "QuarkCore/QuarkCore.hpp"

namespace qc {

struct Vertex {
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
};

struct BatchCommand {
    GLuint textureId = 0;
    std::size_t vertexStart = 0;
    std::size_t vertexCount = 0;
    std::size_t indexStart = 0;
    std::size_t indexCount = 0;
};

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
    GLuint ebo = 0;
    int width = 0;
    int height = 0;
    int targetFps = 60;
    float frameTime = 0.0f;
    std::uint64_t lastFrameCounter = 0;
    bool drawing = false;
    bool shouldClose = false;
    LogLevel minimumLogLevel = LogLevel::Trace;
    std::vector<Vertex> batchVertices;
    std::vector<GLuint> batchIndices;
    std::vector<BatchCommand> batchCommands;
    std::array<bool, SDL_SCANCODE_COUNT> currentKeys{};
    std::array<bool, SDL_SCANCODE_COUNT> previousKeys{};
    std::array<bool, 8> mouseButtons{};
    Vec2 mousePosition{};
    std::vector<Event> events;
    std::size_t nextEventIndex = 0;
    bool eventsReady = false;
};

extern RendererState gRenderer;

struct ShaderUniformLocations {
    GLint screenSizeLocation = -1;
    GLint samplerLocation = -1;
};

struct PngImageData {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

inline constexpr std::size_t kMaxBatchVertices = 8192;
inline constexpr std::size_t kMaxBatchIndices = kMaxBatchVertices * 3 / 2;

void RefreshViewport();
void UpdateInputFromEvents();
void EnsureInitialized();
void WriteLog(LogLevel level, const char* type, const std::string& message);
void FlushBatch();
bool LoadPngImage(const char* filePath, PngImageData& image);

}  // namespace qc
