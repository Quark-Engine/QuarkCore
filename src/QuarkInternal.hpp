#pragma once
#include "QuarkCore/QuarkCore.hpp"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <string>

namespace qc {

/**
 * @brief Shared internal state for windowing and input.
 */
struct WindowState {
    SDL_Window* window      = nullptr;
    SDL_GLContext context   = nullptr;
    VkInstance vkInstance   = VK_NULL_HANDLE;
    VkSurfaceKHR vkSurface  = VK_NULL_HANDLE;

    bool  shouldClose       = false;
    int   targetFps         = 60;
    bool  vsync             = true;
    bool  vsyncSet          = false;
    LogLevel minimumLogLevel = LogLevel::Trace;

    std::array<bool, SDL_SCANCODE_COUNT> currentKeys{};
    std::array<bool, SDL_SCANCODE_COUNT> previousKeys{};
    std::array<bool, 8> mouseButtons{};
    std::array<bool, 8> previousMouseButtons{};
    std::array<std::array<bool, SDL_GAMEPAD_BUTTON_COUNT>, 16> gamepadPressed{};
    std::array<std::array<bool, SDL_GAMEPAD_BUTTON_COUNT>, 16> gamepadReleased{};
    int lastGamepadButtonPressed = -1;
    Vec2  mousePosition{};
    Vec2  mouseWheel{};

    std::vector<Event> events;
    std::vector<std::string> droppedFiles;
    SDL_Event nativeEvent{};
    std::size_t nextEventIndex = 0;
    bool  eventsReady = false;
};

/**
 * @brief Internal decoded image data in interleaved byte format.
 */
struct ImageFileData {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<uint8_t> pixels;
};

extern WindowState gWin;
extern TextureFilterMode gTextureFilterMode;

QCAPI bool LoadImageFile(const char* path, ImageFileData& out, int desiredChannels = 4);

void EnsureInitialized();
void PumpSystemEvents();
void UpdateInputFromEvents();
void CopyText(char* dst, size_t size, const char* src);
void WriteLog(LogLevel level, const char* type, const std::string& message);

} // namespace qc
