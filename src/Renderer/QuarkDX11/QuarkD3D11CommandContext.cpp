#include "QuarkD3D11CommandContext.hpp"

#include <cstring>

#if defined(_WIN32)
namespace qc {

void D3D11CommandContext::Initialize(const D3D11Device &device, D3D11SwapChain &swapChain,
                                     D3D11Pipeline &pipeline, D3D11Resources &resources, int, int)
{
    m_context = device.Context();
    m_swapChain = &swapChain;
    m_pipeline = &pipeline;
    m_resources = &resources;

    TraceLog(LogLevel::Trace, "D3D11", "Immediate command context initialized.");
}

void D3D11CommandContext::RefreshViewport(int width, int height)
{
    if (!m_context)
    {
        return;
    }

    D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                            0.0f, 1.0f};

    m_context->RSSetViewports(1, &viewport);

    TraceLog(LogLevel::Trace, "D3D11",
             TextFormat("Viewport refreshed (%dx%d).", width, height));
}

void D3D11CommandContext::BeginDrawing()
{
    if (!m_context || !m_swapChain)
    {
        return;
    }

    ID3D11RenderTargetView *renderTarget = m_swapChain->RenderTarget();
    m_context->OMSetRenderTargets(1, &renderTarget, nullptr);

}

void D3D11CommandContext::EndDrawing(bool vsync)
{
    if (m_swapChain)
    {
        m_swapChain->Present(vsync);
    }

}

void D3D11CommandContext::Clear(Color color)
{
    if (!m_context || !m_swapChain)
    {
        return;
    }

    const float clearColor[] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                                color.a / 255.0f};

    m_context->ClearRenderTargetView(m_swapChain->RenderTarget(), clearColor);

}

void D3D11CommandContext::DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color, int width,
                                       int height)
{
    if (!m_context || !m_pipeline || !m_resources || width <= 0 || height <= 0)
    {
        return;
    }

    const float colorValues[] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                                 color.a / 255.0f};
    const Vec2 points[] = {v1, v2, v3};
    float vertices[18]{};

    for (int index = 0; index < 3; ++index)
    {
        vertices[index * 6] = points[index].x / width * 2.0f - 1.0f;
        vertices[index * 6 + 1] = 1.0f - points[index].y / height * 2.0f;
        std::memcpy(vertices + index * 6 + 2, colorValues, sizeof(colorValues));
    }

    m_resources->UpdateDynamicBuffer(m_context, m_pipeline->VertexBuffer(), vertices,
                                     sizeof(vertices));

    m_pipeline->Bind(m_context);
    m_context->Draw(3, 0);

}

void D3D11CommandContext::Shutdown()
{
    TraceLog(LogLevel::Trace, "D3D11", "Immediate command context shut down.");

    m_context = nullptr;
    m_swapChain = nullptr;
    m_pipeline = nullptr;
    m_resources = nullptr;
}

} // namespace qc
#endif