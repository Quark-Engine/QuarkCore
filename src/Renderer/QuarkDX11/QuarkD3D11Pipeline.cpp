#include "QuarkD3D11Pipeline.hpp"

#if defined(_WIN32)
namespace qc {

void D3D11Pipeline::Initialize(ID3D11Device *device, D3D11ShaderCompiler &compiler,
                               D3D11Resources &resources)
{
    TraceLog(LogLevel::Info, "D3D11", "Creating built-in D3D11 pipeline...");

    m_device = device;

    static constexpr char texturedVertexSource[] = R"(
        struct VSInput {
            float2 position : POSITION;
            float2 texCoord : TEXCOORD0;
            float4 color : COLOR;
        };

        struct VSOutput {
            float4 position : SV_POSITION;
            float2 texCoord : TEXCOORD0;
            float4 color : COLOR;
        };

        VSOutput main(VSInput input) {
            VSOutput output;
            output.position = float4(input.position, 0.0, 1.0);
            output.texCoord = input.texCoord;
            output.color = input.color;
            return output;
        }
    )";

    static constexpr char texturedPixelSource[] = R"(
        Texture2D textureMap : register(t0);
        SamplerState textureSampler : register(s0);

        struct PSInput {
            float4 position : SV_POSITION;
            float2 texCoord : TEXCOORD0;
            float4 color : COLOR;
        };

