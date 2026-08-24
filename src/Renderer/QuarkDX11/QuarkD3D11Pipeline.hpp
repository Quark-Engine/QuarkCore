#pragma once

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
    ID3D11Buffer *VertexBuffer() const { return m_vertexBuffer; }

private:
    ID3D11Buffer *m_vertexBuffer = nullptr;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerState;
};

using D3D11PipelineState = D3D11Pipeline;

} // namespace qc
#endif