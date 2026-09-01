#include "QuarkGLDevice.hpp"

#include <glad/glad.h>

#include <stdexcept>
#include <string>

namespace qc {

QuarkGLDevice::~QuarkGLDevice() {
    Shutdown();
}

void QuarkGLDevice::Init(SDL_Window* window, int width, int height) {
    m_window = window;
    m_width = width;
    m_height = height;

    m_context = SDL_GL_CreateContext(window);
    if (!m_context) {
        throw std::runtime_error(std::string("SDL_GL_CreateContext: ") + SDL_GetError());
    }

#if defined(__ANDROID__)
    if (!gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress)) {
        throw std::runtime_error("Failed to initialize GLAD for OpenGL ES");
    }
#else
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        throw std::runtime_error("Failed to initialize GLAD");
    }
#endif

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    RefreshViewport();

    m_lastFrameCounter = SDL_GetPerformanceCounter();
}

void QuarkGLDevice::Shutdown() {
    if (m_context) {
        SDL_GL_DestroyContext(m_context);
        m_context = nullptr;
    }
    m_window = nullptr;
    m_drawing = false;
}

void QuarkGLDevice::RefreshViewport() {
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(m_window, &w, &h);
    m_width = w;
    m_height = h;
    glViewport(0, 0, w, h);
}

void QuarkGLDevice::BeginDrawing() {
    m_drawing = true;
    RefreshViewport();
}

void QuarkGLDevice::EndDrawing() {
    SDL_GL_SwapWindow(m_window);

    const std::uint64_t freq = SDL_GetPerformanceFrequency();
    if (m_targetFps > 0) {
        const std::uint64_t targetTicks = freq / static_cast<std::uint64_t>(m_targetFps);
        while (true) {
            const std::uint64_t now = SDL_GetPerformanceCounter();
            const std::uint64_t elapsed = now - m_lastFrameCounter;
            if (elapsed >= targetTicks) {
                break;
            }
            const std::uint64_t remaining = targetTicks - elapsed;
            if (remaining > freq / 500) {
                SDL_Delay(1);
            }
        }
    }

    const std::uint64_t frameEnd = SDL_GetPerformanceCounter();
    m_frameTime = static_cast<float>(frameEnd - m_lastFrameCounter) / static_cast<float>(freq);
    m_lastFrameCounter = frameEnd;
    m_drawing = false;
}

void QuarkGLDevice::SetTargetFPS(int fps) {
    m_targetFps = fps;
    if (!m_vsyncExplicitlySet && m_context) {
        if (fps == 0) {
            SDL_GL_SetSwapInterval(0);
        } else {
            SDL_GL_SetSwapInterval(1);
        }
    }
}

bool QuarkGLDevice::SetVSync(bool enabled) {
    m_vsync = enabled;
    m_vsyncExplicitlySet = true;
    if (m_context) {
        if (!SDL_GL_SetSwapInterval(enabled ? 1 : 0)) {
            return false;
        }
    }
    return true;
}

void QuarkGLDevice::ClearBackground(Color color) {
    const auto n = ToNormColor(color);
    glClearColor(n[0], n[1], n[2], n[3]);
    glClear(GL_COLOR_BUFFER_BIT);
}

std::array<float, 4> QuarkGLDevice::ToNormColor(Color color) {
    constexpr float inv = 1.0f / 255.0f;
    return { color.r * inv, color.g * inv, color.b * inv, color.a * inv };
}

} // namespace qc
