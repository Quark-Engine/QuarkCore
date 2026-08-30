#ifndef __QUARK_D3D11_PIPELINE__
#define __QUARK_D3D11_PIPELINE__

#if defined(_WIN32)
#include "QuarkD3D11Common.hpp"
#include "QuarkD3D11Resources.hpp"
#include "QuarkD3D11ShaderCompiler.hpp"
#include <wrl/client.h>

namespace qc {

struct D3D11LightConstantData {
    float ambientColor[4];
    float viewPosition[4];
    float lightPositions[4][4];
    float lightColors[4][4];
    float lightParams[4][4]; // x = attenuation, y = enabled, z = type, w = padding

    void SetAmbient(const float values[3]) {
        ambientColor[0] = values[0];
        ambientColor[1] = values[1];
        ambientColor[2] = values[2];
        ambientColor[3] = 1.0f;
    }
};

class D3D11Pipeline {
public:
    void Initialize(ID3D11Device *device, D3D11ShaderCompiler &compiler, D3D11Resources &resources);
    void Shutdown();
    void UpdateLights(ID3D11DeviceContext *context, const D3D11LightConstantData &lights);
    void Bind3D(ID3D11DeviceContext *context) const;
    void BindTexture3D(ID3D11DeviceContext *context, ID3D11ShaderResourceView *shaderResource) const;
    void BindBatch(ID3D11DeviceContext *context, ID3D11Buffer *vertexBuffer,
                   ID3D11Buffer *indexBuffer) const;
    void SetBackfaceCulling(bool enabled) { m_backfaceCullingEnabled = enabled; }
    bool BackfaceCulling() const { return m_backfaceCullingEnabled; }
    void SetTextureFilterMode(TextureFilterMode mode);
    TextureFilterMode GetTextureFilterMode() const { return m_textureFilterMode; }
    ID3D11Buffer *VertexBuffer() const { return m_vertexBuffer; }
    ID3D11Buffer *VertexBuffer3D() const { return m_vertexBuffer3D; }
    ID3D11RasterizerState *Rasterizer() const { return m_rasterizerState.Get(); }
    ID3D11RasterizerState *RasterizerCull() const { return m_rasterizerStateCull.Get(); }
    ID3D11DepthStencilState *DepthStencilState() const { return m_depthStencilState.Get(); }
    ID3D11Buffer *const *LightConstantBuffer() const { return m_lightConstantBuffer.GetAddressOf(); }
    ID3D11BlendState *Blend() const { return m_blendState.Get(); }
    ID3D11SamplerState *Sampler() const { return m_textureSampler.Get(); }
    ID3D11DepthStencilState *DepthStencilDisabledState() const
    {
        return m_depthStencilDisabledState.Get();
    }

private:
    void CreateSamplerState(TextureFilterMode mode);
    ID3D11Device *m_device = nullptr;
    ID3D11Buffer *m_vertexBuffer = nullptr;
    ID3D11Buffer *m_vertexBuffer3D = nullptr;
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
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_lightConstantBuffer;
    bool m_backfaceCullingEnabled = false;
    TextureFilterMode m_textureFilterMode = TextureFilterMode::Linear;
};

using D3D11PipelineState = D3D11Pipeline;

} // namespace qc
#endif

#endif // __QUARK_D3D11_PIPELINE__
