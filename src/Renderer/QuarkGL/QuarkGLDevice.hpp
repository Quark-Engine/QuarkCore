#ifndef __QUARK_GL_DEVICE__
#define __QUARK_GL_DEVICE__

#include "../QuarkIRenderer.hpp"

#include <SDL3/SDL.h>
#include <array>
#include <cstdint>

namespace qc {

class QuarkGLDevice {
public:
    QuarkGLDevice() = default;
    ~QuarkGLDevice();

    void Init(SDL_Window* window, int width, int height);
    void Shutdown();

    void RefreshViewport();
    void BeginDrawing();
    void EndDrawing();
    void ClearBackground(Color color);

    void SetTargetFPS(int fps);
    bool SetVSync(bool enabled);

    static std::array<float, 4> ToNormColor(Color color);

    SDL_Window* GetWindow() const { return m_window; }
    SDL_GLContext GetContext() const { return m_context; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    bool IsDrawing() const { return m_drawing; }
    float GetFrameTime() const { return m_frameTime; }
    bool ShouldClose() const { return m_shouldClose; }
    void SetShouldClose(bool value) { m_shouldClose = value; }

private:
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_context = nullptr;
    int m_width = 0;
    int m_height = 0;
    int m_targetFps = 60;
    bool m_vsync = true;
    bool m_vsyncExplicitlySet = false;
    float m_frameTime = 0.0f;
    bool m_drawing = false;
    bool m_shouldClose = false;
    std::uint64_t m_lastFrameCounter = 0;
};

} // namespace qc

#endif // __QUARK_GL_DEVICE__
