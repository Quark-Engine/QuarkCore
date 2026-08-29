#include "QuarkD3D11CommandContext.hpp"

#include <cstring>
#include <cmath>
#include <vector>

#if defined(_WIN32)
namespace qc {

void D3D11CommandContext::Initialize(const D3D11Device &device, D3D11SwapChain &swapChain,
                                     D3D11Pipeline &pipeline, D3D11Resources &resources,
                                     int width, int height)
{
    m_context = device.Context();
    m_swapChain = &swapChain;
    m_pipeline = &pipeline;
    m_resources = &resources;
    m_activeRenderTarget = swapChain.RenderTarget();
    m_defaultWidth = width;
    m_defaultHeight = height;
    m_activeWidth = width;
    m_activeHeight = height;

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
}

void D3D11CommandContext::SetDefaultViewportSize(int width, int height)
{
    m_defaultWidth = width;
    m_defaultHeight = height;
}

void D3D11CommandContext::BeginDrawing()
{
    if (!m_context || !m_swapChain)
    {
        return;
    }

    ID3D11RenderTargetView *renderTarget = m_swapChain->RenderTarget();
    m_activeRenderTarget = renderTarget;
    m_activeWidth = m_defaultWidth > 0 ? m_defaultWidth : m_activeWidth;
    m_activeHeight = m_defaultHeight > 0 ? m_defaultHeight : m_activeHeight;
    m_context->OMSetRenderTargets(1, &renderTarget, m_swapChain->DepthStencilView());
    RefreshViewport(m_activeWidth, m_activeHeight);
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

    m_context->ClearRenderTargetView(m_activeRenderTarget, clearColor);
    ClearDepthStencil();

}

void D3D11CommandContext::ClearDepthStencil()
{
    if (!m_context || !m_swapChain)
    {
        return;
    }

    ID3D11DepthStencilView *depthStencil = m_swapChain->DepthStencilView();
    if (depthStencil)
    {
        m_context->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                         1.0f, 0);
    }
}

void D3D11CommandContext::Draw3D(const float *vertices, UINT vertexCount,
                                 D3D_PRIMITIVE_TOPOLOGY topology)
{
    if (!m_context || !m_pipeline || !m_resources || !m_swapChain ||
        vertices == nullptr || vertexCount == 0)
    {
        return;
    }

    ID3D11DepthStencilView *depthStencil = m_swapChain->DepthStencilView();
    if (!depthStencil)
    {
        return;
    }

    constexpr UINT kMaxVertices = 32768;
    if (vertexCount > kMaxVertices)
    {
        return;
    }

    m_resources->UpdateDynamicBuffer(m_context, m_pipeline->VertexBuffer3D(), vertices,
                                     static_cast<size_t>(vertexCount) * sizeof(float) * 8);

    m_pipeline->Bind3D(m_context);
    m_context->IASetPrimitiveTopology(topology);
    m_context->Draw(vertexCount, 0);
}

void D3D11CommandContext::Draw3DTextured(const float *vertices, UINT vertexCount,
                                         ID3D11ShaderResourceView *shaderResource)
{
    if (!m_context || !m_pipeline || !m_resources || !vertices || vertexCount == 0)
    {
        return;
    }

    constexpr UINT kMaxVertices = 32768;
    if (vertexCount > kMaxVertices)
    {
        return;
    }

    m_resources->UpdateDynamicBuffer(m_context, m_pipeline->VertexBuffer3D(), vertices,
                                     static_cast<size_t>(vertexCount) * sizeof(float) * 10);
    m_pipeline->BindTexture3D(m_context, shaderResource);
    m_context->Draw(vertexCount, 0);
}

