#include "QuarkGLDevice.hpp"

#include <glad/glad.h>

#include <stdexcept>
#include <string>

namespace qci
{
    QuarkGLDevice::~QuarkGLDevice()
    {
        Shutdown();
    }

    void QuarkGLDevice::Init(SDL_Window* pWindow, int width, int height)
    {
        m_pWindow = pWindow;
        m_Width = width;
        m_Height = height;

        m_Context = SDL_GL_CreateContext(pWindow);
        if (!m_Context)
        {
            throw std::runtime_error(std::string("SDL_GL_CreateContext: ") + SDL_GetError());
        }

#if defined(__ANDROID__)
        if (!gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress))
        {
            throw std::runtime_error("Failed to initialize GLAD for OpenGL ES");
        }
#else
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
        {
            throw std::runtime_error("Failed to initialize GLAD");
        }
#endif

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        RefreshViewport();

        m_LastFrameCounter = SDL_GetPerformanceCounter();
    }

    void QuarkGLDevice::Shutdown()
    {
        if (m_Context)
        {
            SDL_GL_DestroyContext(m_Context);
            m_Context = nullptr;
        }
        m_pWindow = nullptr;
        m_Drawing = false;
    }

    void QuarkGLDevice::RefreshViewport()
    {
        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(m_pWindow, &w, &h);
        m_Width = w;
        m_Height = h;
        glViewport(0, 0, w, h);
    }

    void QuarkGLDevice::BeginDrawing()
    {
        m_Drawing = true;
        RefreshViewport();
    }

    void QuarkGLDevice::EndDrawing()
    {
        SDL_GL_SwapWindow(m_pWindow);

        const std::uint64_t freq = SDL_GetPerformanceFrequency();
        if (m_TargetFps > 0)
        {
            const std::uint64_t targetTicks = freq / static_cast<std::uint64_t>(m_TargetFps);
            while (true)
            {
                const std::uint64_t now = SDL_GetPerformanceCounter();
                const std::uint64_t elapsed = now - m_LastFrameCounter;
                if (elapsed >= targetTicks)
                {
                    break;
                }
                const std::uint64_t remaining = targetTicks - elapsed;
                if (remaining > freq / 500)
                {
                    SDL_Delay(1);
                }
            }
        }

        const std::uint64_t frameEnd = SDL_GetPerformanceCounter();
        m_FrameTime = static_cast<float>(frameEnd - m_LastFrameCounter) / static_cast<float>(freq);
        m_LastFrameCounter = frameEnd;
        m_Drawing = false;
    }

    void QuarkGLDevice::SetTargetFPS(int fps)
    {
        m_TargetFps = fps;
        if (!m_VsyncExplicitlySet && m_Context)
        {
            if (fps == 0)
            {
                SDL_GL_SetSwapInterval(0);
            }
            else
            {
                SDL_GL_SetSwapInterval(1);
            }
        }
    }

    bool QuarkGLDevice::SetVSync(bool enabled)
    {
        m_Vsync = enabled;
        m_VsyncExplicitlySet = true;
        if (m_Context)
        {
            if (!SDL_GL_SetSwapInterval(enabled ? 1 : 0))
            {
                return false;
            }
        }
        return true;
    }

    void QuarkGLDevice::ClearBackground(Color color)
    {
        const auto aNorm = ToNormColor(color);
        glClearColor(aNorm[0], aNorm[1], aNorm[2], aNorm[3]);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    std::array<float, 4> QuarkGLDevice::ToNormColor(Color color)
    {
        constexpr float inv = 1.0f / 255.0f;
        return {color.r * inv, color.g * inv, color.b * inv, color.a * inv};
    }
} // qci
