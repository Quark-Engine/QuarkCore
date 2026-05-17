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
    LogLevel minimumLogLevel = LogLevel::Trace;

    std::array<bool, SDL_SCANCODE_COUNT> currentKeys{};
    std::array<bool, SDL_SCANCODE_COUNT> previousKeys{};
    std::array<bool, 8> mouseButtons{};
    std::array<bool, 8> previousMouseButtons{};
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

QCAPI bool LoadImageFile(const char* path, ImageFileData& out, int desiredChannels = 4);

void PumpSystemEvents();

} // namespace qc
