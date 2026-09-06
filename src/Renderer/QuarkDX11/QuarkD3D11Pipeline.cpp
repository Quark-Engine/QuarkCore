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
            float4 worldPosition : WORLD_POSITION;
            float4 normal : NORMAL;
            float4 tangent : TANGENT;
        };

        struct VSOutput {
            float4 position : SV_POSITION;
            float2 texCoord : TEXCOORD0;
            float4 color : COLOR;
            float4 worldPosition : WORLD_POSITION;
            float4 normal : NORMAL;
            float4 tangent : TANGENT;
        };

        VSOutput main(VSInput input) {
            VSOutput output;
            output.position = input.position;
            output.texCoord = input.texCoord;
            output.color = input.color;
            output.worldPosition = input.worldPosition;
            output.normal = input.normal;
            output.tangent = input.tangent;
            return output;
        }
    )";

    static constexpr char texturedPixelSource3D[] = R"(
        Texture2D textureMap : register(t0);
        SamplerState textureSampler : register(s0);
        Texture2D normalMap : register(t6);
        SamplerState normalSampler : register(s6);

        cbuffer LightData : register(b1) {
            float4 uAmbientColor;
            float4 uViewPosition;
            float4 uLightPositions[4];
            float4 uLightColors[4];
            float4 uLightParams[4];
        };

        struct PSInput {
            float4 position : SV_POSITION;
            float2 texCoord : TEXCOORD0;
            float4 color : COLOR;
            float4 worldPosition : WORLD_POSITION;
            float4 normal : NORMAL;
            float4 tangent : TANGENT;
        };

        float3 PerturbNormal(float3 geometricNormal, float4 tangent,
                             float2 texCoord) {
            float3 mapNormal = (normalMap.Sample(normalSampler, texCoord).xyz * 2.0) - 1.0;
            float3 t = normalize(tangent.xyz);
            float3 n = normalize(geometricNormal);
            float3 b = normalize(cross(n, t));
            float3 worldNormal = normalize(t * mapNormal.x + b * mapNormal.y + n * mapNormal.z);
            if (dot(worldNormal, n) < 0.0) return n;
            return worldNormal;
        }

        float3 ApplyLights(float3 worldPos, float3 normal, float3 baseColor) {
            float3 result = baseColor * uAmbientColor.rgb;
            float3 n = normalize(normal);
            int enabled = 0;
            for (int i = 0; i < 4; ++i) {
                if (uLightParams[i].y < 0.5) { continue; }
                enabled = 1;
                float dist = length(uLightPositions[i].xyz - worldPos);
                if (dist < 0.0001) { dist = 0.0001; }
                float3 toLight = (uLightPositions[i].xyz - worldPos) / dist;
                if (uLightParams[i].z < 0.5) {
                    toLight = -normalize(uLightPositions[i].xyz);
                }
                float diff = saturate(dot(n, toLight));
                float attenuation = 1.0 / (1.0 + uLightParams[i].x * dist * dist);
                result += baseColor * uLightColors[i].rgb * diff * attenuation;
            }
            if (enabled == 0) { return baseColor; }
            return saturate(result);
        }

        float4 main(PSInput input) : SV_TARGET {
            float4 texel = textureMap.Sample(textureSampler, input.texCoord);
            float3 baseColor = texel.rgb * input.color.rgb;
            float3 shadingNormal = input.normal.xyz;
            if (input.normal.w > 0.5 && input.tangent.w > 0.5) {
                shadingNormal = PerturbNormal(input.normal.xyz, input.tangent, input.texCoord);
            }
            float3 lit = baseColor;
            if (input.normal.w > 0.5) {
                lit = ApplyLights(input.worldPosition.xyz, shadingNormal, baseColor);
            }
            return float4(lit, texel.a * input.color.a);
        }
    )";

    static constexpr char vertexSource3D[] = R"(
        struct VSInput {
            float4 position : POSITION;
            float4 color : COLOR;
            float4 worldPosition : WORLD_POSITION;
            float4 normal : NORMAL;
        };

        struct VSOutput {
            float4 position : SV_POSITION;
            float4 color : COLOR;
            float4 worldPosition : WORLD_POSITION;
            float4 normal : NORMAL;
        };

        VSOutput main(VSInput input) {
            VSOutput output;
            output.position = input.position;
            output.color = input.color;
            output.worldPosition = input.worldPosition;
            output.normal = input.normal;
            return output;
        }
    )";

    static constexpr char pixelSource3D[] = R"(
        cbuffer LightData : register(b1) {
            float4 uAmbientColor;
            float4 uViewPosition;
            float4 uLightPositions[4];
            float4 uLightColors[4];
            float4 uLightParams[4];
        };

        struct PSInput {
            float4 position : SV_POSITION;
            float4 color : COLOR;
            float4 worldPosition : WORLD_POSITION;
            float4 normal : NORMAL;
        };

        float3 ApplyLights(float3 worldPos, float3 normal, float3 baseColor) {
            float3 result = baseColor * uAmbientColor.rgb;
            float3 n = normalize(normal);
            int enabled = 0;
            for (int i = 0; i < 4; ++i) {
                if (uLightParams[i].y < 0.5) { continue; }
                enabled = 1;
                float dist = length(uLightPositions[i].xyz - worldPos);
                if (dist < 0.0001) { dist = 0.0001; }
                float3 toLight = (uLightPositions[i].xyz - worldPos) / dist;
                if (uLightParams[i].z < 0.5) {
                    toLight = -normalize(uLightPositions[i].xyz);
                }
                float diff = saturate(dot(n, toLight));
                float attenuation = 1.0 / (1.0 + uLightParams[i].x * dist * dist);
                result += baseColor * uLightColors[i].rgb * diff * attenuation;
            }
            if (enabled == 0) { return baseColor; }
            return saturate(result);
        }

        float4 main(PSInput input) : SV_TARGET {
            float3 baseColor = input.color.rgb;
            float3 lit = baseColor;
            if (input.normal.w > 0.5) {
                lit = ApplyLights(input.worldPosition.xyz, input.normal.xyz, baseColor);
            }
            return float4(lit, input.color.a);
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
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"WORLD_POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 40,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 72,
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
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"WORLD_POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48,
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

    D3D11_BUFFER_DESC lightBufferDescription{};
    lightBufferDescription.ByteWidth = sizeof(D3D11LightConstantData);
    lightBufferDescription.Usage = D3D11_USAGE_DEFAULT;
    lightBufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    lightBufferDescription.CPUAccessFlags = 0;

    d3d11::ThrowIfFailed(device->CreateBuffer(&lightBufferDescription, nullptr,
                                              &m_lightConstantBuffer),
                         "ID3D11Device::CreateBuffer light constants");

    m_vertexBuffer = resources.VertexBuffer();
    m_vertexBuffer3D = resources.VertexBuffer3D();

    TraceLog(LogLevel::Info, "D3D11",
             "Built-in pipeline created (Input Layout, Rasterizer, VS, PS).");
}

