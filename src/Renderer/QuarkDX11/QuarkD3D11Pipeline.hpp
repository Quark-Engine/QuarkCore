#ifndef __QUARK_D3D11_PIPELINE__
#define __QUARK_D3D11_PIPELINE__

#if defined(_WIN32)
#include "QuarkD3D11Common.hpp"
#include "QuarkD3D11Resources.hpp"
#include "QuarkD3D11ShaderCompiler.hpp"
#include <wrl/client.h>

namespace qci
{
    struct D3D11LightConstantData
    {
        float m_aAmbientColor[4];
        float m_aViewPosition[4];
        float m_aLightPositions[4][4];
        float m_aLightColors[4][4];
        float m_aLightParams[4][4]; // x = attenuation, y = enabled, z = type, w = padding

        void SetAmbient(const float aValues[3])
        {
            m_aAmbientColor[0] = aValues[0];
            m_aAmbientColor[1] = aValues[1];
            m_aAmbientColor[2] = aValues[2];
            m_aAmbientColor[3] = 1.0f;
        }
    };

    class D3D11Pipeline
    {
    public:
        void Initialize(ID3D11Device* pDevice, D3D11ShaderCompiler& compiler, D3D11Resources& resources);
        void Shutdown();
        void UpdateLights(ID3D11DeviceContext* pContext, const D3D11LightConstantData& lights);
        void Bind3D(ID3D11DeviceContext* pContext) const;
        void BindTexture3D(ID3D11DeviceContext* pContext, ID3D11ShaderResourceView* pShaderResource,
                           ID3D11ShaderResourceView* pNormalResource) const;
        void BindBatch(ID3D11DeviceContext* pContext, ID3D11Buffer* pVertexBuffer,
                       ID3D11Buffer* pIndexBuffer) const;
        void SetBackfaceCulling(bool enabled)
        {
            m_BackfaceCullingEnabled = enabled;
        }
        bool BackfaceCulling() const
        {
            return m_BackfaceCullingEnabled;
        }
        void SetTextureFilterMode(TextureFilter mode);
        void SetTextureWrapMode(int wrap);
        TextureFilter GetTextureFilterMode() const
        {
            return m_TextureFilterMode;
        }
        int GetTextureWrapMode() const
        {
            return m_TextureWrapMode;
        }
        ID3D11Buffer* VertexBuffer() const
        {
            return m_pVertexBuffer;
        }
        ID3D11Buffer* VertexBuffer3D() const
        {
            return m_pVertexBuffer3D;
        }
        ID3D11VertexShader* VertexShader3D() const
        {
            return m_VertexShader3D.Get();
        }
        ID3D11PixelShader* PixelShader3D() const
        {
            return m_PixelShader3D.Get();
        }
        ID3D11InputLayout* InputLayout3D() const
        {
            return m_InputLayout3D.Get();
        }
        ID3D11VertexShader* TexturedVertexShader3D() const
        {
            return m_TexturedVertexShader3D.Get();
        }
        ID3D11PixelShader* TexturedPixelShader3D() const
        {
            return m_TexturedPixelShader3D.Get();
        }
        ID3D11InputLayout* TexturedInputLayout3D() const
        {
            return m_TexturedInputLayout3D.Get();
        }
        ID3D11RasterizerState* Rasterizer() const
        {
            return m_RasterizerState.Get();
        }
        ID3D11RasterizerState* RasterizerCull() const
        {
            return m_RasterizerStateCull.Get();
        }
        ID3D11DepthStencilState* DepthStencilState() const
        {
            return m_DepthStencilState.Get();
        }
        ID3D11Buffer* const* LightConstantBuffer() const
        {
            return m_LightConstantBuffer.GetAddressOf();
        }
        ID3D11BlendState* Blend() const
        {
            return m_BlendState.Get();
        }
        ID3D11SamplerState* Sampler() const
        {
            return m_TextureSampler.Get();
        }
        ID3D11DepthStencilState* DepthStencilDisabledState() const
        {
            return m_DepthStencilDisabledState.Get();
        }

    private:
        void CreateSamplerState(TextureFilter mode);
        ID3D11Device* m_pDevice = nullptr;
        ID3D11Buffer* m_pVertexBuffer = nullptr;
        ID3D11Buffer* m_pVertexBuffer3D = nullptr;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_TexturedVertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_TexturedPixelShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_TexturedInputLayout;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_TexturedVertexShader3D;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_TexturedPixelShader3D;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_TexturedInputLayout3D;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> m_TextureSampler;
        Microsoft::WRL::ComPtr<ID3D11BlendState> m_BlendState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_RasterizerState;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_VertexShader3D;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_PixelShader3D;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_InputLayout3D;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_DepthStencilState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_DepthStencilDisabledState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_RasterizerStateCull;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_LightConstantBuffer;
        bool m_BackfaceCullingEnabled = false;
        TextureFilter m_TextureFilterMode = TEXTURE_FILTER_BILINEAR;
        int m_TextureWrapMode = TEXTURE_WRAP_REPEAT;
    };

    using D3D11PipelineState = D3D11Pipeline;
} // qci
#endif
#endif // __QUARK_D3D11_PIPELINE__
