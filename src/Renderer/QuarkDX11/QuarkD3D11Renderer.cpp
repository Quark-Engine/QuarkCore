#include "QuarkD3D11Renderer.hpp"

#if defined(_WIN32)
namespace qc
{

QuarkD3D11Renderer::~QuarkD3D11Renderer()
{
    Shutdown();
}

void QuarkD3D11Renderer::Init(SDL_Window *window, int width, int height)
{
    TraceLog(LogLevel::Info, "D3D11",
             TextFormat("Initializing renderer (Window: %dx%d)", width, height));
    m_window = window;
    m_width = width;
    m_height = height;
    m_lastFrame = std::chrono::steady_clock::now();

    m_device.Initialize();
    m_resources.Initialize(m_device.Get());
    m_swapChain.Initialize(m_device, window, width, height);
    m_pipeline.Initialize(m_device.Get(), m_shaderCompiler, m_resources);
    m_commands.Initialize(m_device, m_swapChain, m_pipeline, m_resources, width, height);
    RefreshViewport();
    TraceLog(LogLevel::Info, "D3D11", "Renderer initialized successfully.");
}

void QuarkD3D11Renderer::Shutdown()
{
    TraceLog(LogLevel::Info, "D3D11", "Shutting down D3D11 renderer...");

    m_commands.Shutdown();
    m_pipeline.Shutdown();
    m_resources.Shutdown();
    m_swapChain.Shutdown();
    m_device.Shutdown();
    m_window = nullptr;
    m_drawing = false;

    TraceLog(LogLevel::Info, "D3D11", "D3D11 renderer shut down successfully.");
}

void QuarkD3D11Renderer::RefreshViewport()
{
    m_commands.RefreshViewport(m_width, m_height);
}

void QuarkD3D11Renderer::BeginDrawing()
{
    const auto now = std::chrono::steady_clock::now();
    m_frameTime = std::chrono::duration<float>(now - m_lastFrame).count();
    m_lastFrame = now;
    m_drawing = true;
    m_commands.BeginDrawing();
}

void QuarkD3D11Renderer::EndDrawing()
{
    m_commands.EndDrawing(m_vsync);
    m_drawing = false;
}

void QuarkD3D11Renderer::ClearBackground(Color color)
{
    m_commands.Clear(color);
}

void QuarkD3D11Renderer::DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color)
{
    m_commands.DrawTriangle(v1, v2, v3, color, m_width, m_height);
}

} // namespace qc
#endif
