#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/Quark3D.hpp"
#include "QuarkInternal.hpp"
#include "Renderer/QuarkIRenderer.hpp"

#include <cstring>

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

void SetWindowIcon(Image image) {
    EnsureInitialized();

    if (!IsImageValid(image) || image.width <= 0 || image.height <= 0) {
        TraceLog(LogLevel::Warn, "WINDOW", "SetWindowIcon: invalid image");
        return;
    }

    Color* colors = LoadImageColors(image);
    if (!colors) {
        TraceLog(LogLevel::Warn, "WINDOW", "SetWindowIcon: failed to read image colors");
        return;
    }

    const int w = image.width, h = image.height;
    std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
    for (int i = 0; i < w * h; ++i) {
        rgba[static_cast<size_t>(i) * 4 + 0] = colors[i].r;
        rgba[static_cast<size_t>(i) * 4 + 1] = colors[i].g;
        rgba[static_cast<size_t>(i) * 4 + 2] = colors[i].b;
        rgba[static_cast<size_t>(i) * 4 + 3] = colors[i].a;
    }
    UnloadImageColors(colors);

    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        w, h, SDL_PIXELFORMAT_RGBA8888, rgba.data(), w * 4);
    if (surface == nullptr) {
        TraceLog(LogLevel::Warn, "WINDOW", (std::string("SDL_CreateSurfaceFrom failed: ") + SDL_GetError()).c_str());
        return;
    }

    CheckWindowCall(SDL_SetWindowIcon(gWin.window, surface), "SDL_SetWindowIcon");
    SDL_DestroySurface(surface);
}

bool IsWindowResized() {
    EnsureInitialized();
    return gWin.nativeEvent.type == SDL_EVENT_WINDOW_RESIZED ||
           gWin.nativeEvent.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
}

bool IsWindowState(unsigned int flag) {
    EnsureInitialized();
    return (SDL_GetWindowFlags(gWin.window) & static_cast<Uint32>(flag)) != 0;
}

void SetWindowState(unsigned int flags) {
    EnsureInitialized();
    if ((flags & SDL_WINDOW_FULLSCREEN) != 0) {
        SDL_SetWindowFullscreen(gWin.window, true);
    }
    if ((flags & SDL_WINDOW_MAXIMIZED) != 0) {
        MaximizeWindow();
    }
    if ((flags & SDL_WINDOW_HIDDEN) != 0) {
        HideWindow();
    }
    if ((flags & SDL_WINDOW_RESIZABLE) != 0) {
        SetWindowResizable(true);
    }
    if ((flags & SDL_WINDOW_BORDERLESS) != 0) {
        SetWindowBordered(false);
    }
}

void ClearWindowState(unsigned int flags) {
    EnsureInitialized();
    if ((flags & SDL_WINDOW_FULLSCREEN) != 0) {
        SDL_SetWindowFullscreen(gWin.window, false);
    }
    if ((flags & SDL_WINDOW_MAXIMIZED) != 0) {
        RestoreWindow();
    }
    if ((flags & SDL_WINDOW_HIDDEN) != 0) {
        ShowWindow();
    }
    if ((flags & SDL_WINDOW_RESIZABLE) != 0) {
        SetWindowResizable(false);
    }
    if ((flags & SDL_WINDOW_BORDERLESS) != 0) {
        SetWindowBordered(true);
    }
}

void ToggleBorderlessWindowed() {
    EnsureInitialized();
    SetWindowBordered(!IsWindowBorderless());
}

void SetWindowIcons(Image* images, int count) {
    if (images == nullptr || count <= 0) {
        return;
    }
    SetWindowIcon(images[0]);
}

void SetWindowMonitor(int monitor) {
    EnsureInitialized();
    int displayCount = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);
    if (displays == nullptr || displayCount <= 0 || monitor < 0 || monitor >= displayCount) {
        if (displays) {
            SDL_free(displays);
        }
        return;
    }

    SDL_Rect rect{};
    if (SDL_GetDisplayBounds(displays[monitor], &rect)) {
        SDL_SetWindowPosition(gWin.window, rect.x, rect.y);
    }
    SDL_free(displays);
}

void SetWindowOpacity(float opacity) {
    EnsureInitialized();
    SDL_SetWindowOpacity(gWin.window, opacity);
}

void SetWindowFocused() {
    EnsureInitialized();
    SDL_RaiseWindow(gWin.window);
}

int GetRenderWidth() {
    EnsureInitialized();
    return GetWindowSizeInPixels().x;
}

int GetRenderHeight() {
    EnsureInitialized();
    return GetWindowSizeInPixels().y;
}

int GetMonitorCount() {
    int count = 0;
    SDL_GetDisplays(&count);
    return count;
}

