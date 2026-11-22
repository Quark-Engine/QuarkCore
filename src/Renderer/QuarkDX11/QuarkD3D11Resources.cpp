#include "QuarkD3D11Resources.hpp"

#include <cstring>
#include <limits>
#include <utility>

#if defined(_WIN32)
namespace qci
{
    void D3D11Resources::Initialize(ID3D11Device* pDevice)
    {
        TraceLog(LogLevel::Trace, "D3D11", "Creating dynamic triangle vertex buffer...");
        CreateDynamicVertexBuffer(pDevice, sizeof(float) * 8 * 6);
        TraceLog(LogLevel::Info, "D3D11", "Dynamic 2D vertex buffer created (192 bytes).");

        TraceLog(LogLevel::Trace, "D3D11", "Creating dynamic 3D vertex buffer...");
        {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = 8 * 1024 * 1024;
            description.Usage = D3D11_USAGE_DYNAMIC;
            description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            d3d11::ThrowIfFailed(pDevice->CreateBuffer(&description, nullptr, &m_VertexBuffer3D),
                                 "ID3D11Device::CreateBuffer 3D");
        }
        TraceLog(LogLevel::Info, "D3D11", "Dynamic 3D vertex buffer created (8 MB).");
    }

    ID3D11Buffer* D3D11Resources::CreateDynamicVertexBuffer(ID3D11Device* pDevice, UINT byteWidth)
    {
        TraceLog(LogLevel::Trace, "D3D11",
                 TextFormat("Creating dynamic vertex buffer (%u bytes).", byteWidth));

        D3D11_BUFFER_DESC description{};
        description.ByteWidth = byteWidth;
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        d3d11::ThrowIfFailed(pDevice->CreateBuffer(&description, nullptr, &m_TriangleVertexBuffer),
                             "ID3D11Device::CreateBuffer");

        return m_TriangleVertexBuffer.Get();
    }

    void D3D11Resources::UpdateDynamicBuffer(ID3D11DeviceContext* pContext, ID3D11Buffer* pBuffer,
                                             const void* pData, size_t size) const
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        d3d11::ThrowIfFailed(pContext->Map(pBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped),
                             "ID3D11DeviceContext::Map");

