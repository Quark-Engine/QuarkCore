#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/Quark3D.hpp"
#include "QuarkInternal.hpp"
#include "Renderer/QuarkIRenderer.hpp"

#include <cstring>

extern qci::IRenderer* g_pRenderer;

static bool CheckWindowCall(bool result, const char* pOperation)
{
    if (!result)
    {
        TraceLog(LogLevel::Warn, "WINDOW", (std::string(pOperation) + " failed: " + SDL_GetError()).c_str());
    }
    return result;
}

bool SetWindowTitle(const char* pTitle)
{
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowTitle(g_Win.m_pWindow, pTitle != nullptr ? pTitle : ""), "SDL_SetWindowTitle");
}

const char* GetWindowTitle()
{
    EnsureInitialized();
    return SDL_GetWindowTitle(g_Win.m_pWindow);
}

bool SetWindowPosition(int x, int y)
{
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowPosition(g_Win.m_pWindow, x, y), "SDL_SetWindowPosition");
}

Vec2i GetWindowPosition()
{
    EnsureInitialized();
    Vec2i position{};
    SDL_GetWindowPosition(g_Win.m_pWindow, &position.x, &position.y);
    return position;
}

bool SetWindowSize(int width, int height)
{
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowSize(g_Win.m_pWindow, width, height), "SDL_SetWindowSize");
}

Vec2i GetWindowSize()
{
    EnsureInitialized();
    Vec2i size{};
    SDL_GetWindowSize(g_Win.m_pWindow, &size.x, &size.y);
    return size;
}

Vec2i GetWindowSizeInPixels()
{
    EnsureInitialized();
    Vec2i size{};
    SDL_GetWindowSizeInPixels(g_Win.m_pWindow, &size.x, &size.y);
    return size;
}

bool SetWindowMinimumSize(int width, int height)
{
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowMinimumSize(g_Win.m_pWindow, width, height), "SDL_SetWindowMinimumSize");
}

Vec2i GetWindowMinimumSize()
{
    EnsureInitialized();
    Vec2i size{};
    SDL_GetWindowMinimumSize(g_Win.m_pWindow, &size.x, &size.y);
    return size;
}

bool SetWindowMaximumSize(int width, int height)
{
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowMaximumSize(g_Win.m_pWindow, width, height), "SDL_SetWindowMaximumSize");
}

Vec2i GetWindowMaximumSize()
{
    EnsureInitialized();
    Vec2i size{};
    SDL_GetWindowMaximumSize(g_Win.m_pWindow, &size.x, &size.y);
    return size;
}

bool SetWindowResizable(bool resizable)
{
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowResizable(g_Win.m_pWindow, resizable), "SDL_SetWindowResizable");
}

bool SetWindowBordered(bool bordered)
{
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowBordered(g_Win.m_pWindow, bordered), "SDL_SetWindowBordered");
}

bool SetWindowFullscreen(bool fullscreen)
{
    EnsureInitialized();
    return CheckWindowCall(SDL_SetWindowFullscreen(g_Win.m_pWindow, fullscreen), "SDL_SetWindowFullscreen");
}

bool ToggleFullscreen()
{
    EnsureInitialized();
    return SetWindowFullscreen(!IsWindowFullscreen());
}

bool ShowWindow()
{
    EnsureInitialized();
    return CheckWindowCall(SDL_ShowWindow(g_Win.m_pWindow), "SDL_ShowWindow");
}

bool HideWindow()
{
    EnsureInitialized();
    return CheckWindowCall(SDL_HideWindow(g_Win.m_pWindow), "SDL_HideWindow");
}

bool RaiseWindow()
{
    EnsureInitialized();
    return CheckWindowCall(SDL_RaiseWindow(g_Win.m_pWindow), "SDL_RaiseWindow");
}

bool MaximizeWindow()
{
    EnsureInitialized();
    return CheckWindowCall(SDL_MaximizeWindow(g_Win.m_pWindow), "SDL_MaximizeWindow");
}

