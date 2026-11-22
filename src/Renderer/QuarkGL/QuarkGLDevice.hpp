#ifndef __QUARK_GL_DEVICE__
#define __QUARK_GL_DEVICE__

#include "../QuarkIRenderer.hpp"

#include <SDL3/SDL.h>
#include <array>
#include <cstdint>

namespace qci
{
    class QuarkGLDevice
    {
    public:
        QuarkGLDevice() = default;
        ~QuarkGLDevice();

        void Init(SDL_Window* pWindow, int width, int height);
        void Shutdown();

        void RefreshViewport();
        void BeginDrawing();
        void EndDrawing();
        void ClearBackground(Color color);

        void SetTargetFPS(int fps);
        bool SetVSync(bool enabled);

        static std::array<float, 4> ToNormColor(Color color);

        SDL_Window* GetWindow() const
        {
            return m_pWindow;
        }

        SDL_GLContext GetContext() const
        {
            return m_Context;
        }

        int GetWidth() const
        {
            return m_Width;
        }

        int GetHeight() const
        {
            return m_Height;
        }

        bool IsDrawing() const
        {
            return m_Drawing;
        }

        float GetFrameTime() const
        {
            return m_FrameTime;
        }

        bool ShouldClose() const
        {
            return m_ShouldClose;
        }

        void SetShouldClose(bool value)
        {
            m_ShouldClose = value;
        }

    private:
        SDL_Window* m_pWindow = nullptr;
        SDL_GLContext m_Context = nullptr;
        int m_Width = 0;
        int m_Height = 0;
        int m_TargetFps = 60;
        bool m_Vsync = true;
        bool m_VsyncExplicitlySet = false;
        float m_FrameTime = 0.0f;
        bool m_Drawing = false;
        bool m_ShouldClose = false;
        std::uint64_t m_LastFrameCounter = 0;
    };
} // qci

#endif // __QUARK_GL_DEVICE__