        std::memcpy(mapped.pData, pData, size);
        pContext->Unmap(pBuffer, 0);
    }

    void D3D11Resources::Shutdown()
    {
        TraceLog(LogLevel::Trace, "D3D11", "Releasing D3D11 resources...");
        m_TriangleVertexBuffer.Reset();
        m_VertexBuffer3D.Reset();
        m_Textures.clear();
        m_RenderTextures.clear();
        TraceLog(LogLevel::Trace, "D3D11", "D3D11 resources released.");
    }

    ITexture D3D11Resources::CreateTexture(ID3D11Device* pDevice, const uint8_t* pPixels,
                                           int width, int height)
    {
        ITexture result{};
        if (!pDevice || !pPixels || width <= 0 || height <= 0)
        {
            return result;
        }

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
        initialData.pSysMem = pPixels;
        initialData.SysMemPitch = static_cast<UINT>(width * 4);

        TextureResource resource;
        d3d11::ThrowIfFailed(pDevice->CreateTexture2D(&description, &initialData, &resource.m_Texture),
                             "ID3D11Device::CreateTexture2D");
        d3d11::ThrowIfFailed(pDevice->CreateShaderResourceView(resource.m_Texture.Get(), nullptr,
                                                               &resource.m_ShaderResource),
                             "ID3D11Device::CreateShaderResourceView");

        result = {m_NextTextureId++, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, true};
        m_Textures.emplace(result.m_Id, std::move(resource));
        return result;
    }

    ITexture D3D11Resources::CreateCubemap(ID3D11Device* pDevice, const uint8_t* pRgbaFaces,
                                           int faceSize)
    {
        ITexture result{};
        if (!pDevice || !pRgbaFaces || faceSize <= 0 ||
            static_cast<uint64_t>(faceSize) * 4 > std::numeric_limits<UINT>::max())
        {
            return result;
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(faceSize);
        description.Height = static_cast<UINT>(faceSize);
        description.MipLevels = 1;
        description.ArraySize = 6;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

        const size_t faceBytes = static_cast<size_t>(faceSize) * faceSize * 4;
        D3D11_SUBRESOURCE_DATA aInitialData[6]{};
        for (UINT face = 0; face < 6; ++face)
        {
            aInitialData[face].pSysMem = pRgbaFaces + static_cast<size_t>(face) * faceBytes;
            aInitialData[face].SysMemPitch = static_cast<UINT>(faceSize) * 4;
        }

        TextureResource resource;
        d3d11::ThrowIfFailed(pDevice->CreateTexture2D(&description, aInitialData, &resource.m_Texture),
                             "ID3D11Device::CreateTexture2D cubemap");

        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
        viewDescription.Format = description.Format;
        viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        viewDescription.TextureCube.MipLevels = 1;
        d3d11::ThrowIfFailed(pDevice->CreateShaderResourceView(resource.m_Texture.Get(), &viewDescription,
                                                               &resource.m_ShaderResource),
                             "ID3D11Device::CreateShaderResourceView cubemap");

        result = {m_NextTextureId++, faceSize, faceSize, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, true};
        m_Textures.emplace(result.m_Id, std::move(resource));
        return result;
    }

    IRenderTexture D3D11Resources::CreateRenderTexture(ID3D11Device* pDevice, int width, int height)
    {
        IRenderTexture result{};
        if (!pDevice || width <= 0 || height <= 0)
        {
            return result;
        }

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
        d3d11::ThrowIfFailed(pDevice->CreateTexture2D(&description, nullptr, &resource.m_Texture),
                             "ID3D11Device::CreateTexture2D render target");
        d3d11::ThrowIfFailed(pDevice->CreateRenderTargetView(resource.m_Texture.Get(), nullptr,
                                                             &resource.m_RenderTarget),
                             "ID3D11Device::CreateRenderTargetView");
        d3d11::ThrowIfFailed(pDevice->CreateShaderResourceView(resource.m_Texture.Get(), nullptr,
                                                               &resource.m_ShaderResource),
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
        d3d11::ThrowIfFailed(pDevice->CreateTexture2D(&depthDescription, nullptr, &resource.m_DepthTexture),
                             "ID3D11Device::CreateTexture2D render target depth");
        d3d11::ThrowIfFailed(pDevice->CreateDepthStencilView(resource.m_DepthTexture.Get(), nullptr,
                                                             &resource.m_DepthStencilView),
                             "ID3D11Device::CreateDepthStencilView render target depth");

        result.m_Id = m_NextTextureId++;
        result.m_Texture = {result.m_Id, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, true};
        m_RenderTextures.emplace(result.m_Id, std::move(resource));
        return result;
    }

    ID3D11ShaderResourceView* D3D11Resources::ShaderResource(uint32_t id) const
    {
        auto texture = m_Textures.find(id);
        if (texture != m_Textures.end())
        {
            return texture->second.m_ShaderResource.Get();
        }
        auto renderTexture = m_RenderTextures.find(id);
        return renderTexture == m_RenderTextures.end()
                   ? nullptr
                   : renderTexture->second.m_ShaderResource.Get();
    }

    ID3D11RenderTargetView* D3D11Resources::RenderTarget(uint32_t id) const
    {
        auto renderTexture = m_RenderTextures.find(id);
        return renderTexture == m_RenderTextures.end() ? nullptr : renderTexture->second.m_RenderTarget.Get();
    }

    ID3D11DepthStencilView* D3D11Resources::DepthStencil(uint32_t id) const
    {
        auto renderTexture = m_RenderTextures.find(id);
        return renderTexture == m_RenderTextures.end()
                   ? nullptr
                   : renderTexture->second.m_DepthStencilView.Get();
    }

    bool D3D11Resources::IsRenderTexture(uint32_t id) const
    {
        return m_RenderTextures.find(id) != m_RenderTextures.end();
    }

    void D3D11Resources::DestroyTexture(uint32_t id)
    {
        m_Textures.erase(id);
    }

    void D3D11Resources::DestroyRenderTexture(uint32_t id)
    {
        m_RenderTextures.erase(id);
    }

    bool D3D11Resources::ReadPixels(ID3D11DeviceContext* pContext, uint32_t id, void* pOutPixels,
                                    int width, int height) const
    {
        if (!pContext || !pOutPixels || width <= 0 || height <= 0)
        {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
        const auto texture = m_Textures.find(id);
        if (texture != m_Textures.end())
        {
            source = texture->second.m_Texture;
        }
        else
        {
            const auto renderTexture = m_RenderTextures.find(id);
            if (renderTexture != m_RenderTextures.end())
            {
                source = renderTexture->second.m_Texture;
            }
        }
        if (!source)
        {
            return false;
        }

        D3D11_TEXTURE2D_DESC description{};
        source->GetDesc(&description);
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        description.MiscFlags = 0;

        ID3D11Device* pDevice = nullptr;
        pContext->GetDevice(&pDevice);
        if (!pDevice)
        {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        const HRESULT hr = pDevice->CreateTexture2D(&description, nullptr, &staging);
        pDevice->Release();
        if (FAILED(hr))
        {
            return false;
        }

        pContext->CopyResource(staging.Get(), source.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(pContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            return false;
        }

        const size_t rowBytes = static_cast<size_t>(width) * 4;
        const uint8_t* pSrc = static_cast<const uint8_t*>(mapped.pData);
        uint8_t* pDst = static_cast<uint8_t*>(pOutPixels);
        for (int y = 0; y < height; ++y)
        {
            std::memcpy(pDst + static_cast<size_t>(y) * rowBytes, pSrc + static_cast<size_t>(y) * mapped.RowPitch, rowBytes);
        }

        pContext->Unmap(staging.Get(), 0);
        return true;
    }

    bool D3D11Resources::UpdateTexture(ID3D11DeviceContext* pContext, uint32_t id, const uint8_t* pPixels,
                                       int width, int height)
    {
        if (!pContext || !pPixels || width <= 0 || height <= 0)
        {
            return false;
        }

        const auto it = m_Textures.find(id);
        if (it == m_Textures.end())
        {
            TraceLog(LogLevel::Warn, "TEXTURE", TextFormat("[D3D11] Texture not found for update (ID: %u)", id));
            return false;
        }

        const TextureResource& resource = it->second;
        if (!resource.m_Texture)
        {
            return false;
        }

        D3D11_TEXTURE2D_DESC desc{};
        resource.m_Texture->GetDesc(&desc);

        if (static_cast<int>(desc.Width) != width || static_cast<int>(desc.Height) != height)
        {
            TraceLog(LogLevel::Warn, "TEXTURE", TextFormat("[D3D11] Texture size mismatch: expected %ux%u, got %ux%u", desc.Width, desc.Height, width, height));
            return false;
        }

        const size_t rowBytes = static_cast<size_t>(width) * 4;
        const D3D11_BOX box{0, 0, 0, static_cast<UINT>(width), static_cast<UINT>(height), 1};

        pContext->UpdateSubresource(resource.m_Texture.Get(), 0, &box, pPixels, static_cast<UINT>(rowBytes), 0);
        TraceLog(LogLevel::Trace, "TEXTURE", TextFormat("[D3D11] Texture updated (ID: %u, %ux%u)", id, width, height));
        return true;
    }

    bool D3D11Resources::UpdateTextureRegion(ID3D11DeviceContext* pContext, uint32_t id, const uint8_t* pPixels,
                                             int offsetX, int offsetY, int width, int height)
    {
        if (!pContext || !pPixels || width <= 0 || height <= 0)
        {
            return false;
        }

        const auto it = m_Textures.find(id);
        if (it == m_Textures.end())
        {
            TraceLog(LogLevel::Warn, "TEXTURE", TextFormat("[D3D11] Texture not found for region update (ID: %u)", id));
            return false;
        }

        const TextureResource& resource = it->second;
        if (!resource.m_Texture)
        {
            return false;
        }

        D3D11_TEXTURE2D_DESC desc{};
        resource.m_Texture->GetDesc(&desc);

        if (offsetX + width > static_cast<int>(desc.Width) || offsetY + height > static_cast<int>(desc.Height))
        {
            TraceLog(LogLevel::Warn, "TEXTURE", TextFormat("[D3D11] Region out of bounds: texture %ux%u, region (%d,%d) %ux%u", desc.Width, desc.Height, offsetX, offsetY, width, height));
            return false;
        }

        const size_t rowBytes = static_cast<size_t>(width) * 4;
        const D3D11_BOX box{
            static_cast<UINT>(offsetX), static_cast<UINT>(offsetY), 0,
            static_cast<UINT>(offsetX + width), static_cast<UINT>(offsetY + height), 1};

        pContext->UpdateSubresource(resource.m_Texture.Get(), 0, &box, pPixels, static_cast<UINT>(rowBytes), 0);
        TraceLog(LogLevel::Trace, "TEXTURE", TextFormat("[D3D11] Texture region updated (ID: %u, offset (%d,%d) size %ux%u)", id, offsetX, offsetY, width, height));
        return true;
    }
} // qci
#endif