bool MinimizeWindow()
{
    EnsureInitialized();
    return CheckWindowCall(SDL_MinimizeWindow(g_Win.m_pWindow), "SDL_MinimizeWindow");
}

bool RestoreWindow()
{
    EnsureInitialized();
    return CheckWindowCall(SDL_RestoreWindow(g_Win.m_pWindow), "SDL_RestoreWindow");
}

bool SyncWindow()
{
    EnsureInitialized();
    return CheckWindowCall(SDL_SyncWindow(g_Win.m_pWindow), "SDL_SyncWindow");
}

bool IsWindowFullscreen()
{
    EnsureInitialized();
    return (SDL_GetWindowFlags(g_Win.m_pWindow) & SDL_WINDOW_FULLSCREEN) != 0;
}

bool IsWindowHidden()
{
    EnsureInitialized();
    return (SDL_GetWindowFlags(g_Win.m_pWindow) & SDL_WINDOW_HIDDEN) != 0;
}

bool IsWindowMinimized()
{
    EnsureInitialized();
    return (SDL_GetWindowFlags(g_Win.m_pWindow) & SDL_WINDOW_MINIMIZED) != 0;
}

bool IsWindowMaximized()
{
    EnsureInitialized();
    return (SDL_GetWindowFlags(g_Win.m_pWindow) & SDL_WINDOW_MAXIMIZED) != 0;
}