        float4 main(PSInput input) : SV_TARGET {
            return textureMap.Sample(textureSampler, input.texCoord) * input.color;
        }
    )";

    static constexpr char texturedVertexSource3D[] = R"(
        struct VSInput {
            float4 position : POSITION;
            float2 texCoord : TEXCOORD0;
            float4 color : COLOR;
        };

        struct VSOutput {
            float4 position : SV_POSITION;
            float2 texCoord : TEXCOORD0;
            float4 color : COLOR;
        };

        VSOutput main(VSInput input) {
            VSOutput output;
            output.position = input.position;
            output.texCoord = input.texCoord;
            output.color = input.color;
            return output;
        }
    )";

    static constexpr char texturedPixelSource3D[] = R"(
        Texture2D textureMap : register(t0);
        SamplerState textureSampler : register(s0);

        struct PSInput {
            float4 position : SV_POSITION;
            float2 texCoord : TEXCOORD0;
            float4 color : COLOR;
        };

        float4 main(PSInput input) : SV_TARGET {
            return textureMap.Sample(textureSampler, input.texCoord) * input.color;
        }
    )";

    static constexpr char vertexSource3D[] = R"(
        struct VSInput {
            float4 position : POSITION;
            float4 color : COLOR;
        };

        struct VSOutput {
            float4 position : SV_POSITION;
            float4 color : COLOR;
        };

        VSOutput main(VSInput input) {
            VSOutput output;
            output.position = input.position;
            output.color = input.color;
            return output;
        }
    )";

    static constexpr char pixelSource3D[] = R"(
        struct PSInput {
            float4 position : SV_POSITION;
            float4 color : COLOR;
        };

        float4 main(PSInput input) : SV_TARGET {
            return input.color;
        }
    )";

    const auto texturedVertexShader = compiler.Compile(texturedVertexSource, "main", "vs_5_0");
    const auto texturedPixelShader = compiler.Compile(texturedPixelSource, "main", "ps_5_0");
    const auto texturedVertexShader3D = compiler.Compile(texturedVertexSource3D, "main", "vs_5_0");
    const auto texturedPixelShader3D = compiler.Compile(texturedPixelSource3D, "main", "ps_5_0");
    const auto vertexShader3D = compiler.Compile(vertexSource3D, "main", "vs_5_0");
    const auto pixelShader3D = compiler.Compile(pixelSource3D, "main", "ps_5_0");

    d3d11::ThrowIfFailed(
        device->CreateVertexShader(texturedVertexShader->GetBufferPointer(),
                                   texturedVertexShader->GetBufferSize(), nullptr,
                                   &m_texturedVertexShader),
        "ID3D11Device::CreateVertexShader textured");
    d3d11::ThrowIfFailed(
        device->CreatePixelShader(texturedPixelShader->GetBufferPointer(),
                                  texturedPixelShader->GetBufferSize(), nullptr,
                                  &m_texturedPixelShader),
        "ID3D11Device::CreatePixelShader textured");

    d3d11::ThrowIfFailed(
        device->CreateVertexShader(texturedVertexShader3D->GetBufferPointer(),
                                   texturedVertexShader3D->GetBufferSize(), nullptr,
                                   &m_texturedVertexShader3D),
        "ID3D11Device::CreateVertexShader textured 3D");
    d3d11::ThrowIfFailed(
        device->CreatePixelShader(texturedPixelShader3D->GetBufferPointer(),
                                  texturedPixelShader3D->GetBufferSize(), nullptr,
                                  &m_texturedPixelShader3D),
        "ID3D11Device::CreatePixelShader textured 3D");

    d3d11::ThrowIfFailed(
        device->CreateVertexShader(vertexShader3D->GetBufferPointer(),
                                   vertexShader3D->GetBufferSize(), nullptr,
                                   &m_vertexShader3D),
        "ID3D11Device::CreateVertexShader 3D");
    d3d11::ThrowIfFailed(
        device->CreatePixelShader(pixelShader3D->GetBufferPointer(),
                                  pixelShader3D->GetBufferSize(), nullptr,
                                  &m_pixelShader3D),
        "ID3D11Device::CreatePixelShader 3D");

    const D3D11_INPUT_ELEMENT_DESC texturedInputElements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
         D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    d3d11::ThrowIfFailed(
        device->CreateInputLayout(texturedInputElements,
                                  ARRAYSIZE(texturedInputElements),
                                  texturedVertexShader->GetBufferPointer(),
                                  texturedVertexShader->GetBufferSize(),
                                  &m_texturedInputLayout),
        "ID3D11Device::CreateInputLayout textured");

    const D3D11_INPUT_ELEMENT_DESC texturedInputElements3D[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24,
         D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    d3d11::ThrowIfFailed(
        device->CreateInputLayout(texturedInputElements3D,
                                  ARRAYSIZE(texturedInputElements3D),
                                  texturedVertexShader3D->GetBufferPointer(),
                                  texturedVertexShader3D->GetBufferSize(),
                                  &m_texturedInputLayout3D),
        "ID3D11Device::CreateInputLayout textured 3D");

    const D3D11_INPUT_ELEMENT_DESC inputElements3D[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
         D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    d3d11::ThrowIfFailed(
        device->CreateInputLayout(inputElements3D,
                                  ARRAYSIZE(inputElements3D),
                                  vertexShader3D->GetBufferPointer(),
                                  vertexShader3D->GetBufferSize(),
                                  &m_inputLayout3D),
        "ID3D11Device::CreateInputLayout 3D");

    D3D11_RASTERIZER_DESC rasterizerDescription{};
    rasterizerDescription.FillMode = D3D11_FILL_SOLID;
    rasterizerDescription.CullMode = D3D11_CULL_NONE;
    rasterizerDescription.DepthClipEnable = TRUE;

    d3d11::ThrowIfFailed(device->CreateRasterizerState(&rasterizerDescription, &m_rasterizerState),
                         "ID3D11Device::CreateRasterizerState");

    D3D11_DEPTH_STENCIL_DESC depthStencilDescription{};
    depthStencilDescription.DepthEnable = TRUE;
    depthStencilDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthStencilDescription.DepthFunc = D3D11_COMPARISON_LESS;
    depthStencilDescription.StencilEnable = FALSE;

    d3d11::ThrowIfFailed(
        device->CreateDepthStencilState(&depthStencilDescription, &m_depthStencilState),
        "ID3D11Device::CreateDepthStencilState");

    D3D11_DEPTH_STENCIL_DESC depthStencilDisabledDescription{};
    depthStencilDisabledDescription.DepthEnable = FALSE;
    depthStencilDisabledDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depthStencilDisabledDescription.StencilEnable = FALSE;

    d3d11::ThrowIfFailed(
        device->CreateDepthStencilState(&depthStencilDisabledDescription,
                                        &m_depthStencilDisabledState),
        "ID3D11Device::CreateDepthStencilState disabled");

    D3D11_RASTERIZER_DESC cullRasterizerDescription = rasterizerDescription;
    cullRasterizerDescription.CullMode = D3D11_CULL_BACK;
    cullRasterizerDescription.FrontCounterClockwise = TRUE;

    d3d11::ThrowIfFailed(
        device->CreateRasterizerState(&cullRasterizerDescription, &m_rasterizerStateCull),
        "ID3D11Device::CreateRasterizerState cull");

    CreateSamplerState(m_textureFilterMode);

    D3D11_BLEND_DESC blendDescription{};
    blendDescription.RenderTarget[0].BlendEnable = TRUE;
    blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    d3d11::ThrowIfFailed(
        device->CreateBlendState(&blendDescription, &m_blendState),
        "ID3D11Device::CreateBlendState");

    m_vertexBuffer = resources.VertexBuffer();
    m_vertexBuffer3D = resources.VertexBuffer3D();

    TraceLog(LogLevel::Info, "D3D11",
             "Built-in pipeline created (Input Layout, Rasterizer, VS, PS).");
}

void D3D11Pipeline::BindTexture3D(ID3D11DeviceContext *context,
                                 ID3D11ShaderResourceView *shaderResource) const
{
    const UINT stride = sizeof(float) * 10;
    const UINT offset = 0;
    ID3D11Buffer *vertexBuffer = m_vertexBuffer3D;

    context->IASetInputLayout(m_texturedInputLayout3D.Get());
    context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->RSSetState(m_backfaceCullingEnabled ? m_rasterizerStateCull.Get()
                                                 : m_rasterizerState.Get());
    context->VSSetShader(m_texturedVertexShader3D.Get(), nullptr, 0);
    context->PSSetShader(m_texturedPixelShader3D.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, &shaderResource);
    context->PSSetSamplers(0, 1, m_textureSampler.GetAddressOf());
    const float blendFactor[] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
    context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
}

void D3D11Pipeline::BindBatch(ID3D11DeviceContext *context,
                              ID3D11Buffer *vertexBuffer,
                              ID3D11Buffer *indexBuffer) const
{
    const UINT stride = sizeof(float) * 8;
    const UINT offset = 0;

    context->IASetInputLayout(m_texturedInputLayout.Get());
    context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    context->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->RSSetState(m_rasterizerState.Get());
    context->VSSetShader(m_texturedVertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_texturedPixelShader.Get(), nullptr, 0);
    context->PSSetSamplers(0, 1, m_textureSampler.GetAddressOf());
    const float blendFactor[] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->OMSetDepthStencilState(m_depthStencilDisabledState.Get(), 0);
    context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
}

void D3D11Pipeline::Bind3D(ID3D11DeviceContext *context) const
{
    const UINT stride = sizeof(float) * 8;
    const UINT offset = 0;
    ID3D11ShaderResourceView *nullResource = nullptr;
    context->IASetInputLayout(m_inputLayout3D.Get());
    context->IASetVertexBuffers(0, 1, &m_vertexBuffer3D, &stride, &offset);
    context->RSSetState(m_backfaceCullingEnabled ? m_rasterizerStateCull.Get()
                                                 : m_rasterizerState.Get());
    context->VSSetShader(m_vertexShader3D.Get(), nullptr, 0);
    context->PSSetShader(m_pixelShader3D.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, &nullResource);
    context->PSSetSamplers(0, 1, m_textureSampler.GetAddressOf());
    context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
    const float blendFactor[] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
}

void D3D11Pipeline::CreateSamplerState(TextureFilterMode mode)
{
    if (!m_device) {
        return;
    }

    const bool point = (mode == TextureFilterMode::Nearest);

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter =
        point ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDescription.MinLOD = 0.0f;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;

    m_textureSampler.Reset();
    d3d11::ThrowIfFailed(m_device->CreateSamplerState(&samplerDescription, &m_textureSampler),
                         "ID3D11Device::CreateSamplerState");
}

void D3D11Pipeline::SetTextureFilterMode(TextureFilterMode mode)
{
    if (mode == m_textureFilterMode) {
        return;
    }

    m_textureFilterMode = mode;
    CreateSamplerState(mode);
    TraceLog(LogLevel::Info, "D3D11",
             TextFormat("Texture filter mode set to %s.",
                        mode == TextureFilterMode::Nearest ? "Nearest (point)" : "Bilinear"));
}

void D3D11Pipeline::Shutdown()
{
    TraceLog(LogLevel::Trace, "D3D11", "Releasing built-in pipeline state...");

    m_vertexBuffer = nullptr;
    m_vertexBuffer3D = nullptr;
    m_rasterizerState.Reset();
    m_textureSampler.Reset();
    m_texturedInputLayout.Reset();
    m_texturedPixelShader.Reset();
    m_texturedVertexShader.Reset();
    m_texturedInputLayout3D.Reset();
    m_texturedPixelShader3D.Reset();
    m_texturedVertexShader3D.Reset();
    m_blendState.Reset();
    m_inputLayout3D.Reset();
    m_pixelShader3D.Reset();
    m_vertexShader3D.Reset();
    m_depthStencilState.Reset();
    m_depthStencilDisabledState.Reset();
    m_rasterizerStateCull.Reset();
    m_backfaceCullingEnabled = false;
    m_textureFilterMode = TextureFilterMode::Linear;
    m_device = nullptr;

    TraceLog(LogLevel::Trace, "D3D11", "Built-in pipeline state released.");
}

} // namespace qc
#endif