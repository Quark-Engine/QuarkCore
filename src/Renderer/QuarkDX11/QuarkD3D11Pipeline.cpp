#include "QuarkD3D11Pipeline.hpp"

#if defined(_WIN32)
namespace qci
{
    void D3D11Pipeline::Initialize(ID3D11Device* pDevice, D3D11ShaderCompiler& compiler,
                                   D3D11Resources& resources)
    {
        TraceLog(LogLevel::Info, "D3D11", "Creating built-in D3D11 pipeline...");

        m_pDevice = pDevice;

        static constexpr char s_aTexturedVertexSource[] = R"(
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

        static constexpr char s_aTexturedPixelSource[] = R"(
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

        static constexpr char s_aTexturedVertexSource3D[] = R"(
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

        static constexpr char s_aTexturedPixelSource3D[] = R"(
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

        static constexpr char s_aVertexSource3D[] = R"(
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

        static constexpr char s_aPixelSource3D[] = R"(
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

        const auto texturedVertexShader = compiler.Compile(s_aTexturedVertexSource, "main", "vs_5_0");
        const auto texturedPixelShader = compiler.Compile(s_aTexturedPixelSource, "main", "ps_5_0");
        const auto texturedVertexShader3D = compiler.Compile(s_aTexturedVertexSource3D, "main", "vs_5_0");
        const auto texturedPixelShader3D = compiler.Compile(s_aTexturedPixelSource3D, "main", "ps_5_0");
        const auto vertexShader3D = compiler.Compile(s_aVertexSource3D, "main", "vs_5_0");
        const auto pixelShader3D = compiler.Compile(s_aPixelSource3D, "main", "ps_5_0");

        d3d11::ThrowIfFailed(
            pDevice->CreateVertexShader(texturedVertexShader->GetBufferPointer(),
                                        texturedVertexShader->GetBufferSize(), nullptr,
                                        &m_TexturedVertexShader),
            "ID3D11Device::CreateVertexShader textured");
        d3d11::ThrowIfFailed(
            pDevice->CreatePixelShader(texturedPixelShader->GetBufferPointer(),
                                       texturedPixelShader->GetBufferSize(), nullptr,
                                       &m_TexturedPixelShader),
            "ID3D11Device::CreatePixelShader textured");

        d3d11::ThrowIfFailed(
            pDevice->CreateVertexShader(texturedVertexShader3D->GetBufferPointer(),
                                        texturedVertexShader3D->GetBufferSize(), nullptr,
                                        &m_TexturedVertexShader3D),
            "ID3D11Device::CreateVertexShader textured 3D");
        d3d11::ThrowIfFailed(
            pDevice->CreatePixelShader(texturedPixelShader3D->GetBufferPointer(),
                                       texturedPixelShader3D->GetBufferSize(), nullptr,
                                       &m_TexturedPixelShader3D),
            "ID3D11Device::CreatePixelShader textured 3D");

        d3d11::ThrowIfFailed(
            pDevice->CreateVertexShader(vertexShader3D->GetBufferPointer(),
                                        vertexShader3D->GetBufferSize(), nullptr,
                                        &m_VertexShader3D),
            "ID3D11Device::CreateVertexShader 3D");
        d3d11::ThrowIfFailed(
            pDevice->CreatePixelShader(pixelShader3D->GetBufferPointer(),
                                       pixelShader3D->GetBufferSize(), nullptr,
                                       &m_PixelShader3D),
            "ID3D11Device::CreatePixelShader 3D");

        const D3D11_INPUT_ELEMENT_DESC aTexturedInputElements[] =
            {
                {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
                 D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
                 D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
                 D3D11_INPUT_PER_VERTEX_DATA, 0}};

        d3d11::ThrowIfFailed(
            pDevice->CreateInputLayout(aTexturedInputElements,
                                       ARRAYSIZE(aTexturedInputElements),
                                       texturedVertexShader->GetBufferPointer(),
                                       texturedVertexShader->GetBufferSize(),
                                       &m_TexturedInputLayout),
            "ID3D11Device::CreateInputLayout textured");

        const D3D11_INPUT_ELEMENT_DESC aTexturedInputElements3D[] =
            {
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
                 D3D11_INPUT_PER_VERTEX_DATA, 0}};

        d3d11::ThrowIfFailed(
            pDevice->CreateInputLayout(aTexturedInputElements3D,
                                       ARRAYSIZE(aTexturedInputElements3D),
                                       texturedVertexShader3D->GetBufferPointer(),
                                       texturedVertexShader3D->GetBufferSize(),
                                       &m_TexturedInputLayout3D),
            "ID3D11Device::CreateInputLayout textured 3D");

