#include "QuarkD3D11Resources.hpp"

#include <cstring>
#include <utility>

#if defined(_WIN32)
namespace qc {

void D3D11Resources::Initialize(ID3D11Device *device)
{
    TraceLog(LogLevel::Trace, "D3D11", "Creating dynamic triangle vertex buffer...");
    CreateDynamicVertexBuffer(device, sizeof(float) * 8 * 6);
    TraceLog(LogLevel::Info, "D3D11", "Dynamic 2D vertex buffer created (192 bytes).");

    TraceLog(LogLevel::Trace, "D3D11", "Creating dynamic 3D vertex buffer...");
    {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = 1024 * 1024;
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        d3d11::ThrowIfFailed(device->CreateBuffer(&description, nullptr, &m_vertexBuffer3D),
                             "ID3D11Device::CreateBuffer 3D");
    }
    TraceLog(LogLevel::Info, "D3D11", "Dynamic 3D vertex buffer created (1 MB).");
}

ID3D11Buffer *D3D11Resources::CreateDynamicVertexBuffer(ID3D11Device *device, UINT byteWidth)
{
    TraceLog(LogLevel::Trace, "D3D11",
             TextFormat("Creating dynamic vertex buffer (%u bytes).", byteWidth));

    D3D11_BUFFER_DESC description{};
    description.ByteWidth = byteWidth;
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    d3d11::ThrowIfFailed(device->CreateBuffer(&description, nullptr, &m_triangleVertexBuffer),
                         "ID3D11Device::CreateBuffer");

    return m_triangleVertexBuffer.Get();
}

void D3D11Resources::UpdateDynamicBuffer(ID3D11DeviceContext *context, ID3D11Buffer *buffer,
                                         const void *data, size_t size) const
{
    D3D11_MAPPED_SUBRESOURCE mapped{};

    d3d11::ThrowIfFailed(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped),
                         "ID3D11DeviceContext::Map");

    std::memcpy(mapped.pData, data, size);
    context->Unmap(buffer, 0);

}

void D3D11Resources::Shutdown()
{
    TraceLog(LogLevel::Trace, "D3D11", "Releasing D3D11 resources...");
    m_triangleVertexBuffer.Reset();
    m_vertexBuffer3D.Reset();
    m_textures.clear();
    m_renderTextures.clear();
    TraceLog(LogLevel::Trace, "D3D11", "D3D11 resources released.");
}

ITexture D3D11Resources::CreateTexture(ID3D11Device *device, const uint8_t *pixels,
                                       int width, int height)
{
    ITexture result{};
    if (!device || !pixels || width <= 0 || height <= 0) return result;

    D3D11_TEXTURE2D_DESC description{};
    description.Width = static_cast<UINT>(width);
    description.Height = static_cast<UINT>(height);
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initialData{};
    initialData.pSysMem = pixels;
    initialData.SysMemPitch = static_cast<UINT>(width * 4);

    TextureResource resource;
    d3d11::ThrowIfFailed(device->CreateTexture2D(&description, &initialData, &resource.texture),
                         "ID3D11Device::CreateTexture2D");
    d3d11::ThrowIfFailed(device->CreateShaderResourceView(resource.texture.Get(), nullptr,
                                                           &resource.shaderResource),
                         "ID3D11Device::CreateShaderResourceView");

    result = {m_nextTextureId++, width, height, true};
    m_textures.emplace(result.id, std::move(resource));
    return result;
}

IRenderTexture D3D11Resources::CreateRenderTexture(ID3D11Device *device, int width, int height)
{
    IRenderTexture result{};
    if (!device || width <= 0 || height <= 0) return result;

    D3D11_TEXTURE2D_DESC description{};
    description.Width = static_cast<UINT>(width);
    description.Height = static_cast<UINT>(height);
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    TextureResource resource;
    d3d11::ThrowIfFailed(device->CreateTexture2D(&description, nullptr, &resource.texture),
                         "ID3D11Device::CreateTexture2D render target");
    d3d11::ThrowIfFailed(device->CreateRenderTargetView(resource.texture.Get(), nullptr,
                                                         &resource.renderTarget),
                         "ID3D11Device::CreateRenderTargetView");
    d3d11::ThrowIfFailed(device->CreateShaderResourceView(resource.texture.Get(), nullptr,
                                                           &resource.shaderResource),
                         "ID3D11Device::CreateShaderResourceView render target");

    D3D11_TEXTURE2D_DESC depthDescription{};
    depthDescription.Width = static_cast<UINT>(width);
    depthDescription.Height = static_cast<UINT>(height);
    depthDescription.MipLevels = 1;
    depthDescription.ArraySize = 1;
    depthDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDescription.SampleDesc.Count = 1;
    depthDescription.Usage = D3D11_USAGE_DEFAULT;
    depthDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    d3d11::ThrowIfFailed(device->CreateTexture2D(&depthDescription, nullptr, &resource.depthTexture),
                         "ID3D11Device::CreateTexture2D render target depth");
    d3d11::ThrowIfFailed(device->CreateDepthStencilView(resource.depthTexture.Get(), nullptr,
                                                        &resource.depthStencilView),
                         "ID3D11Device::CreateDepthStencilView render target depth");

    result.id = m_nextTextureId++;
    result.texture = {result.id, width, height, true};
    m_renderTextures.emplace(result.id, std::move(resource));
    return result;
}

ID3D11ShaderResourceView *D3D11Resources::ShaderResource(uint32_t id) const
{
    auto texture = m_textures.find(id);
    if (texture != m_textures.end()) return texture->second.shaderResource.Get();
    auto renderTexture = m_renderTextures.find(id);
    return renderTexture == m_renderTextures.end()
               ? nullptr
               : renderTexture->second.shaderResource.Get();
}

ID3D11RenderTargetView *D3D11Resources::RenderTarget(uint32_t id) const
{
    auto renderTexture = m_renderTextures.find(id);
    return renderTexture == m_renderTextures.end() ? nullptr : renderTexture->second.renderTarget.Get();
}

ID3D11DepthStencilView *D3D11Resources::DepthStencil(uint32_t id) const
{
    auto renderTexture = m_renderTextures.find(id);
    return renderTexture == m_renderTextures.end()
               ? nullptr
               : renderTexture->second.depthStencilView.Get();
}

bool D3D11Resources::IsRenderTexture(uint32_t id) const
{
    return m_renderTextures.find(id) != m_renderTextures.end();
}

void D3D11Resources::DestroyTexture(uint32_t id) { m_textures.erase(id); }

void D3D11Resources::DestroyRenderTexture(uint32_t id) { m_renderTextures.erase(id); }

} // namespace qc
#endif