int GetCurrentMonitor() {
    EnsureInitialized();
    const SDL_DisplayID displayId = SDL_GetDisplayForWindow(gWin.window);
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (!displays) {
        return 0;
    }
    for (int i = 0; i < count; ++i) {
        if (displays[i] == displayId) {
            SDL_free(displays);
            return i;
        }
    }
    SDL_free(displays);
    return 0;
}

Vec2 GetMonitorPosition(int monitor) {
    EnsureInitialized();
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (!displays || monitor < 0 || monitor >= count) {
        if (displays) SDL_free(displays);
        return Vec2{};
    }

    SDL_Rect rect{};
    Vec2 result{};
    if (SDL_GetDisplayBounds(displays[monitor], &rect)) {
        result = Vec2(static_cast<float>(rect.x), static_cast<float>(rect.y));
    }
    SDL_free(displays);
    return result;
}

int GetMonitorWidth(int monitor) {
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (!displays || monitor < 0 || monitor >= count) {
        if (displays) SDL_free(displays);
        return 0;
    }

    SDL_Rect rect{};
    int value = 0;
    if (SDL_GetDisplayBounds(displays[monitor], &rect)) {
        value = rect.w;
    }
    SDL_free(displays);
    return value;
}

int GetMonitorHeight(int monitor) {
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (!displays || monitor < 0 || monitor >= count) {
        if (displays) SDL_free(displays);
        return 0;
    }

    SDL_Rect rect{};
    int value = 0;
    if (SDL_GetDisplayBounds(displays[monitor], &rect)) {
        value = rect.h;
    }
    SDL_free(displays);
    return value;
}

int GetMonitorPhysicalWidth(int monitor) {
    return GetMonitorWidth(monitor);
}

int GetMonitorPhysicalHeight(int monitor) {
    return GetMonitorHeight(monitor);
}

const char* GetMonitorName(int monitor) {
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (!displays || monitor < 0 || monitor >= count) {
        if (displays) SDL_free(displays);
        return "";
    }

    const char* name = SDL_GetDisplayName(displays[monitor]);
    SDL_free(displays);
    return name ? name : "";
}

void SetClipboardText(const char* text) {
    if (text == nullptr) {
        return;
    }
    SDL_SetClipboardText(text);
}

const char* GetClipboardText() {
    static thread_local std::string clipboardText;
    char* text = SDL_GetClipboardText();
    clipboardText = text ? text : "";
    SDL_free(text);
    return clipboardText.c_str();
}

Image GetClipboardImage() {
    return Image{};
}

void EnableEventWaiting() {
}

void DisableEventWaiting() {
}

void SetConfigFlags(unsigned int flags) {
    if ((flags & FLAG_VSYNC_HINT) != 0u) {
        SetVSync(true);
    }
    if ((flags & FLAG_FULLSCREEN_MODE) != 0u) {
        SetWindowFullscreen(true);
    }
    if ((flags & FLAG_WINDOW_RESIZABLE) != 0u) {
        SetWindowResizable(true);
    }
    if ((flags & FLAG_BORDERLESS_WINDOWED_MODE) != 0u) {
        SetWindowBordered(false);
    }
    if ((flags & FLAG_WINDOW_HIDDEN) != 0u) {
        HideWindow();
    }
    if ((flags & FLAG_WINDOW_MAXIMIZED) != 0u) {
        MaximizeWindow();
    }
    if ((flags & FLAG_WINDOW_UNDECORATED) != 0u) {
        SetWindowBordered(false);
    }
}

void SwapScreenBuffer() {
#if defined(QC_ENABLE_OPENGL)
    if (gRendererPtr && gRendererPtr->GetRendererType() == RendererType::OpenGL) {
        SDL_GL_SwapWindow(gWin.window);
    }
#else
    (void)gWin;
#endif
}

void PollInputEvents() {
    EnsureInitialized();
    SDL_PumpEvents();
    PumpSystemEvents();
}

void TakeScreenshot(const char* fileName) {
    if (!fileName || std::strlen(fileName) == 0) {
        return;
    }
    Image screenshot = LoadImageFromScreen();
    if (IsImageValid(screenshot)) {
        ExportImage(screenshot, fileName);
        UnloadImage(screenshot);
    }
}

bool OpenURL(const char* url) {
    if (url == nullptr || url[0] == '\0') {
        return false;
    }
    return SDL_OpenURL(url);
}

void SetTraceLogCallback(TraceLogCallback callback) {
    static TraceLogCallback gCallback = nullptr;
    gCallback = callback;
    (void)gCallback;
    if (callback != nullptr) {
        TraceLog(LogLevel::Info, "WINDOW", "Trace log callback registered");
    }
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