void D3D11Pipeline::BindTexture3D(ID3D11DeviceContext *context,
                                 ID3D11ShaderResourceView *shaderResource,
                                 ID3D11ShaderResourceView *normalResource) const
{
    const UINT stride = sizeof(float) * 22;
    const UINT offset = 0;
    ID3D11Buffer *vertexBuffer = m_vertexBuffer3D;

    context->IASetInputLayout(m_texturedInputLayout3D.Get());
    context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->RSSetState(m_backfaceCullingEnabled ? m_rasterizerStateCull.Get()
                                                 : m_rasterizerState.Get());
    context->VSSetShader(m_texturedVertexShader3D.Get(), nullptr, 0);
    context->PSSetShader(m_texturedPixelShader3D.Get(), nullptr, 0);
    context->PSSetConstantBuffers(1, 1, m_lightConstantBuffer.GetAddressOf());
    context->PSSetShaderResources(0, 1, &shaderResource);
    ID3D11ShaderResourceView *normalResourceCopy = normalResource;
    context->PSSetShaderResources(6, 1, &normalResourceCopy);
    context->PSSetSamplers(0, 1, m_textureSampler.GetAddressOf());
    context->PSSetSamplers(6, 1, m_textureSampler.GetAddressOf());
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
    const UINT stride = sizeof(float) * 16;
    const UINT offset = 0;
    ID3D11ShaderResourceView *nullResource = nullptr;
    context->IASetInputLayout(m_inputLayout3D.Get());
    context->IASetVertexBuffers(0, 1, &m_vertexBuffer3D, &stride, &offset);
    context->RSSetState(m_backfaceCullingEnabled ? m_rasterizerStateCull.Get()
                                                 : m_rasterizerState.Get());
    context->VSSetShader(m_vertexShader3D.Get(), nullptr, 0);
    context->PSSetShader(m_pixelShader3D.Get(), nullptr, 0);
    context->PSSetConstantBuffers(1, 1, m_lightConstantBuffer.GetAddressOf());
    context->PSSetShaderResources(0, 1, &nullResource);
    context->PSSetSamplers(0, 1, m_textureSampler.GetAddressOf());
    context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
    const float blendFactor[] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
}