        const D3D11_INPUT_ELEMENT_DESC aInputElements3D[] =
            {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
                 D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
                 D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"WORLD_POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,
                 D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48,
                 D3D11_INPUT_PER_VERTEX_DATA, 0}};

        d3d11::ThrowIfFailed(
            pDevice->CreateInputLayout(aInputElements3D,
                                       ARRAYSIZE(aInputElements3D),
                                       vertexShader3D->GetBufferPointer(),
                                       vertexShader3D->GetBufferSize(),
                                       &m_InputLayout3D),
            "ID3D11Device::CreateInputLayout 3D");

        D3D11_RASTERIZER_DESC rasterizerDescription{};
        rasterizerDescription.FillMode = D3D11_FILL_SOLID;
        rasterizerDescription.CullMode = D3D11_CULL_NONE;
        rasterizerDescription.DepthClipEnable = TRUE;

        d3d11::ThrowIfFailed(pDevice->CreateRasterizerState(&rasterizerDescription, &m_RasterizerState),
                             "ID3D11Device::CreateRasterizerState");

        D3D11_DEPTH_STENCIL_DESC depthStencilDescription{};
        depthStencilDescription.DepthEnable = TRUE;
        depthStencilDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthStencilDescription.DepthFunc = D3D11_COMPARISON_LESS;
        depthStencilDescription.StencilEnable = FALSE;

        d3d11::ThrowIfFailed(
            pDevice->CreateDepthStencilState(&depthStencilDescription, &m_DepthStencilState),
            "ID3D11Device::CreateDepthStencilState");

        D3D11_DEPTH_STENCIL_DESC depthStencilDisabledDescription{};
        depthStencilDisabledDescription.DepthEnable = FALSE;
        depthStencilDisabledDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depthStencilDisabledDescription.StencilEnable = FALSE;

        d3d11::ThrowIfFailed(
            pDevice->CreateDepthStencilState(&depthStencilDisabledDescription,
                                             &m_DepthStencilDisabledState),
            "ID3D11Device::CreateDepthStencilState disabled");

        D3D11_RASTERIZER_DESC cullRasterizerDescription = rasterizerDescription;
        cullRasterizerDescription.CullMode = D3D11_CULL_BACK;
        cullRasterizerDescription.FrontCounterClockwise = TRUE;

        d3d11::ThrowIfFailed(
            pDevice->CreateRasterizerState(&cullRasterizerDescription, &m_RasterizerStateCull),
            "ID3D11Device::CreateRasterizerState cull");

        CreateSamplerState(m_TextureFilterMode);

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
            pDevice->CreateBlendState(&blendDescription, &m_BlendState),
            "ID3D11Device::CreateBlendState");

        D3D11_BUFFER_DESC lightBufferDescription{};
        lightBufferDescription.ByteWidth = sizeof(D3D11LightConstantData);
        lightBufferDescription.Usage = D3D11_USAGE_DEFAULT;
        lightBufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        lightBufferDescription.CPUAccessFlags = 0;

        d3d11::ThrowIfFailed(pDevice->CreateBuffer(&lightBufferDescription, nullptr,
                                                   &m_LightConstantBuffer),
                             "ID3D11Device::CreateBuffer light constants");

        m_pVertexBuffer = resources.VertexBuffer();
        m_pVertexBuffer3D = resources.VertexBuffer3D();

        TraceLog(LogLevel::Info, "D3D11",
                 "Built-in pipeline created (Input Layout, Rasterizer, VS, PS).");
    }

    void D3D11Pipeline::BindTexture3D(ID3D11DeviceContext* pContext,
                                      ID3D11ShaderResourceView* pShaderResource,
                                      ID3D11ShaderResourceView* pNormalResource) const
    {
        const UINT stride = sizeof(float) * 22;
        const UINT offset = 0;
        ID3D11Buffer* pVertexBuffer = m_pVertexBuffer3D;

        pContext->IASetInputLayout(m_TexturedInputLayout3D.Get());
        pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &stride, &offset);
        pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pContext->RSSetState(m_BackfaceCullingEnabled ? m_RasterizerStateCull.Get()
                                                      : m_RasterizerState.Get());
        pContext->VSSetShader(m_TexturedVertexShader3D.Get(), nullptr, 0);
        pContext->PSSetShader(m_TexturedPixelShader3D.Get(), nullptr, 0);
        pContext->PSSetConstantBuffers(1, 1, m_LightConstantBuffer.GetAddressOf());
        pContext->PSSetShaderResources(0, 1, &pShaderResource);
        ID3D11ShaderResourceView* pNormalResourceCopy = pNormalResource;
        pContext->PSSetShaderResources(6, 1, &pNormalResourceCopy);
        pContext->PSSetSamplers(0, 1, m_TextureSampler.GetAddressOf());
        pContext->PSSetSamplers(6, 1, m_TextureSampler.GetAddressOf());
        const float aBlendFactor[] = {0.0f, 0.0f, 0.0f, 0.0f};
        pContext->OMSetDepthStencilState(m_DepthStencilState.Get(), 0);
        pContext->OMSetBlendState(m_BlendState.Get(), aBlendFactor, 0xFFFFFFFF);
    }

    void D3D11Pipeline::BindBatch(ID3D11DeviceContext* pContext,
                                  ID3D11Buffer* pVertexBuffer,
                                  ID3D11Buffer* pIndexBuffer) const
    {
        const UINT stride = sizeof(float) * 8;
        const UINT offset = 0;

        pContext->IASetInputLayout(m_TexturedInputLayout.Get());
        pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &stride, &offset);
        pContext->IASetIndexBuffer(pIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
        pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pContext->RSSetState(m_RasterizerState.Get());
        pContext->VSSetShader(m_TexturedVertexShader.Get(), nullptr, 0);
        pContext->PSSetShader(m_TexturedPixelShader.Get(), nullptr, 0);
        pContext->PSSetSamplers(0, 1, m_TextureSampler.GetAddressOf());
        const float aBlendFactor[] = {0.0f, 0.0f, 0.0f, 0.0f};
        pContext->OMSetDepthStencilState(m_DepthStencilDisabledState.Get(), 0);
        pContext->OMSetBlendState(m_BlendState.Get(), aBlendFactor, 0xFFFFFFFF);
    }

    void D3D11Pipeline::Bind3D(ID3D11DeviceContext* pContext) const
    {
        const UINT stride = sizeof(float) * 16;
        const UINT offset = 0;
        ID3D11ShaderResourceView* pNullResource = nullptr;
        pContext->IASetInputLayout(m_InputLayout3D.Get());
        pContext->IASetVertexBuffers(0, 1, &m_pVertexBuffer3D, &stride, &offset);
        pContext->RSSetState(m_BackfaceCullingEnabled ? m_RasterizerStateCull.Get()
                                                      : m_RasterizerState.Get());
        pContext->VSSetShader(m_VertexShader3D.Get(), nullptr, 0);
        pContext->PSSetShader(m_PixelShader3D.Get(), nullptr, 0);
        pContext->PSSetConstantBuffers(1, 1, m_LightConstantBuffer.GetAddressOf());
        pContext->PSSetShaderResources(0, 1, &pNullResource);
        pContext->PSSetSamplers(0, 1, m_TextureSampler.GetAddressOf());
        pContext->OMSetDepthStencilState(m_DepthStencilState.Get(), 0);
        const float aBlendFactor[] = {0.0f, 0.0f, 0.0f, 0.0f};
        pContext->OMSetBlendState(m_BlendState.Get(), aBlendFactor, 0xFFFFFFFF);
    }

    void D3D11Pipeline::UpdateLights(ID3D11DeviceContext* pContext,
                                     const D3D11LightConstantData& lights)
    {
        if (!pContext || !m_LightConstantBuffer)
        {
            return;
        }

        pContext->UpdateSubresource(m_LightConstantBuffer.Get(), 0, nullptr, &lights, 0, 0);
    }

    void D3D11Pipeline::CreateSamplerState(TextureFilter mode)
    {
        if (!m_pDevice)
        {
            return;
        }

        const bool point = (mode == TEXTURE_FILTER_POINT);

        D3D11_TEXTURE_ADDRESS_MODE addressU = D3D11_TEXTURE_ADDRESS_WRAP;
        D3D11_TEXTURE_ADDRESS_MODE addressV = D3D11_TEXTURE_ADDRESS_WRAP;
        D3D11_TEXTURE_ADDRESS_MODE addressW = D3D11_TEXTURE_ADDRESS_WRAP;

        switch (m_TextureWrapMode)
        {
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

        m_TextureSampler.Reset();
        d3d11::ThrowIfFailed(m_pDevice->CreateSamplerState(&samplerDescription, &m_TextureSampler),
                             "ID3D11Device::CreateSamplerState");
    }

    void D3D11Pipeline::SetTextureFilterMode(TextureFilter mode)
    {
        if (mode == m_TextureFilterMode)
        {
            return;
        }

        m_TextureFilterMode = mode;
        CreateSamplerState(mode);
        TraceLog(LogLevel::Info, "D3D11",
                 TextFormat("Texture filter mode set to %s.",
                            mode == TEXTURE_FILTER_POINT ? "Nearest (point)" : "Bilinear"));
    }

    void D3D11Pipeline::SetTextureWrapMode(int wrap)
    {
        m_TextureWrapMode = wrap;
        CreateSamplerState(m_TextureFilterMode);
    }

    void D3D11Pipeline::Shutdown()
    {
        TraceLog(LogLevel::Trace, "D3D11", "Releasing built-in pipeline state...");

        m_pVertexBuffer = nullptr;
        m_pVertexBuffer3D = nullptr;
        m_RasterizerState.Reset();
        m_TextureSampler.Reset();
        m_TexturedInputLayout.Reset();
        m_TexturedPixelShader.Reset();
        m_TexturedVertexShader.Reset();
        m_TexturedInputLayout3D.Reset();
        m_TexturedPixelShader3D.Reset();
        m_TexturedVertexShader3D.Reset();
        m_BlendState.Reset();
        m_InputLayout3D.Reset();
        m_PixelShader3D.Reset();
        m_VertexShader3D.Reset();
        m_DepthStencilState.Reset();
        m_DepthStencilDisabledState.Reset();
        m_RasterizerStateCull.Reset();
        m_LightConstantBuffer.Reset();
        m_BackfaceCullingEnabled = false;
        m_TextureFilterMode = TEXTURE_FILTER_BILINEAR;
        m_pDevice = nullptr;

        TraceLog(LogLevel::Trace, "D3D11", "Built-in pipeline state released.");
    }
} // qci
#endif
