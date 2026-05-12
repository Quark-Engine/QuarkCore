#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/Quark3D.hpp"

namespace qc {
bool CheckWindowCall(bool result, const char* operation) {
    if (!result) {
        WriteLog(LogLevel::Warn, "WINDOW", std::string(operation) + " failed: " + SDL_GetError());
    }

    return result;
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
    glBindVertexArray(gRenderer.vao);
    glBindBuffer(GL_ARRAY_BUFFER, gRenderer.vbo);

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

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    RefreshViewport();
    gRenderer.lastFrameCounter = SDL_GetPerformanceCounter();
    gRenderer.minimumLogLevel = LogLevel::Trace;
    gRenderer.eventsReady = false;

    int version = SDL_GetVersion();

    WriteLog(LogLevel::Info, "CORE", "SDL Version: " + std::to_string(SDL_VERSIONNUM_MAJOR(version)) + "." +
                                                       std::to_string(SDL_VERSIONNUM_MINOR(version)) + "." +
                                                       std::to_string(SDL_VERSIONNUM_MICRO(version)))
    ;

    WriteLog(LogLevel::Info, "CORE", "OpenGL Version: " + std::string(reinterpret_cast<const char*>(glGetString(GL_VERSION))));
    WriteLog(LogLevel::Info, "CORE", "OpenGL Renderer: " + std::string(reinterpret_cast<const char*>(glGetString(GL_RENDERER))));
    WriteLog(LogLevel::Info, "CORE", "OpenGL Vendor: " + std::string(reinterpret_cast<const char*>(glGetString(GL_VENDOR))));
    WriteLog(LogLevel::Info, "CORE", "OpenGL Shading Language Version: " + std::string(reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION))));

    WriteLog(LogLevel::Info, "CORE", "Window and renderer initialized");
}

bool WindowShouldClose() {
    EnsureInitialized();
    if (!gRenderer.eventsReady) {
        PumpSystemEvents();
    }

    return gRenderer.shouldClose;
}

void CloseWindow() {
    if (g3DState.planeVAO != 0) {
        glDeleteVertexArrays(1, &g3DState.planeVAO);
        glDeleteBuffers(1, &g3DState.planeVBO);
        glDeleteBuffers(1, &g3DState.planeEBO);
    }
    if (g3DState.cubeVAO != 0) {
        glDeleteVertexArrays(1, &g3DState.cubeVAO);
        glDeleteBuffers(1, &g3DState.cubeVBO);
        glDeleteBuffers(1, &g3DState.cubeEBO);
    }
    if (g3DState.sphereVAO != 0) {
        glDeleteVertexArrays(1, &g3DState.sphereVAO);
        glDeleteBuffers(1, &g3DState.sphereVBO);
        glDeleteBuffers(1, &g3DState.sphereEBO);
    }
    if (g3DState.lineVAO != 0) {
        glDeleteVertexArrays(1, &g3DState.lineVAO);
        glDeleteBuffers(1, &g3DState.lineVBO);
    }
    g3DState = Model3DState();

    if (gRenderer.whiteTexture != 0) {
        glDeleteTextures(1, &gRenderer.whiteTexture);
        gRenderer.whiteTexture = 0;
    }
    if (gDefaultFont.textureId != 0) {
        glDeleteTextures(1, &gDefaultFont.textureId);
        gDefaultFont = {};
    }
    if (gRenderer.vbo != 0) {
        glDeleteBuffers(1, &gRenderer.vbo);
        gRenderer.vbo = 0;
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

    if (gFreeTypeInitialized) {
        FT_Done_FreeType(gFreeTypeLibrary);
        gFreeTypeLibrary = nullptr;
        gFreeTypeInitialized = false;
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

SDL_Window* GetNativeWindow() {
    EnsureInitialized();
    return gRenderer.window;
}

void SetTargetFPS(int fps) {
    EnsureInitialized();
    gRenderer.targetFps = fps >= 0 ? fps : 0;
}

int GetFPS() {
    if (gRenderer.frameTime <= 0.0f) {
        return 0;
    }

    return static_cast<int>(std::round(1.0f / gRenderer.frameTime));
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


}