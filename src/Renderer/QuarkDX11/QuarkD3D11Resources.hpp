#ifndef __QUARK_D3D11_RESOURCES__
#define __QUARK_D3D11_RESOURCES__

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
    ID3D11Buffer *VertexBuffer3D() const { return m_vertexBuffer3D.Get(); }
    ITexture CreateTexture(ID3D11Device *device, const uint8_t *pixels, int width, int height);
    IRenderTexture CreateRenderTexture(ID3D11Device *device, int width, int height);
    bool UpdateTexture(ID3D11DeviceContext *context, uint32_t id, const uint8_t *pixels,
                       int width, int height);
    bool UpdateTextureRegion(ID3D11DeviceContext *context, uint32_t id, const uint8_t *pixels,
                             int offsetX, int offsetY, int width, int height);
    ID3D11ShaderResourceView *ShaderResource(uint32_t id) const;
    ID3D11RenderTargetView *RenderTarget(uint32_t id) const;
    ID3D11DepthStencilView *DepthStencil(uint32_t id) const;
    bool IsRenderTexture(uint32_t id) const;
    void DestroyTexture(uint32_t id);
    void DestroyRenderTexture(uint32_t id);
    bool ReadPixels(ID3D11DeviceContext *context, uint32_t id, void *outPixels,
                    int width, int height) const;

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_triangleVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexBuffer3D;

    struct TextureResource {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResource;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthStencilView;
    };

    std::unordered_map<uint32_t, TextureResource> m_textures;
    std::unordered_map<uint32_t, TextureResource> m_renderTextures;
    uint32_t m_nextTextureId = 1;
};

} // namespace qc
#endif

#endif // __QUARK_D3D11_RESOURCES__