void D3D11CommandContext::DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color, int, int)
{
    if (!m_context || !m_pipeline || !m_resources || m_activeWidth <= 0 || m_activeHeight <= 0)
    {
        return;
    }

    const float colorValues[] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                                 color.a / 255.0f};
    const Vec2 points[] = {
        m_camera2DActive ? GetWorldToScreen2D(v1, m_camera2D) : v1,
        m_camera2DActive ? GetWorldToScreen2D(v2, m_camera2D) : v2,
        m_camera2DActive ? GetWorldToScreen2D(v3, m_camera2D) : v3
    };

    if (m_shaderOverride.Active())
    {
        const UINT floatsPerVertex = m_shaderOverride.strideBytes / sizeof(float);
        std::vector<float> vertices(static_cast<size_t>(floatsPerVertex) * 3, 0.0f);

        for (int index = 0; index < 3; ++index)
        {
            float *vertex = vertices.data() + static_cast<size_t>(index) * floatsPerVertex;
            vertex[m_shaderOverride.positionOffset / sizeof(float)] =
                points[index].x / m_activeWidth * 2.0f - 1.0f;
            vertex[m_shaderOverride.positionOffset / sizeof(float) + 1] =
                1.0f - points[index].y / m_activeHeight * 2.0f;
            if (m_shaderOverride.colorOffset != 0xFFFFFFFFu)
            {
                std::memcpy(vertex + m_shaderOverride.colorOffset / sizeof(float), colorValues,
                            sizeof(colorValues));
            }
            if (m_shaderOverride.texCoordOffset != 0xFFFFFFFFu)
            {
                vertex[m_shaderOverride.texCoordOffset / sizeof(float)] =
                    points[index].x / m_activeWidth;
                vertex[m_shaderOverride.texCoordOffset / sizeof(float) + 1] =
                    points[index].y / m_activeHeight;
            }
        }

        m_resources->UpdateDynamicBuffer(m_context, m_pipeline->VertexBuffer(), vertices.data(),
                                         static_cast<UINT>(vertices.size() * sizeof(float)));
        BindOverride(nullptr);
        m_context->Draw(3, 0);
        return;
    }

    float vertices[18]{};

    for (int index = 0; index < 3; ++index)
    {
        vertices[index * 6] = points[index].x / m_activeWidth * 2.0f - 1.0f;
        vertices[index * 6 + 1] = 1.0f - points[index].y / m_activeHeight * 2.0f;
        std::memcpy(vertices + index * 6 + 2, colorValues, sizeof(colorValues));
    }

    m_resources->UpdateDynamicBuffer(m_context, m_pipeline->VertexBuffer(), vertices,
                                     sizeof(vertices));

    m_pipeline->Bind(m_context);
    m_context->Draw(3, 0);

}

void D3D11CommandContext::DrawTextureQuad(const ITexture& texture,
                                          Rectangle source,
                                          Rectangle destination,
                                          Vec2 origin,
                                          float rotation,
                                          Color tint,
                                          int,
                                          int)
{
    if (!m_context || !m_pipeline || !m_resources || !texture.IsValid() ||
        texture.width <= 0 || texture.height <= 0 || m_activeWidth <= 0 || m_activeHeight <= 0) {
        return;
    }

    ID3D11ShaderResourceView* shaderResource = m_resources->ShaderResource(texture.id);
    if (!shaderResource) {
        return;
    }

    const float radians = rotation * 3.14159265359f / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    if (m_resources->IsRenderTexture(texture.id) && source.height < 0.0f) {
        source.y += source.height;
        source.height = -source.height;
    }

    const float u0 = source.x / texture.width;
    const float v0 = source.y / texture.height;
    const float u1 = (source.x + source.width) / texture.width;
    const float v1 = (source.y + source.height) / texture.height;
    const Vec2 local[] = {
        {-origin.x, -origin.y},
        {destination.width - origin.x, -origin.y},
        {destination.width - origin.x, destination.height - origin.y},
        {-origin.x, destination.height - origin.y}
    };
    const Vec2 uv[] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    const int indices[] = {0, 1, 2, 0, 2, 3};
    const float color[] = {
        tint.r / 255.0f,
        tint.g / 255.0f,
        tint.b / 255.0f,
        tint.a / 255.0f
    };

    if (m_shaderOverride.Active())
    {
        const UINT floatsPerVertex = m_shaderOverride.strideBytes / sizeof(float);
        std::vector<float> vertices(static_cast<size_t>(floatsPerVertex) * 6, 0.0f);

        for (int index = 0; index < 6; ++index)
        {
            const int vertexIndex = indices[index];
            const float rotatedX = local[vertexIndex].x * cosine - local[vertexIndex].y * sine;
            const float rotatedY = local[vertexIndex].x * sine + local[vertexIndex].y * cosine;
            Vec2 position{destination.x + rotatedX, destination.y + rotatedY};
            if (m_camera2DActive) {
                position = GetWorldToScreen2D(position, m_camera2D);
            }

            float *vertex = vertices.data() + static_cast<size_t>(index) * floatsPerVertex;
            vertex[m_shaderOverride.positionOffset / sizeof(float)] =
                position.x / m_activeWidth * 2.0f - 1.0f;
            vertex[m_shaderOverride.positionOffset / sizeof(float) + 1] =
                1.0f - position.y / m_activeHeight * 2.0f;
            if (m_shaderOverride.texCoordOffset != 0xFFFFFFFFu)
            {
                vertex[m_shaderOverride.texCoordOffset / sizeof(float)] = uv[vertexIndex].x;
                vertex[m_shaderOverride.texCoordOffset / sizeof(float) + 1] = uv[vertexIndex].y;
            }
            if (m_shaderOverride.colorOffset != 0xFFFFFFFFu)
            {
                std::memcpy(vertex + m_shaderOverride.colorOffset / sizeof(float), color,
                            sizeof(color));
            }
        }

        m_resources->UpdateDynamicBuffer(
            m_context,
            m_pipeline->VertexBuffer(),
            vertices.data(),
            static_cast<UINT>(vertices.size() * sizeof(float)));
        BindOverride(shaderResource);
        m_context->Draw(6, 0);
        return;
    }

    float vertices[6 * 8]{};

    for (int index = 0; index < 6; ++index) {
        const int vertexIndex = indices[index];
        const float rotatedX = local[vertexIndex].x * cosine - local[vertexIndex].y * sine;
        const float rotatedY = local[vertexIndex].x * sine + local[vertexIndex].y * cosine;
        Vec2 position{destination.x + rotatedX, destination.y + rotatedY};
        if (m_camera2DActive) {
            position = GetWorldToScreen2D(position, m_camera2D);
        }

        float* vertex = vertices + index * 8;
        vertex[0] = position.x / m_activeWidth * 2.0f - 1.0f;
        vertex[1] = 1.0f - position.y / m_activeHeight * 2.0f;
        vertex[2] = uv[vertexIndex].x;
        vertex[3] = uv[vertexIndex].y;
        std::memcpy(vertex + 4, color, sizeof(color));
    }

    m_resources->UpdateDynamicBuffer(
        m_context,
        m_pipeline->VertexBuffer(),
        vertices,
        sizeof(vertices));
    m_pipeline->BindTexture(m_context, shaderResource);
    m_context->Draw(6, 0);
}

