#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/Quark3D.hpp"
#include "QuarkInternal.hpp"
#include "Renderer/QuarkGLRenderer.hpp"

namespace qc {

extern IRenderer* gRendererPtr;

static bool CheckWindowCall(bool result, const char* operation) {
    if (!result) {
        TraceLog(LogLevel::Warn, "WINDOW", (std::string(operation) + " failed: " + SDL_GetError()).c_str());
    }
    return result;
}

bool SetWindowTitle(const char* title) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowTitle(gWin.window, title != nullptr ? title : ""), "SDL_SetWindowTitle");
}

const char* GetWindowTitle() {
    EnsureInitialized();
    return SDL_GetWindowTitle(gWin.window);
}

bool SetWindowPosition(int x, int y) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowPosition(gWin.window, x, y), "SDL_SetWindowPosition");
}

IVec2 GetWindowPosition() {
    EnsureInitialized();
    IVec2 position{};
    SDL_GetWindowPosition(gWin.window, &position.x, &position.y);
    return position;
}

bool SetWindowSize(int width, int height) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowSize(gWin.window, width, height), "SDL_SetWindowSize");
}

IVec2 GetWindowSize() {
    EnsureInitialized();
    IVec2 size{};
    SDL_GetWindowSize(gWin.window, &size.x, &size.y);
    return size;
}

IVec2 GetWindowSizeInPixels() {
    EnsureInitialized();
    IVec2 size{};
    SDL_GetWindowSizeInPixels(gWin.window, &size.x, &size.y);
    return size;
}

bool SetWindowMinimumSize(int width, int height) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowMinimumSize(gWin.window, width, height), "SDL_SetWindowMinimumSize");
}

IVec2 GetWindowMinimumSize() {
    EnsureInitialized();
    IVec2 size{};
    SDL_GetWindowMinimumSize(gWin.window, &size.x, &size.y);
    return size;
}

bool SetWindowMaximumSize(int width, int height) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowMaximumSize(gWin.window, width, height), "SDL_SetWindowMaximumSize");
}

IVec2 GetWindowMaximumSize() {
    EnsureInitialized();
    IVec2 size{};
    SDL_GetWindowMaximumSize(gWin.window, &size.x, &size.y);
    return size;
}

bool SetWindowResizable(bool resizable) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowResizable(gWin.window, resizable), "SDL_SetWindowResizable");
}

bool SetWindowBordered(bool bordered) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowBordered(gWin.window, bordered), "SDL_SetWindowBordered");
}

bool SetWindowFullscreen(bool fullscreen) {
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowFullscreen(gWin.window, fullscreen), "SDL_SetWindowFullscreen");
}

bool ToggleFullscreen() {
    EnsureInitialized();
    return SetWindowFullscreen(!IsWindowFullscreen());
}

bool ShowWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_ShowWindow(gWin.window), "SDL_ShowWindow");
}

bool HideWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_HideWindow(gWin.window), "SDL_HideWindow");
}

bool RaiseWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_RaiseWindow(gWin.window), "SDL_RaiseWindow");
}

bool MaximizeWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_MaximizeWindow(gWin.window), "SDL_MaximizeWindow");
}

bool MinimizeWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_MinimizeWindow(gWin.window), "SDL_MinimizeWindow");
}

bool RestoreWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_RestoreWindow(gWin.window), "SDL_RestoreWindow");
}

bool SyncWindow() {
    EnsureInitialized();
    return CheckWindowCall(SDL_SyncWindow(gWin.window), "SDL_SyncWindow");
}

bool IsWindowFullscreen() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gWin.window) & SDL_WINDOW_FULLSCREEN) != 0;
}

bool IsWindowHidden() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gWin.window) & SDL_WINDOW_HIDDEN) != 0;
}

bool IsWindowMinimized() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gWin.window) & SDL_WINDOW_MINIMIZED) != 0;
}

bool IsWindowMaximized() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gWin.window) & SDL_WINDOW_MAXIMIZED) != 0;
}

bool IsWindowFocused() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gWin.window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

bool IsWindowMouseFocused() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gWin.window) & SDL_WINDOW_MOUSE_FOCUS) != 0;
}

bool IsWindowResizable() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gWin.window) & SDL_WINDOW_RESIZABLE) != 0;
}

bool IsWindowBorderless() {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gWin.window) & SDL_WINDOW_BORDERLESS) != 0;
}

float GetWindowDisplayScale() {
    EnsureInitialized();
    return SDL_GetWindowDisplayScale(gWin.window);
}

float GetWindowPixelDensity() {
    EnsureInitialized();
    return SDL_GetWindowPixelDensity(gWin.window);
}

bool SetWindowIcon(const char* filePath) {
    EnsureInitialized();

    ImageFileData image;
    if (!LoadImageFile(filePath, image, 4)) {
        TraceLog(LogLevel::Warn, "WINDOW", (std::string("Failed to load window icon: ") + (filePath != nullptr ? filePath : "")).c_str());
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
        TraceLog(LogLevel::Warn, "WINDOW", (std::string("SDL_CreateSurfaceFrom failed: ") + SDL_GetError()).c_str());
        return false;
    }

    const bool ok = CheckWindowCall(SDL_SetWindowIcon(gWin.window, surface), "SDL_SetWindowIcon");
    SDL_DestroySurface(surface);
    return ok;
}

SDL_Window* GetNativeWindow() {
    EnsureInitialized();
    return gWin.window;
}

int GetFPS() {
    if (!gRendererPtr || gRendererPtr->GetFrameTime() <= 0.0f) {
        return 0;
    }

    return static_cast<int>(std::round(1.0f / gRendererPtr->GetFrameTime()));
}

float GetCurrentMonitorRefreshRate() {
    EnsureInitialized();

    const SDL_DisplayID displayId = SDL_GetDisplayForWindow(gWin.window);
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

bool StartTextInput() {
    EnsureInitialized();
    return CheckWindowCall(SDL_StartTextInput(gWin.window), "SDL_StartTextInput");
}

bool StopTextInput() {
    EnsureInitialized();
    return CheckWindowCall(SDL_StopTextInput(gWin.window), "SDL_StopTextInput");
}

}