void D3D11Pipeline::UpdateLights(ID3D11DeviceContext *context,
                                 const D3D11LightConstantData &lights)
{
    if (!context || !m_lightConstantBuffer)
    {
        return;
    }

    context->UpdateSubresource(m_lightConstantBuffer.Get(), 0, nullptr, &lights, 0, 0);
}

void D3D11Pipeline::CreateSamplerState(TextureFilterMode mode)
{
    if (!m_device) {
        return;
    }

    const bool point = (mode == TextureFilterMode::Nearest);

    D3D11_TEXTURE_ADDRESS_MODE addressU = D3D11_TEXTURE_ADDRESS_WRAP;
    D3D11_TEXTURE_ADDRESS_MODE addressV = D3D11_TEXTURE_ADDRESS_WRAP;
    D3D11_TEXTURE_ADDRESS_MODE addressW = D3D11_TEXTURE_ADDRESS_WRAP;

    switch (m_textureWrapMode) {
        case TEXTURE_WRAP_CLAMP:
            addressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            addressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            addressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            break;
        case TEXTURE_WRAP_MIRROR_REPEAT:
            addressU = D3D11_TEXTURE_ADDRESS_MIRROR;
            addressV = D3D11_TEXTURE_ADDRESS_MIRROR;
            addressW = D3D11_TEXTURE_ADDRESS_MIRROR;
            break;
        case TEXTURE_WRAP_MIRROR_CLAMP:
            addressU = D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
            addressV = D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
            addressW = D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
            break;
        case TEXTURE_WRAP_REPEAT:
        default:
            addressU = D3D11_TEXTURE_ADDRESS_WRAP;
            addressV = D3D11_TEXTURE_ADDRESS_WRAP;
            addressW = D3D11_TEXTURE_ADDRESS_WRAP;
            break;
    }

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter =
        point ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU = addressU;
    samplerDescription.AddressV = addressV;
    samplerDescription.AddressW = addressW;
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

void D3D11Pipeline::SetTextureWrapMode(int wrap)
{
    m_textureWrapMode = wrap;
    CreateSamplerState(m_textureFilterMode);
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
    m_lightConstantBuffer.Reset();
    m_backfaceCullingEnabled = false;
    m_textureFilterMode = TextureFilterMode::Linear;
    m_device = nullptr;

    TraceLog(LogLevel::Trace, "D3D11", "Built-in pipeline state released.");
}

} // namespace qc
#endif