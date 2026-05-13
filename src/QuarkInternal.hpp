#pragma once
#include "QuarkCore/QuarkCore.hpp"
#include <SDL3/SDL.h>
#include <vector>
#include <array>

namespace qc {

/**
 * @brief Shared internal state for windowing and input.
 */
struct WindowState {
    SDL_Window* window      = nullptr;
    SDL_GLContext context   = nullptr;
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
    SDL_Event nativeEvent{};
    std::size_t nextEventIndex = 0;
    bool  eventsReady = false;
};

/**
 * @brief Internal PNG data structure.
 */
struct PngImageData { int width=0, height=0; std::vector<uint8_t> pixels; };
QCAPI bool LoadPngImage(const char* path, PngImageData& out);

void PumpSystemEvents();

} // namespace qc