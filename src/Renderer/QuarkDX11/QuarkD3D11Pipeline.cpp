#include "QuarkD3D11Pipeline.hpp"

#if defined(_WIN32)
namespace qc {

void D3D11Pipeline::Initialize(ID3D11Device *device, D3D11ShaderCompiler &compiler,
                               D3D11Resources &resources)
{
    TraceLog(LogLevel::Info, "D3D11", "Creating built-in D3D11 pipeline...");

    static constexpr char vertexSource[] = R"(
        struct VSInput {
            float2 position : POSITION;
            float4 color : COLOR;
        };

        struct VSOutput {
            float4 position : SV_POSITION;
            float4 color : COLOR;
        };

        VSOutput main(VSInput input) {
            VSOutput output;
            output.position = float4(input.position, 0.0, 1.0);
            output.color = input.color;
            return output;
        }
    )";

    static constexpr char pixelSource[] = R"(
        struct PSInput {
            float4 position : SV_POSITION;
            float4 color : COLOR;
        };

        float4 main(PSInput input) : SV_TARGET {
            return input.color;
        }
    )";

    const auto vertexShader = compiler.Compile(vertexSource, "main", "vs_5_0");
    const auto pixelShader = compiler.Compile(pixelSource, "main", "ps_5_0");

    d3d11::ThrowIfFailed(device->CreateVertexShader(vertexShader->GetBufferPointer(),
                                                    vertexShader->GetBufferSize(), nullptr,
                                                    &m_vertexShader),
                         "ID3D11Device::CreateVertexShader");

    d3d11::ThrowIfFailed(device->CreatePixelShader(pixelShader->GetBufferPointer(),
                                                   pixelShader->GetBufferSize(), nullptr,
                                                   &m_pixelShader),
                         "ID3D11Device::CreatePixelShader");

    const D3D11_INPUT_ELEMENT_DESC inputElements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}};

    d3d11::ThrowIfFailed(device->CreateInputLayout(inputElements, ARRAYSIZE(inputElements),
                                                   vertexShader->GetBufferPointer(),
                                                   vertexShader->GetBufferSize(), &m_inputLayout),
                         "ID3D11Device::CreateInputLayout");

    D3D11_RASTERIZER_DESC rasterizerDescription{};
    rasterizerDescription.FillMode = D3D11_FILL_SOLID;
    rasterizerDescription.CullMode = D3D11_CULL_NONE;
    rasterizerDescription.DepthClipEnable = TRUE;

    d3d11::ThrowIfFailed(device->CreateRasterizerState(&rasterizerDescription, &m_rasterizerState),
                         "ID3D11Device::CreateRasterizerState");

    m_vertexBuffer = resources.VertexBuffer();

    TraceLog(LogLevel::Info, "D3D11",
             "Built-in pipeline created (Input Layout, Rasterizer, VS, PS).");
}

void D3D11Pipeline::Bind(ID3D11DeviceContext *context) const
{
    const UINT stride = sizeof(float) * 6;
    const UINT offset = 0;
    context->IASetInputLayout(m_inputLayout.Get());
    context->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->RSSetState(m_rasterizerState.Get());
    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
}

void D3D11Pipeline::Shutdown()
{
    TraceLog(LogLevel::Trace, "D3D11", "Releasing built-in pipeline state...");

    m_vertexBuffer = nullptr;
    m_rasterizerState.Reset();
    m_inputLayout.Reset();
    m_pixelShader.Reset();
    m_vertexShader.Reset();

    TraceLog(LogLevel::Trace, "D3D11", "Built-in pipeline state released.");
}

} // namespace qc
#endif