bool IsWindowFocused()
{
    EnsureInitialized();
    return (SDL_GetWindowFlags(g_Win.m_pWindow) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

bool IsWindowMouseFocused()
{
    EnsureInitialized();
    return (SDL_GetWindowFlags(g_Win.m_pWindow) & SDL_WINDOW_MOUSE_FOCUS) != 0;
}

bool IsWindowResizable()
{
    EnsureInitialized();
    return (SDL_GetWindowFlags(g_Win.m_pWindow) & SDL_WINDOW_RESIZABLE) != 0;
}

bool IsWindowBorderless()
{
    EnsureInitialized();
    return (SDL_GetWindowFlags(g_Win.m_pWindow) & SDL_WINDOW_BORDERLESS) != 0;
}

float GetWindowDisplayScale()
{
    EnsureInitialized();
    return SDL_GetWindowDisplayScale(g_Win.m_pWindow);
}

float GetWindowPixelDensity()
{
    EnsureInitialized();
    return SDL_GetWindowPixelDensity(g_Win.m_pWindow);
}

void SetWindowIcon(Image image)
{
    EnsureInitialized();

    if (!IsImageValid(image) || image.width <= 0 || image.height <= 0)
    {
        TraceLog(LogLevel::Warn, "WINDOW", "SetWindowIcon: invalid image");
        return;
    }

    Color* pColors = LoadImageColors(image);
    if (!pColors)
    {
        TraceLog(LogLevel::Warn, "WINDOW", "SetWindowIcon: failed to read image colors");
        return;
    }

    const int w = image.width;
    const int h = image.height;
    std::vector<unsigned char> vRgba(static_cast<size_t>(w) * h * 4);
    for (int i = 0; i < w * h; ++i)
    {
        vRgba[static_cast<size_t>(i) * 4 + 0] = pColors[i].r;
        vRgba[static_cast<size_t>(i) * 4 + 1] = pColors[i].g;
        vRgba[static_cast<size_t>(i) * 4 + 2] = pColors[i].b;
        vRgba[static_cast<size_t>(i) * 4 + 3] = pColors[i].a;
    }
    UnloadImageColors(pColors);

    SDL_Surface* pSurface = SDL_CreateSurfaceFrom(
        w, h, SDL_PIXELFORMAT_RGBA8888, vRgba.data(), w * 4);
    if (pSurface == nullptr)
    {
        TraceLog(LogLevel::Warn, "WINDOW", (std::string("SDL_CreateSurfaceFrom failed: ") + SDL_GetError()).c_str());
        return;
    }

    CheckWindowCall(SDL_SetWindowIcon(g_Win.m_pWindow, pSurface), "SDL_SetWindowIcon");
    SDL_DestroySurface(pSurface);
}

bool IsWindowResized()
{
    EnsureInitialized();
    return g_Win.m_WindowResized;
}

bool IsWindowState(unsigned int flag)
{
    EnsureInitialized();
    return (SDL_GetWindowFlags(g_Win.m_pWindow) & static_cast<Uint32>(flag)) != 0;
}

void SetWindowState(unsigned int flags)
{
    EnsureInitialized();
    if ((flags & SDL_WINDOW_FULLSCREEN) != 0)
    {
        SDL_SetWindowFullscreen(g_Win.m_pWindow, true);
    }
    if ((flags & SDL_WINDOW_MAXIMIZED) != 0)
    {
        MaximizeWindow();
    }
    if ((flags & SDL_WINDOW_HIDDEN) != 0)
    {
        HideWindow();
    }
    if ((flags & SDL_WINDOW_RESIZABLE) != 0)
    {
        SetWindowResizable(true);
    }
    if ((flags & SDL_WINDOW_BORDERLESS) != 0)
    {
        SetWindowBordered(false);
    }
}

void ClearWindowState(unsigned int flags)
{
    EnsureInitialized();
    if ((flags & SDL_WINDOW_FULLSCREEN) != 0)
    {
        SDL_SetWindowFullscreen(g_Win.m_pWindow, false);
    }
    if ((flags & SDL_WINDOW_MAXIMIZED) != 0)
    {
        RestoreWindow();
    }
    if ((flags & SDL_WINDOW_HIDDEN) != 0)
    {
        ShowWindow();
    }
    if ((flags & SDL_WINDOW_RESIZABLE) != 0)
    {
        SetWindowResizable(false);
    }
    if ((flags & SDL_WINDOW_BORDERLESS) != 0)
    {
        SetWindowBordered(true);
    }
}

void ToggleBorderlessWindowed()
{
    EnsureInitialized();
    SetWindowBordered(!IsWindowBorderless());
}

void SetWindowIcons(Image* pImages, int count)
{
    if (pImages == nullptr || count <= 0)
    {
        return;
    }
    SetWindowIcon(pImages[0]);
}

void SetWindowMonitor(int monitor)
{
    EnsureInitialized();
    int displayCount = 0;
    SDL_DisplayID* pDisplays = SDL_GetDisplays(&displayCount);
    if (pDisplays == nullptr || displayCount <= 0 || monitor < 0 || monitor >= displayCount)
    {
        if (pDisplays)
        {
            SDL_free(pDisplays);
        }
        return;
    }

    SDL_Rect rect{};
    if (SDL_GetDisplayBounds(pDisplays[monitor], &rect))
    {
        SDL_SetWindowPosition(g_Win.m_pWindow, rect.x, rect.y);
    }
    SDL_free(pDisplays);
}

void SetWindowOpacity(float opacity)
{
    EnsureInitialized();
    SDL_SetWindowOpacity(g_Win.m_pWindow, opacity);
}

void SetWindowFocused()
{
    EnsureInitialized();
    SDL_RaiseWindow(g_Win.m_pWindow);
}

int GetRenderWidth()
{
    EnsureInitialized();
    return GetWindowSizeInPixels().x;
}

int GetRenderHeight()
{
    EnsureInitialized();
    return GetWindowSizeInPixels().y;
}

int GetMonitorCount()
{
    int count = 0;
    SDL_GetDisplays(&count);
    return count;
}

int GetCurrentMonitor()
{
    EnsureInitialized();
    const SDL_DisplayID displayId = SDL_GetDisplayForWindow(g_Win.m_pWindow);
    int count = 0;
    SDL_DisplayID* pDisplays = SDL_GetDisplays(&count);
    if (!pDisplays)
    {
        return 0;
    }
    for (int i = 0; i < count; ++i)
    {
        if (pDisplays[i] == displayId)
        {
            SDL_free(pDisplays);
            return i;
        }
    }
    SDL_free(pDisplays);
    return 0;
}

Vec2 GetMonitorPosition(int monitor)
{
    EnsureInitialized();
    int count = 0;
    SDL_DisplayID* pDisplays = SDL_GetDisplays(&count);
    if (!pDisplays || monitor < 0 || monitor >= count)
    {
        if (pDisplays)
        {
            SDL_free(pDisplays);
        }
        return Vec2{};
    }

    SDL_Rect rect{};
    Vec2 result{};
    if (SDL_GetDisplayBounds(pDisplays[monitor], &rect))
    {
        result = Vec2(static_cast<float>(rect.x), static_cast<float>(rect.y));
    }
    SDL_free(pDisplays);
    return result;
}

int GetMonitorWidth(int monitor)
{
    int count = 0;
    SDL_DisplayID* pDisplays = SDL_GetDisplays(&count);
    if (!pDisplays || monitor < 0 || monitor >= count)
    {
        if (pDisplays)
        {
            SDL_free(pDisplays);
        }
        return 0;
    }

    SDL_Rect rect{};
    int value = 0;
    if (SDL_GetDisplayBounds(pDisplays[monitor], &rect))
    {
        value = rect.w;
    }
    SDL_free(pDisplays);
    return value;
}

int GetMonitorHeight(int monitor)
{
    int count = 0;
    SDL_DisplayID* pDisplays = SDL_GetDisplays(&count);
    if (!pDisplays || monitor < 0 || monitor >= count)
    {
        if (pDisplays)
        {
            SDL_free(pDisplays);
        }
        return 0;
    }

    SDL_Rect rect{};
    int value = 0;
    if (SDL_GetDisplayBounds(pDisplays[monitor], &rect))
    {
        value = rect.h;
    }
    SDL_free(pDisplays);
    return value;
}

int GetMonitorPhysicalWidth(int monitor)
{
    return GetMonitorWidth(monitor);
}

int GetMonitorPhysicalHeight(int monitor)
{
    return GetMonitorHeight(monitor);
}

const char* GetMonitorName(int monitor)
{
    int count = 0;
    SDL_DisplayID* pDisplays = SDL_GetDisplays(&count);
    if (!pDisplays || monitor < 0 || monitor >= count)
    {
        if (pDisplays)
        {
            SDL_free(pDisplays);
        }
        return "";
    }

    const char* pName = SDL_GetDisplayName(pDisplays[monitor]);
    SDL_free(pDisplays);
    return pName ? pName : "";
}

void SetClipboardText(const char* pText)
{
    if (pText == nullptr)
    {
        return;
    }
    SDL_SetClipboardText(pText);
}

const char* GetClipboardText()
{
    static thread_local std::string s_ClipboardText;
    char* pText = SDL_GetClipboardText();
    s_ClipboardText = pText ? pText : "";
    SDL_free(pText);
    return s_ClipboardText.c_str();
}

Image GetClipboardImage()
{
    return Image{};
}

#if defined(_WIN32)
extern "C"
{
    __declspec(dllimport) void* __stdcall CreateSolidBrush(unsigned long color);
    __declspec(dllimport) long long __stdcall SetClassLongPtrW(void* pHWnd, int nIndex, long long dwNewLong);
}
#endif

void ApplyPlatformDarkWindowBackground(SDL_Window* pWindow)
{
#if defined(_WIN32)
    if (!pWindow)
    {
        return;
    }
    const SDL_PropertiesID props = SDL_GetWindowProperties(pWindow);
    void* pHwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!pHwnd)
    {
        return;
    }
    static void* s_pDarkBrush = CreateSolidBrush(24u | (24u << 8) | (28u << 16));
    if (s_pDarkBrush)
    {
        constexpr int GCLP_HBRBACKGROUND = -10;
        SetClassLongPtrW(pHwnd, GCLP_HBRBACKGROUND, reinterpret_cast<long long>(s_pDarkBrush));
    }
#else
    (void)pWindow;
#endif
}

void SetConfigFlags(unsigned int flags)
{
    if (g_Win.m_pWindow == nullptr)
    {
        g_Win.m_PendingConfigFlags |= flags;
        if ((flags & FLAG_VSYNC_HINT) != 0u)
        {
            g_Win.m_Vsync = true;
            g_Win.m_VsyncSet = true;
        }
        return;
    }
    if ((flags & FLAG_VSYNC_HINT) != 0u)
    {
        SetVSync(true);
    }
    if ((flags & FLAG_FULLSCREEN_MODE) != 0u)
    {
        SetWindowFullscreen(true);
    }
    if ((flags & FLAG_WINDOW_RESIZABLE) != 0u)
    {
        SetWindowResizable(true);
    }
    if ((flags & FLAG_BORDERLESS_WINDOWED_MODE) != 0u)
    {
        SetWindowBordered(false);
    }
    if ((flags & FLAG_WINDOW_HIDDEN) != 0u)
    {
        HideWindow();
    }
    if ((flags & FLAG_WINDOW_MAXIMIZED) != 0u)
    {
        MaximizeWindow();
    }
    if ((flags & FLAG_WINDOW_UNDECORATED) != 0u)
    {
        SetWindowBordered(false);
    }
}

void SwapScreenBuffer()
{
#if defined(QC_ENABLE_OPENGL)
    if (g_pRenderer && g_pRenderer->GetRendererType() == RendererType::OpenGL)
    {
        SDL_GL_SwapWindow(g_Win.pWindow);
    }
#else
    (void)g_Win;
#endif
}

void PollInputEvents()
{
    EnsureInitialized();
    SDL_PumpEvents();
    PumpInput();
}

void TakeScreenshot(const char* pFileName)
{
    if (!pFileName || std::strlen(pFileName) == 0)
    {
        return;
    }
    Image screenshot = LoadImageFromScreen();
    if (IsImageValid(screenshot))
    {
        ExportImage(screenshot, pFileName);
        UnloadImage(screenshot);
    }
}

bool OpenURL(const char* pUrl)
{
    if (pUrl == nullptr || pUrl[0] == '\0')
    {
        return false;
    }
    return SDL_OpenURL(pUrl);
}

SDL_Window* GetNativeWindow()
{
    EnsureInitialized();
    return g_Win.m_pWindow;
}

int GetFPS()
{
    if (!g_pRenderer || g_pRenderer->GetFrameTime() <= 0.0f)
    {
        return 0;
    }

    return static_cast<int>(std::round(1.0f / g_pRenderer->GetFrameTime()));
}

float GetCurrentMonitorRefreshRate()
{
    EnsureInitialized();

    const SDL_DisplayID displayId = SDL_GetDisplayForWindow(g_Win.m_pWindow);
    if (displayId == 0)
    {
        return 0.0f;
    }

    const SDL_DisplayMode* pMode = SDL_GetCurrentDisplayMode(displayId);
    if (pMode == nullptr)
    {
        return 0.0f;
    }

    if (pMode->refresh_rate > 0.0f)
    {
        return pMode->refresh_rate;
    }

    if (pMode->refresh_rate_numerator > 0 && pMode->refresh_rate_denominator > 0)
    {
        return static_cast<float>(pMode->refresh_rate_numerator) /
               static_cast<float>(pMode->refresh_rate_denominator);
    }

    return 0.0f;
}

bool StartTextInput()
{
    EnsureInitialized();
    return CheckWindowCall(SDL_StartTextInput(g_Win.m_pWindow), "SDL_StartTextInput");
}

bool StopTextInput()
{
    EnsureInitialized();
    return CheckWindowCall(SDL_StopTextInput(g_Win.m_pWindow), "SDL_StopTextInput");
}
