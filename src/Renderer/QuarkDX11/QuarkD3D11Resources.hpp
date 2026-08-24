#pragma once

#if defined(_WIN32)
#include "QuarkD3D11Common.hpp"
#include "../QuarkTexture.hpp"
#include <wrl/client.h>
#include <unordered_map>

namespace qc {

class D3D11Resources {
public:
    void Initialize(ID3D11Device *device);
    void Shutdown();
    ID3D11Buffer *CreateDynamicVertexBuffer(ID3D11Device *device, UINT byteWidth);
    void UpdateDynamicBuffer(ID3D11DeviceContext *context, ID3D11Buffer *buffer, const void *data,
                             size_t size) const;
    ID3D11Buffer *VertexBuffer() const { return m_triangleVertexBuffer.Get(); }
    ITexture CreateTexture(ID3D11Device *device, const uint8_t *pixels, int width, int height);
    IRenderTexture CreateRenderTexture(ID3D11Device *device, int width, int height);
    ID3D11ShaderResourceView *ShaderResource(uint32_t id) const;
    ID3D11RenderTargetView *RenderTarget(uint32_t id) const;
    bool IsRenderTexture(uint32_t id) const;
    void DestroyTexture(uint32_t id);
    void DestroyRenderTexture(uint32_t id);

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_triangleVertexBuffer;

    struct TextureResource {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResource;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget;
    };

    std::unordered_map<uint32_t, TextureResource> m_textures;
    std::unordered_map<uint32_t, TextureResource> m_renderTextures;
    uint32_t m_nextTextureId = 1;
};

} // namespace qc
#endif