#ifndef __QUARK_D3D11_PIPELINE__
#define __QUARK_D3D11_PIPELINE__

#if defined(_WIN32)
#include "QuarkD3D11Common.hpp"
#include "QuarkD3D11Resources.hpp"
#include "QuarkD3D11ShaderCompiler.hpp"
#include <wrl/client.h>

namespace qc {

class D3D11Pipeline {
public:
    void Initialize(ID3D11Device *device, D3D11ShaderCompiler &compiler, D3D11Resources &resources);
    void Shutdown();
    void Bind(ID3D11DeviceContext *context) const;
    void Bind3D(ID3D11DeviceContext *context) const;
    void BindTexture(ID3D11DeviceContext *context, ID3D11ShaderResourceView *shaderResource) const;
    void BindTexture3D(ID3D11DeviceContext *context, ID3D11ShaderResourceView *shaderResource) const;
    void BindDepthDisabled(ID3D11DeviceContext *context) const;
    void SetBackfaceCulling(bool enabled) { m_backfaceCullingEnabled = enabled; }
    bool BackfaceCulling() const { return m_backfaceCullingEnabled; }
    ID3D11Buffer *VertexBuffer() const { return m_vertexBuffer; }
    ID3D11Buffer *VertexBuffer3D() const { return m_vertexBuffer3D; }
    ID3D11RasterizerState *Rasterizer() const { return m_rasterizerState.Get(); }
    ID3D11BlendState *Blend() const { return m_blendState.Get(); }
    ID3D11SamplerState *Sampler() const { return m_textureSampler.Get(); }
    ID3D11DepthStencilState *DepthStencilDisabledState() const
    {
        return m_depthStencilDisabledState.Get();
    }

private:
    ID3D11Buffer *m_vertexBuffer = nullptr;
    ID3D11Buffer *m_vertexBuffer3D = nullptr;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_texturedVertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_texturedPixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_texturedInputLayout;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_texturedVertexShader3D;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_texturedPixelShader3D;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_texturedInputLayout3D;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_textureSampler;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerState;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader3D;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader3D;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout3D;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStencilState;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStencilDisabledState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerStateCull;
    bool m_backfaceCullingEnabled = false;
};

using D3D11PipelineState = D3D11Pipeline;

} // namespace qc
#endif

#endif // __QUARK_D3D11_PIPELINE__
