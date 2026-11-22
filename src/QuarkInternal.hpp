#ifndef __QUARK_INTERNAL__
#define __QUARK_INTERNAL__
#include "QuarkCore/QuarkCore.hpp"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <string>

/**
 * @brief Shared internal state for windowing and input.
 */
struct WindowState
{
    SDL_Window* m_pWindow = nullptr;
    SDL_GLContext m_Context = nullptr;
    VkInstance m_VkInstance = VK_NULL_HANDLE;
    VkSurfaceKHR m_VkSurface = VK_NULL_HANDLE;

    bool m_ShouldClose = false;
    int m_TargetFps = 60;
    bool m_Vsync = true;
    bool m_VsyncSet = false;
    LogLevel m_MinimumLogLevel = LogLevel::Trace;

    int m_ActiveBlendMode = BLEND_ALPHA;
    int m_ActiveTextureFilter = TEXTURE_FILTER_BILINEAR;
    int m_ActiveTextureWrap = TEXTURE_WRAP_REPEAT;
    bool m_ScissorEnabled = false;
    Rectangle m_ScissorRect{};
    unsigned int m_EnabledGestures = GESTURE_NONE;
    int m_CurrentGesture = GESTURE_NONE;
    float m_GestureHoldDuration = 0.0f;
    Vec2 m_GestureDragVector{};
    float m_GestureDragAngle = 0.0f;
    Vec2 m_GesturePinchVector{};
    float m_GesturePinchAngle = 0.0f;

    std::array<bool, SDL_SCANCODE_COUNT> m_aCurrentKeys{};
    std::array<bool, SDL_SCANCODE_COUNT> m_aPreviousKeys{};
    std::array<bool, 8> m_aMouseButtons{};
    std::array<bool, 8> m_aPreviousMouseButtons{};
    std::array<std::array<bool, SDL_GAMEPAD_BUTTON_COUNT>, 16> m_aGamepadPressed{};
    std::array<std::array<bool, SDL_GAMEPAD_BUTTON_COUNT>, 16> m_aGamepadReleased{};
    int m_LastGamepadButtonPressed = -1;
    Vec2 m_MousePosition{};
    Vec2 m_MouseWheel{};

    std::vector<std::string> m_vDroppedFiles;
    bool m_WindowResized = false;
    bool m_InputPolled = false;
    unsigned int m_PendingConfigFlags = 0u;
};

/**
 * @brief Internal decoded image data in interleaved byte format.
 */
struct ImageFileData
{
    int m_Width = 0;
    int m_Height = 0;
    int m_Channels = 0;
    std::vector<uint8_t> m_vPixels;
};

namespace qci
{
    class IRenderer;
} // qci

extern qci::IRenderer* g_pRenderer;
extern WindowState g_Win;
extern TextureFilter g_TextureFilter;

QCAPI bool LoadImageFile(const char* pPath, ImageFileData& out, int desiredChannels = 4);

void EnsureInitialized();
void PumpInput();
void UpdateInputState();
SDL_Scancode ToSDLScancode(KeyboardKey key);
void CopyText(char* pDst, size_t size, const char* pSrc);
void WriteLog(LogLevel level, const char* pType, const std::string& message);
void ApplyPlatformDarkWindowBackground(SDL_Window* pWindow);
#endif // __QUARK_INTERNAL__