void D3D11CommandContext::BindOverride(ID3D11ShaderResourceView *drawnResource)
{
    if (!m_context || !m_pipeline)
    {
        return;
    }

    const ShaderOverride &shaderOverride = m_shaderOverride;
    const UINT stride = shaderOverride.strideBytes;
    const UINT offset = 0;
    ID3D11Buffer *vertexBuffer = m_pipeline->VertexBuffer();

    m_context->IASetInputLayout(shaderOverride.inputLayout);
    m_context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->RSSetState(m_pipeline->Rasterizer());
    m_context->VSSetShader(shaderOverride.vertexShader, nullptr, 0);
    m_context->PSSetShader(shaderOverride.pixelShader, nullptr, 0);

    if (shaderOverride.constantBuffer)
    {
        ID3D11Buffer *constantBuffer = shaderOverride.constantBuffer;
        m_context->VSSetConstantBuffers(0, 1, &constantBuffer);
        m_context->PSSetConstantBuffers(0, 1, &constantBuffer);
    }

    ID3D11ShaderResourceView *resources[8] = {};
    for (size_t index = 0; index < 8; ++index)
    {
        resources[index] = shaderOverride.shaderResources[index];
    }
    if (drawnResource)
    {
        resources[0] = drawnResource;
    }
    m_context->PSSetShaderResources(0, 8, resources);

    ID3D11SamplerState *sampler = m_pipeline->Sampler();
    m_context->PSSetSamplers(0, 1, &sampler);

    const float blendFactor[] = {0.0f, 0.0f, 0.0f, 0.0f};
    m_context->OMSetDepthStencilState(m_pipeline->DepthStencilDisabledState(), 0);
    m_context->OMSetBlendState(m_pipeline->Blend(), blendFactor, 0xFFFFFFFF);
}

void D3D11CommandContext::BeginTextureMode(const IRenderTexture& target)
{
    if (!m_context || !m_resources || target.id == 0) {
        return;
    }

    ID3D11RenderTargetView* renderTarget = m_resources->RenderTarget(target.id);
    if (!renderTarget) {
        return;
    }

    m_context->OMSetRenderTargets(1, &renderTarget, nullptr);
    m_activeRenderTarget = renderTarget;
    m_activeWidth = target.texture.width;
    m_activeHeight = target.texture.height;
    RefreshViewport(target.texture.width, target.texture.height);
}

void D3D11CommandContext::EndTextureMode(int width, int height)
{
    BeginDrawing();
    const int restoreWidth = width > 0 ? width : m_defaultWidth;
    const int restoreHeight = height > 0 ? height : m_defaultHeight;
    m_activeWidth = restoreWidth;
    m_activeHeight = restoreHeight;
    RefreshViewport(restoreWidth, restoreHeight);
}

void D3D11CommandContext::BeginMode2D(const Camera2D& camera)
{
    m_camera2D = camera;
    m_camera2DActive = true;
}

void D3D11CommandContext::EndMode2D()
{
    m_camera2DActive = false;
}

void D3D11CommandContext::Shutdown()
{
    TraceLog(LogLevel::Trace, "D3D11", "Immediate command context shut down.");

    m_context = nullptr;
    m_swapChain = nullptr;
    m_pipeline = nullptr;
    m_resources = nullptr;
    m_activeRenderTarget = nullptr;
    m_defaultWidth = 0;
    m_defaultHeight = 0;
    m_activeWidth = 0;
    m_activeHeight = 0;
    m_camera2D = {};
    m_camera2DActive = false;
    m_shaderOverride = {};
}

} // namespace qc
#endif