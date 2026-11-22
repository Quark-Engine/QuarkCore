#ifndef __QUARK_D3D11_RESOURCES__
#define __QUARK_D3D11_RESOURCES__

#if defined(_WIN32)
#include "QuarkD3D11Common.hpp"
#include "../QuarkTexture.hpp"
#include <wrl/client.h>
#include <unordered_map>

namespace qci
{
    class D3D11Resources
    {
    public:
        void Initialize(ID3D11Device* pDevice);
        void Shutdown();
        ID3D11Buffer* CreateDynamicVertexBuffer(ID3D11Device* pDevice, UINT byteWidth);
        void UpdateDynamicBuffer(ID3D11DeviceContext* pContext, ID3D11Buffer* pBuffer, const void* pData,
                                 size_t size) const;
        ID3D11Buffer* VertexBuffer() const
        {
            return m_TriangleVertexBuffer.Get();
        }
        ID3D11Buffer* VertexBuffer3D() const
        {
            return m_VertexBuffer3D.Get();
        }
        ITexture CreateTexture(ID3D11Device* pDevice, const uint8_t* pPixels, int width, int height);
        ITexture CreateCubemap(ID3D11Device* pDevice, const uint8_t* pRgbaFaces, int faceSize);
        IRenderTexture CreateRenderTexture(ID3D11Device* pDevice, int width, int height);
        bool UpdateTexture(ID3D11DeviceContext* pContext, uint32_t id, const uint8_t* pPixels,
                           int width, int height);
        bool UpdateTextureRegion(ID3D11DeviceContext* pContext, uint32_t id, const uint8_t* pPixels,
                                 int offsetX, int offsetY, int width, int height);
        ID3D11ShaderResourceView* ShaderResource(uint32_t id) const;
        ID3D11RenderTargetView* RenderTarget(uint32_t id) const;
        ID3D11DepthStencilView* DepthStencil(uint32_t id) const;
        bool IsRenderTexture(uint32_t id) const;
        void DestroyTexture(uint32_t id);
        void DestroyRenderTexture(uint32_t id);
        bool ReadPixels(ID3D11DeviceContext* pContext, uint32_t id, void* pOutPixels,
                        int width, int height) const;

    private:
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_TriangleVertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_VertexBuffer3D;

        struct TextureResource
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Texture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_ShaderResource;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RenderTarget;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DepthTexture;
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_DepthStencilView;
        };

        std::unordered_map<uint32_t, TextureResource> m_Textures;
        std::unordered_map<uint32_t, TextureResource> m_RenderTextures;
        uint32_t m_NextTextureId = 1;
    };
} // qci
#endif
#endif // __QUARK_D3D11_RESOURCES__
