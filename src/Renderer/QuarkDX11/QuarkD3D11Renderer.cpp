#include "QuarkD3D11Renderer.hpp"
#include "../DebugFont.h"
#include "../DefaultFont.h"
#include "../QuarkAssimpTexture.hpp"
#include "../../QuarkInternal.hpp"
#include "../../QuarkModelAnim.hpp"

#if defined(_WIN32)
#include <ft2build.h>
#include FT_FREETYPE_H
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace qci
{
    namespace
    {
        static Color MultiplyColor(Color lhs, Color rhs)
        {
            return Color{static_cast<unsigned char>((static_cast<unsigned int>(lhs.r) * rhs.r) / 255u),
                         static_cast<unsigned char>((static_cast<unsigned int>(lhs.g) * rhs.g) / 255u),
                         static_cast<unsigned char>((static_cast<unsigned int>(lhs.b) * rhs.b) / 255u),
                         static_cast<unsigned char>((static_cast<unsigned int>(lhs.a) * rhs.a) / 255u)};
        }

        static Mat4 TransposeMat4(const Mat4& matrix)
        {
            Mat4 result{};
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    result.m[row * 4 + col] = matrix.m[col * 4 + row];
                }
            }
            return result;
        }

        const char* s_apShaderLocationNames[SHADER_LOC_COUNT] = {
            "POSITION",   "TEXCOORD0",   "TEXCOORD1",  "NORMAL",      "TANGENT",        "COLOR",
            "mvp",        "view",        "projection", "model",       "normalMatrix",   "viewPos",
            "colDiffuse", "colSpecular", "colAmbient", "albedo",      "metalness",      "normal",
            "roughness",  "occlusion",   "emission",   "height",      "cubemap",        "irradiance",
            "prefilter",  "brdf",        "boneIds",    "boneWeights", "boneTransforms", "instanceTransform"};

        constexpr char DEFAULT_HLSL_VERTEX_SOURCE[] = R"(
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

        constexpr char DEFAULT_HLSL_PIXEL_SOURCE[] = R"(
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

        bool ReadTextFile(const char* pPath, std::string& out)
        {
            if (!pPath)
            {
                return false;
            }

            std::ifstream file(pPath, std::ios::in | std::ios::binary);
            if (!file.is_open())
            {
                return false;
            }

            out.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            return true;
        }

        UINT AlignTo16(UINT value)
        {
            return (value + 15u) & ~15u;
        }

        UINT CountMaskComponents(BYTE mask)
        {
            UINT count = 0;
            for (BYTE bit = 1; bit != 0; bit <<= 1)
            {
                if (mask & bit)
                {
                    ++count;
                }
            }
            return count > 0 ? count : 1;
        }

        DXGI_FORMAT SignatureFormat(D3D_REGISTER_COMPONENT_TYPE componentType, UINT components)
        {
            switch (componentType)
            {
            case D3D_REGISTER_COMPONENT_SINT32:
                return components >= 4   ? DXGI_FORMAT_R32G32B32A32_SINT
                       : components == 3 ? DXGI_FORMAT_R32G32B32_SINT
                       : components == 2 ? DXGI_FORMAT_R32G32_SINT
                                         : DXGI_FORMAT_R32_SINT;
            case D3D_REGISTER_COMPONENT_UINT32:
                return components >= 4   ? DXGI_FORMAT_R32G32B32A32_UINT
                       : components == 3 ? DXGI_FORMAT_R32G32B32_UINT
                       : components == 2 ? DXGI_FORMAT_R32G32_UINT
                                         : DXGI_FORMAT_R32_UINT;
            case D3D_REGISTER_COMPONENT_FLOAT32:
            default:
                return components >= 4   ? DXGI_FORMAT_R32G32B32A32_FLOAT
                       : components == 3 ? DXGI_FORMAT_R32G32B32_FLOAT
                       : components == 2 ? DXGI_FORMAT_R32G32_FLOAT
                                         : DXGI_FORMAT_R32_FLOAT;
            }
        }

        bool SemanticEquals(const char* pSemantic, const char* pName)
        {
            return pSemantic != nullptr && _stricmp(pSemantic, pName) == 0;
        }

        void DrawThickLine(D3D11CommandContext& commands, Vec2 start, Vec2 end, float lineWidth, Color color,
                           int screenWidth, int screenHeight)
        {
            const float deltaX = end.x - start.x;
            const float deltaY = end.y - start.y;
            const float length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
            const float halfWidth = std::max(lineWidth, 1.0f) * 0.5f;

            if (length <= 0.0f)
            {
                return;
            }

            const Vec2 normal{-deltaY / length * halfWidth, deltaX / length * halfWidth};
            const Vec2 points[] = {{start.x + normal.x, start.y + normal.y},
                                   {end.x + normal.x, end.y + normal.y},
                                   {end.x - normal.x, end.y - normal.y},
                                   {start.x - normal.x, start.y - normal.y}};

            commands.DrawTriangle(points[0], points[1], points[2], color, screenWidth, screenHeight);
            commands.DrawTriangle(points[0], points[2], points[3], color, screenWidth, screenHeight);
        }
    } // namespace

    QuarkD3D11Renderer::~QuarkD3D11Renderer()

    {
        Shutdown();
    }

    void QuarkD3D11Renderer::Init(SDL_Window* pWindow, int width, int height)
    {
        TraceLog(LogLevel::Info, "D3D11", TextFormat("Initializing renderer (Window: %dx%d)", width, height));
        m_pWindow = pWindow;
        SDL_GetWindowSizeInPixels(pWindow, &m_Width, &m_Height);
        if (m_Width <= 0 || m_Height <= 0)
        {
            m_Width = width;
            m_Height = height;
        }
        m_LastFrame = std::chrono::steady_clock::now();

        m_Device.Initialize();
        m_Resources.Initialize(m_Device.Get());
        m_SwapChain.Initialize(m_Device, pWindow, m_Width, m_Height);
        m_Pipeline.Initialize(m_Device.Get(), m_ShaderCompiler, m_Resources);
        m_ThreadPool.Initialize();
        m_Commands.Initialize(m_Device, m_SwapChain, m_Pipeline, m_Resources, m_Width, m_Height, &m_ThreadPool);
        {
            const uint8_t white[4] = {255, 255, 255, 255};
            m_WhiteShaderTexture = m_Resources.CreateTexture(m_Device.Get(), white, 1, 1);
        }
        {
            const uint8_t black[4] = {0, 0, 0, 255};
            m_BlackShaderTexture = m_Resources.CreateTexture(m_Device.Get(), black, 1, 1);
        }
        {
            const uint8_t flatNormal[4] = {128, 128, 255, 255};
            m_FlatNormalShaderTexture = m_Resources.CreateTexture(m_Device.Get(), flatNormal, 1, 1);
        }
        if (m_RequestedMsaaSamples > 1)
        {
            m_SwapChain.SetMSAASamples(static_cast<UINT>(m_RequestedMsaaSamples));
        }
        m_TextureFilterMode = g_TextureFilter;
        m_Pipeline.SetTextureFilterMode(m_TextureFilterMode);
        RefreshViewport();
        TraceLog(LogLevel::Info, "D3D11", "Renderer initialized successfully.");
    }

    void QuarkD3D11Renderer::Shutdown()
    {
        if (m_pWindow == nullptr)
            return;

        TraceLog(LogLevel::Info, "D3D11", "Shutting down D3D11 renderer...");

        m_CurrentShaderId = 0;
        m_StaticGpuMeshes.clear();
        m_vStaticMeshKeysByVertices.clear();
        m_BlendStateCache.clear();
        m_ShaderPrograms.clear();
        if (m_WhiteShaderTexture.IsValid())
        {
            m_Resources.DestroyTexture(m_WhiteShaderTexture.m_Id);
            m_WhiteShaderTexture = {};
        }
        if (m_BlackShaderTexture.IsValid())
        {
            m_Resources.DestroyTexture(m_BlackShaderTexture.m_Id);
            m_BlackShaderTexture = {};
        }
        if (m_FlatNormalShaderTexture.IsValid())
        {
            m_Resources.DestroyTexture(m_FlatNormalShaderTexture.m_Id);
            m_FlatNormalShaderTexture = {};
        }
        for (const auto& [_, cachedTexture] : m_TextureCache)
        {
            if (cachedTexture.m_Texture.IsValid())
            {
                m_Resources.DestroyTexture(cachedTexture.m_Texture.m_Id);
            }
        }
        m_TextureCache.clear();
        m_TextureCacheKeys.clear();
        m_Commands.Shutdown();
        m_ThreadPool.Shutdown();
        m_Pipeline.Shutdown();
        m_Resources.Shutdown();
        m_SwapChain.Shutdown();
        m_Device.Shutdown();
        m_pWindow = nullptr;
        m_Drawing = false;

        TraceLog(LogLevel::Info, "D3D11", "D3D11 renderer shut down successfully.");
    }

    void QuarkD3D11Renderer::SetMSAASamples(int samples)
    {
        m_RequestedMsaaSamples = (samples == 2 || samples == 4 || samples == 8) ? samples : 1;
        if (m_SwapChain.Get() != nullptr)
        {
            m_SwapChain.SetMSAASamples(static_cast<UINT>(m_RequestedMsaaSamples));
        }
    }

    void QuarkD3D11Renderer::SetTextureFilterMode(TextureFilter mode)
    {
        m_TextureFilterMode = mode;
        m_Pipeline.SetTextureFilterMode(mode);
    }

    void QuarkD3D11Renderer::SetTextureFilter(TextureFilter filter)
    {
        SetTextureFilterMode(filter);
    }

    void QuarkD3D11Renderer::SetTextureWrap(int wrap)
    {
        m_Pipeline.SetTextureWrapMode(wrap);
    }

    void QuarkD3D11Renderer::BeginScissorMode(int x, int y, int width, int height)
    {
        m_Commands.FlushBatch();
        D3D11_RECT scissorRect{};
        scissorRect.left = static_cast<LONG>(x);
        scissorRect.top = static_cast<LONG>(y);
        scissorRect.right = static_cast<LONG>(x + width);
        scissorRect.bottom = static_cast<LONG>(y + height);
        m_Device.Context()->RSSetScissorRects(1, &scissorRect);
    }

    void QuarkD3D11Renderer::EndScissorMode()
    {
        m_Commands.FlushBatch();
        D3D11_RECT scissorRect{};
        scissorRect.left = 0;
        scissorRect.top = 0;
        scissorRect.right = static_cast<LONG>(m_Width);
        scissorRect.bottom = static_cast<LONG>(m_Height);
        m_Device.Context()->RSSetScissorRects(1, &scissorRect);
    }

    void QuarkD3D11Renderer::SetBlendMode(int mode)
    {
        if (!m_Device.Get())
        {
            return;
        }

        m_Commands.FlushBatch();

        const float blendFactor[] = {0.0f, 0.0f, 0.0f, 0.0f};
        ID3D11BlendState* pBlendState = nullptr;
        switch (mode)
        {
        case BLEND_ADDITIVE:
        case BLEND_ADD_COLORS:
            pBlendState = nullptr;
            break;
        case BLEND_MULTIPLIED:
        case BLEND_MOD_COLOR:
            pBlendState = nullptr;
            break;
        case BLEND_SUBTRACT_COLORS:
            pBlendState = nullptr;
            break;
        case BLEND_ALPHA:
        default:
            pBlendState = m_Pipeline.Blend();
            break;
        }

        if (pBlendState == nullptr)
        {
            D3D11_BLEND_DESC desc{};
            desc.AlphaToCoverageEnable = FALSE;
            desc.IndependentBlendEnable = FALSE;
            desc.RenderTarget[0].BlendEnable = TRUE;
            desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

            switch (mode)
            {
            case BLEND_ADDITIVE:
            case BLEND_ADD_COLORS:
                desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
                desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
                desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
                desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
                desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
                desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
                break;
            case BLEND_MULTIPLIED:
            case BLEND_MOD_COLOR:
                desc.RenderTarget[0].SrcBlend = D3D11_BLEND_DEST_COLOR;
                desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
                desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
                desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
                desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
                desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
                break;
            case BLEND_SUBTRACT_COLORS:
                desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
                desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
                desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_REV_SUBTRACT;
                desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
                desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
                desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_REV_SUBTRACT;
                break;
            case BLEND_ALPHA:
            default:
                desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
                desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
                desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
                desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
                desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
                desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
                break;
            }

            auto cachedIt = m_BlendStateCache.find(mode);
            if (cachedIt != m_BlendStateCache.end() && cachedIt->second)
            {
                m_Commands.SetBlendState(cachedIt->second.Get());
                m_Device.Context()->OMSetBlendState(cachedIt->second.Get(), blendFactor, 0xFFFFFFFF);
                return;
            }

            Microsoft::WRL::ComPtr<ID3D11BlendState> customBlend;
            HRESULT hr = m_Device.Get()->CreateBlendState(&desc, customBlend.GetAddressOf());
            if (SUCCEEDED(hr))
            {
                m_BlendStateCache[mode] = customBlend;
                m_Commands.SetBlendState(customBlend.Get());
                m_Device.Context()->OMSetBlendState(customBlend.Get(), blendFactor, 0xFFFFFFFF);
            }
            return;
        }

        m_Commands.SetBlendState(pBlendState);
        m_Device.Context()->OMSetBlendState(pBlendState, blendFactor, 0xFFFFFFFF);
    }

    void QuarkD3D11Renderer::Set3DLightEnabled(int index, bool enabled)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= m_aLights.size())
        {
            return;
        }

        m_aLights[static_cast<std::size_t>(index)].m_Enabled = enabled;
        TraceLog(LogLevel::Trace, "D3D11", TextFormat("3D light %d %s.", index, enabled ? "enabled" : "disabled"));
    }

    void QuarkD3D11Renderer::RefreshViewport()
    {
        if (m_pWindow)
        {
            int pixelWidth = 0;
            int pixelHeight = 0;
            SDL_GetWindowSizeInPixels(m_pWindow, &pixelWidth, &pixelHeight);

            if (pixelWidth > 0 && pixelHeight > 0 && (pixelWidth != m_Width || pixelHeight != m_Height))
            {
                m_WindowResized = true;
            }
        }

        m_Commands.SetDefaultViewportSize(m_Width, m_Height);
        m_Commands.RefreshViewport(m_Width, m_Height);
    }

    void QuarkD3D11Renderer::ResizeSwapChain()
    {
        if (!m_pWindow)
        {
            return;
        }

        int pixelWidth = 0;
        int pixelHeight = 0;
        SDL_GetWindowSizeInPixels(m_pWindow, &pixelWidth, &pixelHeight);

        if (pixelWidth <= 0 || pixelHeight <= 0)
        {
            return;
        }

        m_Commands.ReleaseRenderTargets();
        m_SwapChain.Resize(m_Device, pixelWidth, pixelHeight);
        m_Width = pixelWidth;
        m_Height = pixelHeight;
        m_Commands.SetDefaultViewportSize(m_Width, m_Height);
        m_Commands.RefreshViewport(m_Width, m_Height);
    }

    void QuarkD3D11Renderer::BeginDrawing()
    {
        const auto now = std::chrono::steady_clock::now();
        m_FrameTime = std::chrono::duration<float>(now - m_LastFrame).count();
        m_LastFrame = now;
        m_Drawing = true;
        m_Commands.BeginDrawing();

        D3D11LightConstantData lightData{};
        const float ambient[3] = {0.1f, 0.1f, 0.1f};
        lightData.SetAmbient(ambient);
        lightData.m_aViewPosition[0] = m_ViewPos.x;
        lightData.m_aViewPosition[1] = m_ViewPos.y;
        lightData.m_aViewPosition[2] = m_ViewPos.z;
        lightData.m_aViewPosition[3] = 1.0f;

        for (std::size_t i = 0; i < m_aLights.size(); ++i)
        {
            const Light3D& light = m_aLights[i];
            lightData.m_aLightPositions[i][0] = light.m_Position.x;
            lightData.m_aLightPositions[i][1] = light.m_Position.y;
            lightData.m_aLightPositions[i][2] = light.m_Position.z;
            lightData.m_aLightPositions[i][3] = 1.0f;
            lightData.m_aLightColors[i][0] = light.m_Color.x;
            lightData.m_aLightColors[i][1] = light.m_Color.y;
            lightData.m_aLightColors[i][2] = light.m_Color.z;
            lightData.m_aLightColors[i][3] = 1.0f;
            lightData.m_aLightParams[i][0] = light.m_Attenuation;
            lightData.m_aLightParams[i][1] = light.m_Enabled ? 1.0f : 0.0f;
            lightData.m_aLightParams[i][2] = static_cast<float>(light.m_Type);
            lightData.m_aLightParams[i][3] = 0.0f;
        }

        m_Pipeline.UpdateLights(m_Device.Context(), lightData);
    }

    void QuarkD3D11Renderer::EndDrawing()
    {
        m_Commands.FlushBatch();

        if (const D3D11RenderCallback callback = GetD3D11RenderCallback())
        {
            callback(m_Device.Context());
        }

        m_Commands.FlushDebugOverlay();

        const bool vsync = m_VsyncExplicitlySet ? m_Vsync : (m_TargetFps != 0);
        m_Commands.Present(vsync);

        if (m_WindowResized)
        {
            m_WindowResized = false;
            ResizeSwapChain();
        }

        const std::uint64_t freq = SDL_GetPerformanceFrequency();
        if (m_TargetFps > 0 && !vsync)
        {
            const std::uint64_t targetTicks = freq / static_cast<std::uint64_t>(m_TargetFps);
            while (true)
            {
                const std::uint64_t now = SDL_GetPerformanceCounter();
                const std::uint64_t elapsed = now - m_LastFrameCounter;
                if (elapsed >= targetTicks)
                {
                    break;
                }
                const std::uint64_t remaining = targetTicks - elapsed;
                if (remaining > freq / 500)
                {
                    SDL_Delay(1);
                }
            }
        }

        m_LastFrameCounter = SDL_GetPerformanceCounter();
        m_Drawing = false;
    }

    void QuarkD3D11Renderer::ClearBackground(Color color)
    {
        m_Commands.Clear(color);
    }

    void QuarkD3D11Renderer::DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color)
    {
        m_Commands.DrawTriangle(v1, v2, v3, color, m_Width, m_Height);
    }

    void QuarkD3D11Renderer::DrawRectangle(float x, float y, float width, float height, Color color)
    {
        DrawRectangle(Rectangle{x, y, width, height}, color);
    }

    void QuarkD3D11Renderer::DrawRectangle(const Rectangle& rectangle, Color color)
    {
        DrawRectangleV(Vec2{rectangle.x, rectangle.y}, Vec2{rectangle.width, rectangle.height}, color);
    }

    void QuarkD3D11Renderer::DrawRectangleV(Vec2 position, Vec2 size, Color color)
    {
        const Vec2 topLeft{position.x, position.y};
        const Vec2 topRight{position.x + size.x, position.y};
        const Vec2 bottomRight{position.x + size.x, position.y + size.y};
        const Vec2 bottomLeft{position.x, position.y + size.y};

        m_Commands.DrawTriangle(topLeft, topRight, bottomRight, color, m_Width, m_Height);
        m_Commands.DrawTriangle(topLeft, bottomRight, bottomLeft, color, m_Width, m_Height);
    }

    void QuarkD3D11Renderer::DrawRectangleLines(Rectangle rectangle, float lineWidth, Color color)
    {
        const Vec2 topLeft{rectangle.x, rectangle.y};
        const Vec2 topRight{rectangle.x + rectangle.width, rectangle.y};
        const Vec2 bottomRight{rectangle.x + rectangle.width, rectangle.y + rectangle.height};
        const Vec2 bottomLeft{rectangle.x, rectangle.y + rectangle.height};

        DrawThickLine(m_Commands, topLeft, topRight, lineWidth, color, m_Width, m_Height);
        DrawThickLine(m_Commands, topRight, bottomRight, lineWidth, color, m_Width, m_Height);
        DrawThickLine(m_Commands, bottomRight, bottomLeft, lineWidth, color, m_Width, m_Height);
        DrawThickLine(m_Commands, bottomLeft, topLeft, lineWidth, color, m_Width, m_Height);
    }

    void QuarkD3D11Renderer::DrawRectangleRounded(Rectangle rectangle, float roundness, int segments, Color color)
    {
        (void)segments;
        const float shortestSide = std::min(std::abs(rectangle.width), std::abs(rectangle.height));
        const float radius = std::clamp(roundness * shortestSide * 0.5f, 0.0f, shortestSide * 0.5f);

        if (radius <= 0.0f)
        {
            DrawRectangle(rectangle, color);
            return;
        }

        const float x = rectangle.x;
        const float y = rectangle.y;
        const float w = rectangle.width;
        const float h = rectangle.height;

        DrawCircle(x + radius, y + radius, radius, color);
        DrawCircle(x + w - radius, y + radius, radius, color);
        DrawCircle(x + w - radius, y + h - radius, radius, color);
        DrawCircle(x + radius, y + h - radius, radius, color);

        DrawRectangle(Rectangle{x + radius, y, w - 2.0f * radius, h}, color);
        DrawRectangle(Rectangle{x, y + radius, w, h - 2.0f * radius}, color);
    }

    void QuarkD3D11Renderer::DrawCircle(float centerX, float centerY, float radius, Color color)
    {
        DrawEllipse(centerX, centerY, radius, radius, color);
    }

    void QuarkD3D11Renderer::DrawCircleLines(float centerX, float centerY, float radius, Color color)
    {
        constexpr int segments = 32;
        Vec2 previous{centerX + radius, centerY};

        for (int index = 1; index <= segments; ++index)
        {
            const float angle = 2.0f * PI * index / segments;
            const Vec2 current{centerX + std::cos(angle) * radius, centerY + std::sin(angle) * radius};
            DrawThickLine(m_Commands, previous, current, 1.0f, color, m_Width, m_Height);
            previous = current;
        }
    }

    void QuarkD3D11Renderer::DrawEllipse(float centerX, float centerY, float radiusH, float radiusV, Color color)
    {
        constexpr int segments = 32;
        const Vec2 center{centerX, centerY};

        for (int index = 0; index < segments; ++index)
        {
            const float firstAngle = 2.0f * PI * index / segments;
            const float secondAngle = 2.0f * PI * (index + 1) / segments;
            const Vec2 first{centerX + std::cos(firstAngle) * radiusH, centerY + std::sin(firstAngle) * radiusV};
            const Vec2 second{centerX + std::cos(secondAngle) * radiusH, centerY + std::sin(secondAngle) * radiusV};

            m_Commands.DrawTriangle(center, first, second, color, m_Width, m_Height);
        }
    }

    void QuarkD3D11Renderer::DrawLine(float x1, float y1, float x2, float y2, Color color)
    {
        DrawLineV(Vec2{x1, y1}, Vec2{x2, y2}, color);
    }

    void QuarkD3D11Renderer::DrawLineV(Vec2 start, Vec2 end, Color color)
    {
        DrawThickLine(m_Commands, start, end, 1.0f, color, m_Width, m_Height);
    }

    void QuarkD3D11Renderer::DrawPoly(Vec2 center, int sides, float radius, float rotation, Color color)
    {
        if (sides < 3 || radius <= 0.0f)
        {
            return;
        }

        const float rotationRadians = rotation * PI / 180.0f;

        for (int index = 0; index < sides; ++index)
        {
            const float firstAngle = rotationRadians + 2.0f * PI * index / sides;
            const float secondAngle = rotationRadians + 2.0f * PI * (index + 1) / sides;
            const Vec2 first{center.x + std::cos(firstAngle) * radius, center.y + std::sin(firstAngle) * radius};
            const Vec2 second{center.x + std::cos(secondAngle) * radius, center.y + std::sin(secondAngle) * radius};

            m_Commands.DrawTriangle(center, first, second, color, m_Width, m_Height);
        }
    }

    void QuarkD3D11Renderer::BeginMode3D(const Camera3D& camera)
    {
        m_ViewMatrix = Mat4::lookAt(camera.position, camera.target, camera.up);
        m_ViewPos = camera.position;

        const int activeWidth = m_Commands.ActiveWidth() > 0 ? m_Commands.ActiveWidth() : m_Width;
        const int activeHeight = m_Commands.ActiveHeight() > 0 ? m_Commands.ActiveHeight() : m_Height;

        if (camera.projection == CAMERA_PERSPECTIVE)
        {
            const float aspect =
                activeHeight > 0 ? static_cast<float>(activeWidth) / static_cast<float>(activeHeight) : 1.0f;
            if (camera.fovy > 0.0f && camera.fovy < 180.0f)
            {
                m_ProjectionMatrix = Mat4::perspective(camera.fovy * PI / 180.0f, aspect, 0.1f, 1000.0f);
            }
            else
            {
                m_ProjectionMatrix = Mat4::perspective(45.0f * PI / 180.0f, aspect, 0.1f, 1000.0f);
            }
        }
        else
        {
            m_ProjectionMatrix = Mat4::ortho(
                static_cast<float>(-activeWidth) * 0.5f, static_cast<float>(activeWidth) * 0.5f,
                static_cast<float>(-activeHeight) * 0.5f, static_cast<float>(activeHeight) * 0.5f, 0.1f, 1000.0f);
        }
    }

    void QuarkD3D11Renderer::EndMode3D()
    {
        m_vMatrixStack.clear();
        m_CurrentMatrix = Mat4::identity();
    }

    void QuarkD3D11Renderer::PushMatrix()
    {
        m_vMatrixStack.push_back(m_CurrentMatrix);
    }

    void QuarkD3D11Renderer::PopMatrix()
    {
        if (!m_vMatrixStack.empty())
        {
            m_CurrentMatrix = m_vMatrixStack.back();
            m_vMatrixStack.pop_back();
        }
        else
        {
            m_CurrentMatrix = Mat4::identity();
        }
    }

    void QuarkD3D11Renderer::Translate(const Vec3& translation)
    {
        m_CurrentMatrix = m_CurrentMatrix * Mat4::translation(translation.x, translation.y, translation.z);
    }

    void QuarkD3D11Renderer::Rotate(float angle, const Vec3& axis)
    {
        Vec3 normalized = axis;
        const float length = normalized.length();
        if (length <= 0.0f)
        {
            return;
        }
        normalized = normalized * (1.0f / length);

        const float c = std::cos(angle);
        const float s = std::sin(angle);
        const float t = 1.0f - c;

        Mat4 rotation = Mat4::identity();
        rotation.m[0] = c + normalized.x * normalized.x * t;
        rotation.m[1] = normalized.x * normalized.y * t + normalized.z * s;
        rotation.m[2] = normalized.x * normalized.z * t - normalized.y * s;
        rotation.m[4] = normalized.y * normalized.x * t - normalized.z * s;
        rotation.m[5] = c + normalized.y * normalized.y * t;
        rotation.m[6] = normalized.y * normalized.z * t + normalized.x * s;
        rotation.m[8] = normalized.z * normalized.x * t + normalized.y * s;
        rotation.m[9] = normalized.z * normalized.y * t - normalized.x * s;
        rotation.m[10] = c + normalized.z * normalized.z * t;

        m_CurrentMatrix = m_CurrentMatrix * rotation;
    }

    void QuarkD3D11Renderer::Scale(const Vec3& scale)
    {
        m_CurrentMatrix = m_CurrentMatrix * Mat4::scale(scale.x, scale.y, scale.z);
    }

    void QuarkD3D11Renderer::MultMatrix(const Mat4& matrix)
    {
        m_CurrentMatrix = m_CurrentMatrix * matrix;
    }

    const float* QuarkD3D11Renderer::GetMatrixModelview()
    {
        m_ModelviewCapture = m_ViewMatrix * m_CurrentMatrix;
        return m_ModelviewCapture.m;
    }

    const float* QuarkD3D11Renderer::GetMatrixProjection()
    {
        return m_ProjectionMatrix.m;
    }

    void QuarkD3D11Renderer::EnableBackfaceCulling()
    {
        m_Pipeline.SetBackfaceCulling(true);
    }

    void QuarkD3D11Renderer::DisableBackfaceCulling()
    {
        m_Pipeline.SetBackfaceCulling(false);
    }

    Mat4 QuarkD3D11Renderer::CurrentMVP() const
    {
        return m_ProjectionMatrix * m_ViewMatrix * m_CurrentMatrix;
    }

    namespace
    {
        Vec3 SafeNormalized(const Vec3& value, const Vec3& fallback)
        {
            const float length = value.length();
            if (length <= 1e-6f)
            {
                return fallback;
            }
            return value.normalized();
        }
    } // namespace

    void QuarkD3D11Renderer::DrawTris3D(const Vec3* pPositions, const Vec3* pNormals, size_t vertexCount,
                                        const Mat4& mvp, Color color)
    {
        if (!pPositions || vertexCount == 0 || vertexCount % 3 != 0)
        {
            return;
        }

        const size_t sliceOffset = m_Commands.Allocate3DVertexSlice(static_cast<UINT>(vertexCount), 16);
        float* pVertices = m_Commands.Get3DVertexPtr(sliceOffset);
        const float r = color.r / 255.0f;
        const float g = color.g / 255.0f;
        const float b = color.b / 255.0f;
        const float a = color.a / 255.0f;

        m_ThreadPool.ParallelFor(vertexCount, 256,
                                 [&](uint32_t, size_t begin, size_t end)
                                 {
                                     for (size_t index = begin; index < end; ++index)
                                     {
                                         const Vec4 clip = mvp * Vec4{pPositions[index].x, pPositions[index].y,
                                                                      pPositions[index].z, 1.0f};
                                         float* pVertex = pVertices + index * 16;
                                         pVertex[0] = clip.x;
                                         pVertex[1] = clip.y;
                                         pVertex[2] = clip.z;
                                         pVertex[3] = clip.w;
                                         pVertex[4] = r;
                                         pVertex[5] = g;
                                         pVertex[6] = b;
                                         pVertex[7] = a;
                                         pVertex[8] = pPositions[index].x;
                                         pVertex[9] = pPositions[index].y;
                                         pVertex[10] = pPositions[index].z;
                                         pVertex[11] = 1.0f;

                                         if (pNormals)
                                         {
                                             pVertex[12] = pNormals[index].x;
                                             pVertex[13] = pNormals[index].y;
                                             pVertex[14] = pNormals[index].z;
                                             pVertex[15] = 1.0f;
                                         }
                                         else
                                         {
                                             pVertex[12] = 0.0f;
                                             pVertex[13] = 0.0f;
                                             pVertex[14] = 0.0f;
                                             pVertex[15] = 0.0f;
                                         }
                                     }
                                 });

        m_Commands.SubmitQueued3D(sliceOffset, static_cast<UINT>(vertexCount), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    void QuarkD3D11Renderer::DrawLines3D(const Vec3* pPositions, size_t vertexCount, const Mat4& mvp, Color color)
    {
        if (!pPositions || vertexCount == 0 || vertexCount % 2 != 0)
        {
            return;
        }

        const size_t sliceOffset = m_Commands.Allocate3DVertexSlice(static_cast<UINT>(vertexCount), 16);
        float* pVertices = m_Commands.Get3DVertexPtr(sliceOffset);
        const float r = color.r / 255.0f;
        const float g = color.g / 255.0f;
        const float b = color.b / 255.0f;
        const float a = color.a / 255.0f;

        m_ThreadPool.ParallelFor(vertexCount, 256,
                                 [&](uint32_t, size_t begin, size_t end)
                                 {
                                     for (size_t index = begin; index < end; ++index)
                                     {
                                         const Vec4 clip = mvp * Vec4{pPositions[index].x, pPositions[index].y,
                                                                      pPositions[index].z, 1.0f};
                                         float* vertex = pVertices + index * 16;
                                         vertex[0] = clip.x;
                                         vertex[1] = clip.y;
                                         vertex[2] = clip.z;
                                         vertex[3] = clip.w;
                                         vertex[4] = r;
                                         vertex[5] = g;
                                         vertex[6] = b;
                                         vertex[7] = a;
                                         vertex[8] = 0.0f;
                                         vertex[9] = 0.0f;
                                         vertex[10] = 0.0f;
                                         vertex[11] = 0.0f;
                                         vertex[12] = 0.0f;
                                         vertex[13] = 0.0f;
                                         vertex[14] = 0.0f;
                                         vertex[15] = 0.0f;
                                     }
                                 });

        m_Commands.SubmitQueued3D(sliceOffset, static_cast<UINT>(vertexCount), D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    }

    void QuarkD3D11Renderer::Set3DView(const Mat4& view, const Mat4& projection)
    {
        m_ViewMatrix = view;
        m_ProjectionMatrix = projection;
    }

    void QuarkD3D11Renderer::DrawLine3D(Vec3 startPos, Vec3 endPos, Color color)
    {
        const Vec3 positions[2] = {startPos, endPos};
        const size_t sliceOffset = m_Commands.Allocate3DVertexSlice(2, 16);
        float* pVertices = m_Commands.Get3DVertexPtr(sliceOffset);
        const Mat4 mvp = CurrentMVP();
        const float r = color.r / 255.0f;
        const float g = color.g / 255.0f;
        const float b = color.b / 255.0f;
        const float a = color.a / 255.0f;
        for (size_t index = 0; index < 2; ++index)
        {
            const Vec4 clip = mvp * Vec4{positions[index].x, positions[index].y, positions[index].z, 1.0f};
            float* pVertex = pVertices + index * 16;
            pVertex[0] = clip.x;
            pVertex[1] = clip.y;
            pVertex[2] = clip.z;
            pVertex[3] = clip.w;
            pVertex[4] = r;
            pVertex[5] = g;
            pVertex[6] = b;
            pVertex[7] = a;
            pVertex[8] = 0.0f;
            pVertex[9] = 0.0f;
            pVertex[10] = 0.0f;
            pVertex[11] = 0.0f;
            pVertex[12] = 0.0f;
            pVertex[13] = 0.0f;
            pVertex[14] = 0.0f;
            pVertex[15] = 0.0f;
        }
        m_Commands.SubmitQueued3D(sliceOffset, 2, D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    }

    void QuarkD3D11Renderer::DrawPlane(Vec3 center, Vec2 size, Color color)
    {
        const Vec3 positions[6] = {
            center + Vec3{-size.x * 0.5f, 0.0f, -size.y * 0.5f}, center + Vec3{size.x * 0.5f, 0.0f, -size.y * 0.5f},
            center + Vec3{size.x * 0.5f, 0.0f, size.y * 0.5f},   center + Vec3{-size.x * 0.5f, 0.0f, -size.y * 0.5f},
            center + Vec3{size.x * 0.5f, 0.0f, size.y * 0.5f},   center + Vec3{-size.x * 0.5f, 0.0f, size.y * 0.5f}};

        const Vec3 normal{0.0f, 1.0f, 0.0f};
        const Vec3 normals[6] = {normal, normal, normal, normal, normal, normal};

        DrawTris3D(positions, normals, 6, CurrentMVP(), color);
    }

    void QuarkD3D11Renderer::DrawCube(Vec3 position, float width, float height, float length, Color color)
    {
        const float hw = width * 0.5f;
        const float hh = height * 0.5f;
        const float hl = length * 0.5f;

        const Vec3 v[8] = {position + Vec3{-hw, -hh, -hl}, position + Vec3{hw, -hh, -hl}, position + Vec3{hw, hh, -hl},
                           position + Vec3{-hw, hh, -hl},  position + Vec3{-hw, -hh, hl}, position + Vec3{hw, -hh, hl},
                           position + Vec3{hw, hh, hl},    position + Vec3{-hw, hh, hl}};

        const Vec3 positions[36] = {v[0], v[1], v[2], v[0], v[2], v[3],

                                    v[4], v[6], v[5], v[4], v[7], v[6],

                                    v[4], v[5], v[1], v[4], v[1], v[0],

                                    v[3], v[2], v[6], v[3], v[6], v[7],

                                    v[1], v[5], v[6], v[1], v[6], v[2],

                                    v[4], v[0], v[3], v[4], v[3], v[7]};

        const Vec3 nZ{0.0f, 0.0f, -1.0f};
        const Vec3 pZ{0.0f, 0.0f, 1.0f};
        const Vec3 nX{-1.0f, 0.0f, 0.0f};
        const Vec3 pX{1.0f, 0.0f, 0.0f};
        const Vec3 pY{0.0f, 1.0f, 0.0f};
        const Vec3 nY{0.0f, -1.0f, 0.0f};

        const Vec3 normals[36] = {nZ, nZ, nZ, nZ, nZ, nZ,

                                  pZ, pZ, pZ, pZ, pZ, pZ,

                                  nX, nX, nX, nX, nX, nX,

                                  pX, pX, pX, pX, pX, pX,

                                  pY, pY, pY, pY, pY, pY,

                                  nY, nY, nY, nY, nY, nY};

        DrawTris3D(positions, normals, 36, CurrentMVP(), color);
    }

    void QuarkD3D11Renderer::DrawCubeV(Vec3 position, Vec3 size, Color color)
    {
        DrawCube(position, size.x, size.y, size.z, color);
    }

    void QuarkD3D11Renderer::DrawCubeWires(Vec3 position, float width, float height, float length, Color color)
    {
        const float hw = width * 0.5f;
        const float hh = height * 0.5f;
        const float hl = length * 0.5f;

        const Vec3 v[8] = {position + Vec3{-hw, -hh, -hl}, position + Vec3{hw, -hh, -hl}, position + Vec3{hw, hh, -hl},
                           position + Vec3{-hw, hh, -hl},  position + Vec3{-hw, -hh, hl}, position + Vec3{hw, -hh, hl},
                           position + Vec3{hw, hh, hl},    position + Vec3{-hw, hh, hl}};

        const Vec3 positions[24] = {v[0], v[1], v[1], v[2], v[2], v[3], v[3], v[0],

                                    v[4], v[5], v[5], v[6], v[6], v[7], v[7], v[4],

                                    v[0], v[4], v[1], v[5], v[2], v[6], v[3], v[7]};

        DrawLines3D(positions, 24, CurrentMVP(), color);
    }

    void QuarkD3D11Renderer::DrawCubeWiresV(Vec3 position, Vec3 size, Color color)
    {
        DrawCubeWires(position, size.x, size.y, size.z, color);
    }

    void QuarkD3D11Renderer::DrawSphere(Vec3 centerPos, float radius, Color color)
    {
        DrawSphereEx(centerPos, radius, 16, 16, color);
    }

    void QuarkD3D11Renderer::DrawSphereEx(Vec3 centerPos, float radius, int rings, int slices, Color color)
    {
        if (rings < 2 || slices < 3)
        {
            return;
        }

        std::vector<Vec3> vPositions;
        std::vector<Vec3> vNormals;
        vPositions.reserve(static_cast<size_t>(rings) * slices * 6);
        vNormals.reserve(static_cast<size_t>(rings) * slices * 6);

        for (int ri = 0; ri < rings; ++ri)
        {
            for (int si = 0; si < slices; ++si)
            {
                const float phi1 = PI * static_cast<float>(ri) / static_cast<float>(rings);
                const float phi2 = PI * static_cast<float>(ri + 1) / static_cast<float>(rings);
                const float theta1 = 2.0f * PI * static_cast<float>(si) / static_cast<float>(slices);
                const float theta2 = 2.0f * PI * static_cast<float>(si + 1) / static_cast<float>(slices);

                const Vec3 a{radius * std::sin(phi1) * std::cos(theta1), radius * std::cos(phi1),
                             radius * std::sin(phi1) * std::sin(theta1)};
                const Vec3 b{radius * std::sin(phi1) * std::cos(theta2), radius * std::cos(phi1),
                             radius * std::sin(phi1) * std::sin(theta2)};
                const Vec3 d{radius * std::sin(phi2) * std::cos(theta1), radius * std::cos(phi2),
                             radius * std::sin(phi2) * std::sin(theta1)};
                const Vec3 e{radius * std::sin(phi2) * std::cos(theta2), radius * std::cos(phi2),
                             radius * std::sin(phi2) * std::sin(theta2)};

                vPositions.push_back(centerPos + a);
                vPositions.push_back(centerPos + b);
                vPositions.push_back(centerPos + e);
                vPositions.push_back(centerPos + a);
                vPositions.push_back(centerPos + e);
                vPositions.push_back(centerPos + d);

                vNormals.push_back(a.normalized());
                vNormals.push_back(b.normalized());
                vNormals.push_back(e.normalized());
                vNormals.push_back(a.normalized());
                vNormals.push_back(e.normalized());
                vNormals.push_back(d.normalized());
            }
        }

        DrawTris3D(vPositions.data(), vNormals.data(), vPositions.size(), CurrentMVP(), color);
    }

    void QuarkD3D11Renderer::DrawSphereWires(Vec3 centerPos, float radius, int rings, int slices, Color color)
    {
        if (rings < 2 || slices < 3)
        {
            return;
        }

        std::vector<Vec3> vPositions;
        vPositions.reserve(static_cast<size_t>(rings + 1 + slices) * slices * 2);

        for (int ri = 0; ri <= rings; ++ri)
        {
            const float phi = PI * static_cast<float>(ri) / static_cast<float>(rings);
            for (int si = 0; si < slices; ++si)
            {
                const float t1 = 2.0f * PI * static_cast<float>(si) / static_cast<float>(slices);
                const float t2 = 2.0f * PI * static_cast<float>(si + 1) / static_cast<float>(slices);
                vPositions.push_back(centerPos + Vec3{radius * std::sin(phi) * std::cos(t1), radius * std::cos(phi),
                                                      radius * std::sin(phi) * std::sin(t1)});
                vPositions.push_back(centerPos + Vec3{radius * std::sin(phi) * std::cos(t2), radius * std::cos(phi),
                                                      radius * std::sin(phi) * std::sin(t2)});
            }
        }

        for (int si = 0; si < slices; ++si)
        {
            const float th = 2.0f * PI * static_cast<float>(si) / static_cast<float>(slices);
            for (int ri = 0; ri < rings; ++ri)
            {
                const float p1 = PI * static_cast<float>(ri) / static_cast<float>(rings);
                const float p2 = PI * static_cast<float>(ri + 1) / static_cast<float>(rings);
                vPositions.push_back(centerPos + Vec3{radius * std::sin(p1) * std::cos(th), radius * std::cos(p1),
                                                      radius * std::sin(p1) * std::sin(th)});
                vPositions.push_back(centerPos + Vec3{radius * std::sin(p2) * std::cos(th), radius * std::cos(p2),
                                                      radius * std::sin(p2) * std::sin(th)});
            }
        }

        DrawLines3D(vPositions.data(), vPositions.size(), CurrentMVP(), color);
    }

    void QuarkD3D11Renderer::DrawCylinder(Vec3 position, float radiusTop, float radiusBottom, float height, int slices,
                                          Color color)
    {
        DrawCylinderEx(position + Vec3{0, -height * 0.5f, 0}, position + Vec3{0, height * 0.5f, 0}, radiusBottom,
                       radiusTop, slices, color);
    }

    void QuarkD3D11Renderer::DrawCylinderEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius, int sides,
                                            Color color)
    {
        if (sides < 3)
        {
            return;
        }

        const Vec3 delta = endPos - startPos;
        const float length = delta.length();
        if (length <= 0.0f)
        {
            return;
        }
        const Vec3 dir = delta * (1.0f / length);

        Vec3 up{0, 1, 0};
        if (std::fabs(dir.dot(up)) > 0.99f)
        {
            up = {1, 0, 0};
        }

        const Vec3 xDir = dir.cross(up).normalized();
        const Vec3 yDir = dir.cross(xDir).normalized();

        std::vector<Vec3> vPositions;
        std::vector<Vec3> vNormals;
        vPositions.reserve(static_cast<size_t>(sides) * 6);
        vNormals.reserve(static_cast<size_t>(sides) * 6);

        for (int i = 0; i < sides; ++i)
        {
            const float a1 = 2.0f * PI * static_cast<float>(i) / static_cast<float>(sides);
            const float a2 = 2.0f * PI * static_cast<float>(i + 1) / static_cast<float>(sides);

            const Vec3 p1 = startPos + xDir * std::cos(a1) * startRadius + yDir * std::sin(a1) * startRadius;
            const Vec3 p2 = startPos + xDir * std::cos(a2) * startRadius + yDir * std::sin(a2) * startRadius;
            const Vec3 p3 = endPos + xDir * std::cos(a2) * endRadius + yDir * std::sin(a2) * endRadius;
            const Vec3 p4 = endPos + xDir * std::cos(a1) * endRadius + yDir * std::sin(a1) * endRadius;

            const Vec3 n1 = SafeNormalized(p1 - startPos, xDir);
            const Vec3 n2 = SafeNormalized(p2 - startPos, xDir);
            const Vec3 n3 = SafeNormalized(p3 - endPos, xDir);
            const Vec3 n4 = SafeNormalized(p4 - endPos, xDir);

            vPositions.push_back(p1);
            vPositions.push_back(p2);
            vPositions.push_back(p3);
            vPositions.push_back(p1);
            vPositions.push_back(p3);
            vPositions.push_back(p4);

            vNormals.push_back(n1);
            vNormals.push_back(n2);
            vNormals.push_back(n3);
            vNormals.push_back(n1);
            vNormals.push_back(n3);
            vNormals.push_back(n4);

            vPositions.push_back(startPos);
            vPositions.push_back(p2);
            vPositions.push_back(p1);
            vPositions.push_back(endPos);
            vPositions.push_back(p3);
            vPositions.push_back(p4);

            vNormals.push_back(Vec3{-dir.x, -dir.y, -dir.z});
            vNormals.push_back(Vec3{-dir.x, -dir.y, -dir.z});
            vNormals.push_back(Vec3{-dir.x, -dir.y, -dir.z});
            vNormals.push_back(dir);
            vNormals.push_back(dir);
            vNormals.push_back(dir);
        }

        DrawTris3D(vPositions.data(), vNormals.data(), vPositions.size(), CurrentMVP(), color);
    }

    void QuarkD3D11Renderer::DrawCylinderWires(Vec3 position, float radiusTop, float radiusBottom, float height,
                                               int slices, Color color)
    {
        DrawCylinderWiresEx(position + Vec3{0, -height * 0.5f, 0}, position + Vec3{0, height * 0.5f, 0}, radiusBottom,
                            radiusTop, slices, color);
    }

    void QuarkD3D11Renderer::DrawCylinderWiresEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius,
                                                 int slices, Color color)
    {
        if (slices < 3)
        {
            return;
        }

        const Vec3 delta = endPos - startPos;
        const float length = delta.length();
        if (length <= 0.0f)
        {
            return;
        }
        const Vec3 dir = delta * (1.0f / length);

        Vec3 up{0, 1, 0};
        if (std::fabs(dir.dot(up)) > 0.99f)
        {
            up = {1, 0, 0};
        }

        const Vec3 xDir = dir.cross(up).normalized();
        const Vec3 yDir = dir.cross(xDir).normalized();

        std::vector<Vec3> vPositions;
        vPositions.reserve(static_cast<size_t>(slices) * 6);

        for (int i = 0; i < slices; ++i)
        {
            const float a1 = 2.0f * PI * static_cast<float>(i) / static_cast<float>(slices);
            const float a2 = 2.0f * PI * static_cast<float>(i + 1) / static_cast<float>(slices);

            const Vec3 p1 = startPos + xDir * std::cos(a1) * startRadius + yDir * std::sin(a1) * startRadius;
            const Vec3 p2 = startPos + xDir * std::cos(a2) * startRadius + yDir * std::sin(a2) * startRadius;
            const Vec3 p3 = endPos + xDir * std::cos(a1) * endRadius + yDir * std::sin(a1) * endRadius;
            const Vec3 p4 = endPos + xDir * std::cos(a2) * endRadius + yDir * std::sin(a2) * endRadius;

            vPositions.push_back(p1);
            vPositions.push_back(p2);
            vPositions.push_back(p3);
            vPositions.push_back(p4);
            vPositions.push_back(p1);
            vPositions.push_back(p3);
        }

        DrawLines3D(vPositions.data(), vPositions.size(), CurrentMVP(), color);
    }

    void QuarkD3D11Renderer::DrawGrid(int slices, float spacing, Color color)
    {
        if (slices < 1 || spacing <= 0.0f)
        {
            return;
        }

        const float half = static_cast<float>(slices) * spacing * 0.5f;

        std::vector<Vec3> vPositions;
        vPositions.reserve(static_cast<size_t>(slices + 1) * 4);

        for (int i = 0; i <= slices; ++i)
        {
            const float f = -half + static_cast<float>(i) * spacing;
            vPositions.push_back({f, 0.01f, -half});
            vPositions.push_back({f, 0.01f, half});
            vPositions.push_back({-half, 0.01f, f});
            vPositions.push_back({half, 0.01f, f});
        }

        DrawLines3D(vPositions.data(), vPositions.size(), CurrentMVP(), color);
    }

    namespace
    {
        Color ReadMeshVertexColor(const Mesh& mesh, int index, Color fallback)
        {
            if (!mesh.colors || index < 0 || index * 4 + 3 >= mesh.vertexCount * 4)
            {
                return fallback;
            }

            return Color{mesh.colors[index * 4 + 0], mesh.colors[index * 4 + 1], mesh.colors[index * 4 + 2],
                         mesh.colors[index * 4 + 3]};
        }

        std::string GetModelDirectory(const char* pFilePath)
        {
            if (!pFilePath)
            {
                return {};
            }

            const std::string path(pFilePath);
            const std::size_t slash = path.find_last_of("/\\");
            if (slash == std::string::npos)
            {
                return {};
            }

            return path.substr(0, slash + 1);
        }
    } // namespace

    static void ensure_mesh_tangents(Mesh& mesh, bool forceRecompute = false);

    Model QuarkD3D11Renderer::LoadModel(const char* pFilePath)
    {
        TraceLog(LogLevel::Info, "MODEL", TextFormat("[D3D11] Loading 3D model: %s", pFilePath ? pFilePath : "<null>"));

        if (!pFilePath)
        {
            return {};
        }

        Assimp::Importer importer;
        const aiScene* pScene = nullptr;
        try
        {
            pScene = importer.ReadFile(pFilePath, aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs |
                                                      aiProcess_PopulateArmatureData);
        }
        catch (const std::exception& error)
        {
            TraceLog(LogLevel::Error, "MODEL",
                     TextFormat("[D3D11] Exception while loading model %s: %s", pFilePath, error.what()));
            return {};
        }

        if (!pScene || (pScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || !pScene->mRootNode)
        {
            TraceLog(LogLevel::Error, "MODEL",
                     TextFormat("[D3D11] Failed to load model %s: %s", pFilePath, importer.GetErrorString()));
            return {};
        }

        Model model{};
        model.directory = GetModelDirectory(pFilePath);
        model.meshCount = static_cast<int>(pScene->mNumMeshes);
        model.materialCount = static_cast<int>(pScene->mNumMaterials);
        model.meshes = (model.meshCount > 0) ? new Mesh[model.meshCount]{} : nullptr;
        model.materials = (model.materialCount > 0) ? new Material[model.materialCount]{} : nullptr;
        model.meshMaterial = (model.meshCount > 0) ? new int[model.meshCount]{} : nullptr;

        for (int materialIndex = 0; materialIndex < model.materialCount; ++materialIndex)
        {
            Material& material = model.materials[materialIndex];
            material.maps = new MaterialMap[MATERIAL_MAP_BRDF + 1]{};
            material.maps[MATERIAL_MAP_ALBEDO].color = WHITE;

            aiMaterial* pSourceMaterial = pScene->mMaterials[materialIndex];
            aiColor4D baseColor{};
            if (AI_SUCCESS == aiGetMaterialColor(pSourceMaterial, AI_MATKEY_BASE_COLOR, &baseColor) ||
                AI_SUCCESS == aiGetMaterialColor(pSourceMaterial, AI_MATKEY_COLOR_DIFFUSE, &baseColor))
            {
                material.maps[MATERIAL_MAP_ALBEDO].color =
                    Color{static_cast<unsigned char>(std::clamp(baseColor.r * 255.0f, 0.0f, 255.0f)),
                          static_cast<unsigned char>(std::clamp(baseColor.g * 255.0f, 0.0f, 255.0f)),
                          static_cast<unsigned char>(std::clamp(baseColor.b * 255.0f, 0.0f, 255.0f)),
                          static_cast<unsigned char>(std::clamp(baseColor.a * 255.0f, 0.0f, 255.0f))};
            }

            const std::array<std::pair<int, aiTextureType>, 7> textureTypes = {
                {{MATERIAL_MAP_ALBEDO, aiTextureType_BASE_COLOR},
                 {MATERIAL_MAP_ALBEDO, aiTextureType_DIFFUSE},
                 {MATERIAL_MAP_METALNESS, aiTextureType_METALNESS},
                 {MATERIAL_MAP_NORMAL, aiTextureType_NORMALS},
                 {MATERIAL_MAP_ROUGHNESS, aiTextureType_DIFFUSE_ROUGHNESS},
                 {MATERIAL_MAP_OCCLUSION, aiTextureType_AMBIENT_OCCLUSION},
                 {MATERIAL_MAP_EMISSION, aiTextureType_EMISSION_COLOR}}};

            for (const auto& [mapIndex, textureType] : textureTypes)
            {
                if (material.maps[mapIndex].texture.valid)
                {
                    continue;
                }

                aiString texturePath{};
                if (AI_SUCCESS != pSourceMaterial->GetTexture(textureType, 0, &texturePath))
                {
                    continue;
                }

                const std::string textureReference = texturePath.C_Str();
                const std::string fullTexturePath = model.directory + textureReference;
                ITexture loadedTexture{};
                if (!textureReference.empty() && textureReference.front() == '*')
                {
                    Image embeddedImage{};
                    std::vector<unsigned char> vRawPixels;
                    bool ownsImage = false;
                    if (DecodeEmbeddedAssimpTexture(*pScene, texturePath, embeddedImage, vRawPixels, ownsImage))
                    {
                        loadedTexture = LoadTextureFromImage(embeddedImage);
                        ReleaseEmbeddedAssimpTexture(embeddedImage, ownsImage);
                    }
                    else
                    {
                        TraceLog(LogLevel::Error, "MODEL",
                                 TextFormat("[D3D11] Failed to decode embedded texture: %s", textureReference.c_str()));
                    }
                }
                else
                {
                    loadedTexture = LoadTexture(fullTexturePath.c_str());
                }
                material.maps[mapIndex].texture =
                    Texture2D{loadedTexture.m_Id,      loadedTexture.m_Width,  loadedTexture.m_Height,
                              loadedTexture.m_Mipmaps, loadedTexture.m_Format, loadedTexture.m_Valid};
            }
        }

        qcPopulateModelSkeleton(pScene, model);

        for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex)
        {
            aiMesh* pSourceMesh = pScene->mMeshes[meshIndex];
            if (!pSourceMesh)
            {
                continue;
            }

            Mesh& destinationMesh = model.meshes[meshIndex];
            destinationMesh.vertexCount = static_cast<int>(pSourceMesh->mNumVertices);
            destinationMesh.triangleCount = static_cast<int>(pSourceMesh->mNumFaces);

            if (destinationMesh.vertexCount > 0)
            {
                destinationMesh.vertices = new float[destinationMesh.vertexCount * 3]{};
                destinationMesh.normals = new float[destinationMesh.vertexCount * 3]{};
                destinationMesh.texcoords = new float[destinationMesh.vertexCount * 2]{};
                destinationMesh.boneIndices =
                    new unsigned char[static_cast<std::size_t>(destinationMesh.vertexCount) * 4u]{};
                destinationMesh.boneWeights = new float[static_cast<std::size_t>(destinationMesh.vertexCount) * 4u]{};
            }

            if (destinationMesh.triangleCount > 0)
            {
                destinationMesh.indices = new unsigned short[destinationMesh.triangleCount * 3]{};
            }

            for (unsigned int boneIndex = 0; boneIndex < pSourceMesh->mNumBones; ++boneIndex)
            {
                const aiBone* pBone = pSourceMesh->mBones[boneIndex];
                const int skeletonBoneIndex = qcFindSkeletonBoneIndex(pScene, model, pBone);
                if (skeletonBoneIndex < 0 || skeletonBoneIndex > 255)
                {
                    TraceLog(LogLevel::Warn, "MODEL",
                             TextFormat("[D3D11] Mesh bone '%s' is not present in the model skeleton or exceeds the "
                                        "8-bit bone index range",
                                        pBone->mName.C_Str()));
                    continue;
                }
                qcSetMeshBoneOffset(destinationMesh, static_cast<unsigned int>(skeletonBoneIndex),
                                    model.skeleton.boneCount, pBone->mOffsetMatrix);
                for (unsigned int weightIndex = 0; weightIndex < pBone->mNumWeights; ++weightIndex)
                {
                    const aiVertexWeight& weight = pBone->mWeights[weightIndex];
                    const unsigned int vertexIndex = weight.mVertexId;
                    float* pDstWeights = destinationMesh.boneWeights + static_cast<std::size_t>(vertexIndex) * 4u;
                    unsigned char* pDstBones = destinationMesh.boneIndices + static_cast<std::size_t>(vertexIndex) * 4u;
                    for (int slot = 0; slot < 4; ++slot)
                    {
                        if (pDstWeights[slot] <= 0.0f)
                        {
                            pDstBones[slot] = static_cast<unsigned char>(skeletonBoneIndex);
                            pDstWeights[slot] = weight.mWeight;
                            break;
                        }
                    }
                }
            }

            if (pSourceMesh->HasVertexColors(0))
            {
                destinationMesh.colors = new unsigned char[destinationMesh.vertexCount * 4]{};
            }

            for (int vertexIndex = 0; vertexIndex < destinationMesh.vertexCount; ++vertexIndex)
            {
                const aiVector3D& position = pSourceMesh->mVertices[vertexIndex];
                destinationMesh.vertices[vertexIndex * 3 + 0] = position.x;
                destinationMesh.vertices[vertexIndex * 3 + 1] = position.y;
                destinationMesh.vertices[vertexIndex * 3 + 2] = position.z;

                if (pSourceMesh->HasNormals())
                {
                    const aiVector3D& normal = pSourceMesh->mNormals[vertexIndex];
                    destinationMesh.normals[vertexIndex * 3 + 0] = normal.x;
                    destinationMesh.normals[vertexIndex * 3 + 1] = normal.y;
                    destinationMesh.normals[vertexIndex * 3 + 2] = normal.z;
                }
                else
                {
                    destinationMesh.normals[vertexIndex * 3 + 0] = 0.0f;
                    destinationMesh.normals[vertexIndex * 3 + 1] = 1.0f;
                    destinationMesh.normals[vertexIndex * 3 + 2] = 0.0f;
                }

                if (pSourceMesh->HasTextureCoords(0))
                {
                    const aiVector3D& uv = pSourceMesh->mTextureCoords[0][vertexIndex];
                    destinationMesh.texcoords[vertexIndex * 2 + 0] = uv.x;
                    destinationMesh.texcoords[vertexIndex * 2 + 1] = uv.y;
                }
                else
                {
                    destinationMesh.texcoords[vertexIndex * 2 + 0] = 0.0f;
                    destinationMesh.texcoords[vertexIndex * 2 + 1] = 0.0f;
                }

                if (pSourceMesh->HasVertexColors(0))
                {
                    const aiColor4D& color = pSourceMesh->mColors[0][vertexIndex];
                    destinationMesh.colors[vertexIndex * 4 + 0] =
                        static_cast<unsigned char>(std::clamp(color.r * 255.0f, 0.0f, 255.0f));
                    destinationMesh.colors[vertexIndex * 4 + 1] =
                        static_cast<unsigned char>(std::clamp(color.g * 255.0f, 0.0f, 255.0f));
                    destinationMesh.colors[vertexIndex * 4 + 2] =
                        static_cast<unsigned char>(std::clamp(color.b * 255.0f, 0.0f, 255.0f));
                    destinationMesh.colors[vertexIndex * 4 + 3] =
                        static_cast<unsigned char>(std::clamp(color.a * 255.0f, 0.0f, 255.0f));
                }
            }

            for (int faceIndex = 0; faceIndex < destinationMesh.triangleCount; ++faceIndex)
            {
                const aiFace& face = pSourceMesh->mFaces[faceIndex];
                if (face.mNumIndices < 3)
                {
                    continue;
                }

                destinationMesh.indices[faceIndex * 3 + 0] = static_cast<unsigned short>(face.mIndices[0]);
                destinationMesh.indices[faceIndex * 3 + 1] = static_cast<unsigned short>(face.mIndices[1]);
                destinationMesh.indices[faceIndex * 3 + 2] = static_cast<unsigned short>(face.mIndices[2]);
            }

            ensure_mesh_tangents(destinationMesh);
            model.meshMaterial[meshIndex] = static_cast<int>(pSourceMesh->mMaterialIndex);
        }

        TraceLog(LogLevel::Info, "MODEL",
                 TextFormat("[D3D11] Model loaded successfully: %s (%d meshes, %d materials)", pFilePath,
                            model.meshCount, model.materialCount));
        return model;
    }

    void QuarkD3D11Renderer::UnloadModel(Model& model)
    {
        if (model.meshes)
        {
            for (int i = 0; i < model.meshCount; ++i)
            {
                UnloadMesh(model.meshes[i]);
            }
            delete[] model.meshes;
            model.meshes = nullptr;
        }

        if (model.materials)
        {
            for (int i = 0; i < model.materialCount; ++i)
            {
                Material& material = model.materials[i];
                if (material.maps)
                {
                    for (int mapIndex = 0; mapIndex <= MATERIAL_MAP_BRDF; ++mapIndex)
                    {
                        if (material.maps[mapIndex].texture.valid)
                        {
                            ITexture texture{
                                material.maps[mapIndex].texture.id,     material.maps[mapIndex].texture.width,
                                material.maps[mapIndex].texture.height, material.maps[mapIndex].texture.mipmaps,
                                material.maps[mapIndex].texture.format, material.maps[mapIndex].texture.valid};
                            UnloadTexture(texture);
                        }
                    }
                    delete[] material.maps;
                    material.maps = nullptr;
                }
            }
            delete[] model.materials;
            model.materials = nullptr;
        }

        delete[] model.meshMaterial;
        model.meshMaterial = nullptr;
        qcFreeModelSkeleton(model);
        model = {};
    }

    void QuarkD3D11Renderer::DrawModel(const Model& model, const Vec3& position, float scale, float rotationX,
                                       float rotationY, float rotationZ)
    {
        Mat4 transform = Mat4::translation(position.x, position.y, position.z) * Mat4::rotationY(rotationY) *
                         Mat4::rotationX(rotationX) * Mat4::rotationZ(rotationZ) * Mat4::scale(scale, scale, scale);
        DrawModelEx(model, transform);
    }

    void QuarkD3D11Renderer::DrawModelEx(const Model& model, const Mat4& transform)
    {
        const Mat4 modelTransform = transform * model.transform;

        for (int i = 0; i < model.meshCount; ++i)
        {
            const Mesh& mesh = model.meshes[i];
            const Material* pMaterial = nullptr;
            if (model.meshMaterial && i >= 0 && i < model.meshCount && model.meshMaterial[i] >= 0 &&
                model.meshMaterial[i] < model.materialCount)
            {
                pMaterial = &model.materials[model.meshMaterial[i]];
            }

            DrawMesh(mesh, pMaterial ? *pMaterial : Material{}, modelTransform);
        }
    }

    void QuarkD3D11Renderer::DrawModelEx(const Model& model, const Mat4& transform, Color tint)
    {
        const Mat4 modelTransform = transform * model.transform;
        std::vector<MaterialMap> vAdjustedMaps(MATERIAL_MAP_BRDF + 1, MaterialMap{});

        for (int i = 0; i < model.meshCount; ++i)
        {
            const Mesh& mesh = model.meshes[i];
            Material adjustedMaterial{};
            const Material* pSourceMaterial = nullptr;

            if (model.meshMaterial && i >= 0 && i < model.meshCount && model.meshMaterial[i] >= 0 &&
                model.meshMaterial[i] < model.materialCount)
            {
                pSourceMaterial = &model.materials[model.meshMaterial[i]];
            }

            if (pSourceMaterial && pSourceMaterial->maps)
            {
                std::copy(pSourceMaterial->maps, pSourceMaterial->maps + static_cast<int>(vAdjustedMaps.size()),
                          vAdjustedMaps.begin());
                adjustedMaterial = *pSourceMaterial;
                adjustedMaterial.maps = vAdjustedMaps.data();
                adjustedMaterial.maps[MATERIAL_MAP_ALBEDO].color =
                    MultiplyColor(adjustedMaterial.maps[MATERIAL_MAP_ALBEDO].color, tint);
            }
            else
            {
                adjustedMaterial.maps = vAdjustedMaps.data();
                adjustedMaterial.maps[MATERIAL_MAP_ALBEDO].color = tint;
            }

            DrawMesh(mesh, adjustedMaterial, modelTransform);
        }
    }

    void QuarkD3D11Renderer::UploadMesh(Mesh& mesh, bool dynamic)
    {
        (void)dynamic;
        ensure_mesh_tangents(mesh);
        mesh.vaoId = 0;
        mesh.vboId = 0;
        mesh.eboId = 0;
    }

    void QuarkD3D11Renderer::UpdateMeshBuffer(Mesh& mesh, int index, const void* pData, int dataSize, int offset)
    {
        if (!pData || dataSize <= 0 || offset < 0)
        {
            return;
        }

        auto copyBytes = [&](void* destination, std::size_t destinationBytes)
        {
            if (!destination || static_cast<std::size_t>(offset) >= destinationBytes)
            {
                return;
            }

            const std::size_t bytesToCopy = std::min<std::size_t>(static_cast<std::size_t>(dataSize),
                                                                  destinationBytes - static_cast<std::size_t>(offset));
            std::memcpy(static_cast<unsigned char*>(destination) + offset, pData, bytesToCopy);
        };

        switch (index)
        {
        case 0:
            copyBytes(mesh.vertices, static_cast<std::size_t>(mesh.vertexCount) * 3u * sizeof(float));
            ensure_mesh_tangents(mesh, true);
            break;
        case 1:
            copyBytes(mesh.normals, static_cast<std::size_t>(mesh.vertexCount) * 3u * sizeof(float));
            ensure_mesh_tangents(mesh, true);
            break;
        case 2:
            copyBytes(mesh.texcoords, static_cast<std::size_t>(mesh.vertexCount) * 2u * sizeof(float));
            ensure_mesh_tangents(mesh, true);
            break;
        case 6:
            copyBytes(mesh.indices, static_cast<std::size_t>(mesh.triangleCount) * 3u * sizeof(unsigned short));
            ensure_mesh_tangents(mesh, true);
            break;
        default:
            break;
        }
        InvalidateStaticGpuMesh(mesh.vertices);
    }

    void QuarkD3D11Renderer::UnloadMesh(Mesh& mesh)
    {
        InvalidateStaticGpuMesh(mesh.vertices);
        delete[] mesh.vertices;
        mesh.vertices = nullptr;
        delete[] mesh.texcoords;
        mesh.texcoords = nullptr;
        delete[] mesh.texcoords2;
        mesh.texcoords2 = nullptr;
        delete[] mesh.normals;
        mesh.normals = nullptr;
        delete[] mesh.tangents;
        mesh.tangents = nullptr;
        delete[] mesh.colors;
        mesh.colors = nullptr;
        delete[] mesh.indices;
        mesh.indices = nullptr;
        delete[] mesh.boneIndices;
        mesh.boneIndices = nullptr;
        delete[] mesh.boneWeights;
        mesh.boneWeights = nullptr;
        delete[] mesh.boneOffsets;
        mesh.boneOffsets = nullptr;
        mesh.boneOffsetCount = 0;
        delete[] mesh.animVertices;
        mesh.animVertices = nullptr;
        delete[] mesh.animNormals;
        mesh.animNormals = nullptr;

        mesh.vertexCount = 0;
        mesh.triangleCount = 0;
        mesh.vaoId = 0;
        mesh.vboId = 0;
        mesh.eboId = 0;
    }

    static void ensure_mesh_tangents(Mesh& mesh, bool forceRecompute)
    {
        if (!mesh.vertices || mesh.vertexCount <= 0)
            return;
        if (mesh.tangents && !forceRecompute)
            return;
        if (!mesh.tangents)
            mesh.tangents = new float[static_cast<size_t>(mesh.vertexCount) * 3u];

        for (int i = 0; i < mesh.vertexCount * 3; ++i)
            mesh.tangents[i] = 0.0f;

        const bool indexed = mesh.indices && mesh.triangleCount > 0;
        const int triangleCount = mesh.triangleCount > 0 ? mesh.triangleCount : static_cast<int>(mesh.vertexCount / 3);

        for (int pT = 0; pT < triangleCount; ++pT)
        {
            const int ia = indexed ? mesh.indices[pT * 3 + 0] : pT * 3 + 0;
            const int ib = indexed ? mesh.indices[pT * 3 + 1] : pT * 3 + 1;
            const int ic = indexed ? mesh.indices[pT * 3 + 2] : pT * 3 + 2;
            if (ia < 0 || ib < 0 || ic < 0 || ia >= mesh.vertexCount || ib >= mesh.vertexCount ||
                ic >= mesh.vertexCount)
                continue;

            const float* pV0 = mesh.vertices + ia * 3;
            const float* pV1 = mesh.vertices + ib * 3;
            const float* pV2 = mesh.vertices + ic * 3;

            const float e1x = pV1[0] - pV0[0], e1y = pV1[1] - pV0[1], e1z = pV1[2] - pV0[2];
            const float e2x = pV2[0] - pV0[0], e2y = pV2[1] - pV0[1], e2z = pV2[2] - pV0[2];

            float tx = 0.0f, ty = 0.0f, tz = 0.0f;
            if (mesh.texcoords)
            {
                const float u0 = mesh.texcoords[ia * 2 + 0];
                const float vt0 = mesh.texcoords[ia * 2 + 1];
                const float u1 = mesh.texcoords[ib * 2 + 0];
                const float vt1 = mesh.texcoords[ib * 2 + 1];
                const float u2 = mesh.texcoords[ic * 2 + 0];
                const float vt2 = mesh.texcoords[ic * 2 + 1];

                const float du1 = u1 - u0, dv1 = vt1 - vt0;
                const float du2 = u2 - u0, dv2 = vt2 - vt0;
                const float determinant = du1 * dv2 - du2 * dv1;
                if (fabsf(determinant) > 1e-8f)
                {
                    const float r = 1.0f / determinant;
                    tx = (e1x * dv2 - e2x * dv1) * r;
                    ty = (e1y * dv2 - e2y * dv1) * r;
                    tz = (e1z * dv2 - e2z * dv1) * r;
                }
            }
            if (tx == 0.0f && ty == 0.0f && tz == 0.0f)
                tx = 1.0f;

            const int idx[3] = {ia, ib, ic};
            for (int k = 0; k < 3; ++k)
            {
                float* pDst = mesh.tangents + idx[k] * 3;
                pDst[0] += tx;
                pDst[1] += ty;
                pDst[2] += tz;
            }
        }

        for (int i = 0; i < mesh.vertexCount; ++i)
        {
            float* pT = mesh.tangents + i * 3;
            const float* pN = mesh.normals ? mesh.normals + i * 3 : nullptr;
            if (pN)
            {
                const float nx = pN[0], ny = pN[1], nz = pN[2];
                const float nn = sqrtf(nx * nx + ny * ny + nz * nz);
                if (nn > 1e-9f)
                {
                    const float inv = 1.0f / nn;
                    const float anx = nx * inv, any = ny * inv, anz = nz * inv;
                    const float d = pT[0] * anx + pT[1] * any + pT[2] * anz;
                    pT[0] -= anx * d;
                    pT[1] -= any * d;
                    pT[2] -= anz * d;
                }
            }
            const float total = sqrtf(pT[0] * pT[0] + pT[1] * pT[1] + pT[2] * pT[2]);
            if (total > 1e-8f)
            {
                const float inv = 1.0f / total;
                pT[0] *= inv;
                pT[1] *= inv;
                pT[2] *= inv;
            }
            else
            {
                pT[0] = 1.0f;
                pT[1] = 0.0f;
                pT[2] = 0.0f;
            }
        }
    }

    void QuarkD3D11Renderer::DrawMesh(const Mesh& mesh, const Material& material, const Mat4& transform)
    {
        if (!mesh.vertices || mesh.vertexCount <= 0)
        {
            return;
        }
        if (!mesh.tangents)
        {
            ensure_mesh_tangents(const_cast<Mesh&>(mesh));
        }

        ShaderProgramData* pCustomProgram = Resolve3DShaderProgram(material);
        if (pCustomProgram != nullptr)
        {
            DrawMeshWithShader(mesh, material, transform, *pCustomProgram);
            return;
        }

        const ID3D11ShaderResourceView* pTextureResource = nullptr;
        const MaterialMap* pAlbedoMap = nullptr;
        if (material.maps)
        {
            pAlbedoMap = &material.maps[MATERIAL_MAP_ALBEDO];
            if (pAlbedoMap->texture.valid || pAlbedoMap->texture.id != 0)
            {
                pTextureResource = m_Resources.ShaderResource(pAlbedoMap->texture.id);
            }
        }

        ID3D11ShaderResourceView* pNormalResource = nullptr;
        if (material.maps)
        {
            const MaterialMap& normalMap = material.maps[MATERIAL_MAP_NORMAL];
            if (normalMap.texture.valid || normalMap.texture.id != 0)
            {
                pNormalResource = m_Resources.ShaderResource(normalMap.texture.id);
            }
        }
        if (pNormalResource == nullptr && m_FlatNormalShaderTexture.IsValid())
        {
            pNormalResource = m_Resources.ShaderResource(m_FlatNormalShaderTexture.m_Id);
        }

        const Mat4 finalTransform = m_CurrentMatrix * transform;
        const Mat4 viewProj = m_ProjectionMatrix * m_ViewMatrix;
        const Color baseColor = pAlbedoMap ? pAlbedoMap->color : WHITE;
        const bool textured = pTextureResource != nullptr;
        const std::size_t floatsPerVertex = textured ? 22u : 16u;

        const auto transformNormal = [&](const Vec3& normal) -> Vec3
        {
            const Vec3 rotated{
                finalTransform.m[0] * normal.x + finalTransform.m[4] * normal.y + finalTransform.m[8] * normal.z,
                finalTransform.m[1] * normal.x + finalTransform.m[5] * normal.y + finalTransform.m[9] * normal.z,
                finalTransform.m[2] * normal.x + finalTransform.m[6] * normal.y + finalTransform.m[10] * normal.z};
            return SafeNormalized(rotated, Vec3{0.0f, 1.0f, 0.0f});
        };

        const auto meshNormalAt = [&](int vertexIndex) -> Vec3
        {
            if (mesh.normals && vertexIndex >= 0 && vertexIndex < mesh.vertexCount)
            {
                return transformNormal(Vec3{mesh.normals[vertexIndex * 3 + 0], mesh.normals[vertexIndex * 3 + 1],
                                            mesh.normals[vertexIndex * 3 + 2]});
            }
            return Vec3{0.0f, 1.0f, 0.0f};
        };

        const auto meshTangentAt = [&](int vertexIndex) -> Vec3
        {
            if (mesh.tangents && vertexIndex >= 0 && vertexIndex < mesh.vertexCount)
            {
                return transformNormal(Vec3{mesh.tangents[vertexIndex * 3 + 0], mesh.tangents[vertexIndex * 3 + 1],
                                            mesh.tangents[vertexIndex * 3 + 2]});
            }
            return Vec3{0.0f, 0.0f, 0.0f};
        };

        const auto writeVertex =
            [&](float* dst, const Vec4& world, const Vec3& normal, const Vec3& tangent, Color color, float u, float v)
        {
            const Vec4 clip = viewProj * world;
            std::size_t idx = 0;
            dst[idx++] = clip.x;
            dst[idx++] = clip.y;
            dst[idx++] = clip.z;
            dst[idx++] = clip.w;

            if (textured)
            {
                dst[idx++] = u;
                dst[idx++] = v;
            }

            dst[idx++] = color.r / 255.0f;
            dst[idx++] = color.g / 255.0f;
            dst[idx++] = color.b / 255.0f;
            dst[idx++] = color.a / 255.0f;

            dst[idx++] = world.x;
            dst[idx++] = world.y;
            dst[idx++] = world.z;
            dst[idx++] = 1.0f;

            dst[idx++] = normal.x;
            dst[idx++] = normal.y;
            dst[idx++] = normal.z;
            dst[idx++] = 1.0f;

            if (textured)
            {
                const bool hasTangent = (tangent.x != 0.0f || tangent.y != 0.0f || tangent.z != 0.0f);
                dst[idx++] = tangent.x;
                dst[idx++] = tangent.y;
                dst[idx++] = tangent.z;
                dst[idx++] = hasTangent ? 1.0f : 0.0f;
            }
        };

        const auto worldPosAt = [&](int vertexIndex) -> Vec4
        {
            return finalTransform * Vec4{mesh.vertices[vertexIndex * 3 + 0], mesh.vertices[vertexIndex * 3 + 1],
                                         mesh.vertices[vertexIndex * 3 + 2], 1.0f};
        };

        if (mesh.indices && mesh.triangleCount > 0)
        {
            const UINT totalVertices = static_cast<UINT>(mesh.triangleCount) * 3u;
            const size_t sliceOffset =
                m_Commands.Allocate3DVertexSlice(totalVertices, static_cast<UINT>(floatsPerVertex));
            float* pOutBase = m_Commands.Get3DVertexPtr(sliceOffset);

            m_ThreadPool.ParallelFor(
                static_cast<size_t>(mesh.triangleCount), 256,
                [&](uint32_t, size_t beginTri, size_t endTri)
                {
                    for (size_t t = beginTri; t < endTri; ++t)
                    {
                        const int triangleIndex = static_cast<int>(t);
                        float* triDst = pOutBase + t * 3u * floatsPerVertex;
                        int vertexIndex[3] = {-1, -1, -1};
                        for (int localIndex = 0; localIndex < 3; ++localIndex)
                        {
                            const int index = mesh.indices[triangleIndex * 3 + localIndex];
                            if (index >= 0 && index < mesh.vertexCount)
                            {
                                vertexIndex[localIndex] = index;
                            }
                        }
                        if (vertexIndex[0] < 0 || vertexIndex[1] < 0 || vertexIndex[2] < 0)
                        {
                            std::memset(triDst, 0, 3u * floatsPerVertex * sizeof(float));
                            continue;
                        }

                        const Vec4 worldVerts[3] = {worldPosAt(vertexIndex[0]), worldPosAt(vertexIndex[1]),
                                                    worldPosAt(vertexIndex[2])};
                        Vec3 faceNormal{0.0f, 1.0f, 0.0f};
                        if (!mesh.normals)
                        {
                            faceNormal =
                                SafeNormalized((Vec3{worldVerts[1].x, worldVerts[1].y, worldVerts[1].z} -
                                                Vec3{worldVerts[0].x, worldVerts[0].y, worldVerts[0].z})
                                                   .cross(Vec3{worldVerts[2].x, worldVerts[2].y, worldVerts[2].z} -
                                                          Vec3{worldVerts[0].x, worldVerts[0].y, worldVerts[0].z}),
                                               Vec3{0.0f, 1.0f, 0.0f});
                        }

                        for (int localIndex = 0; localIndex < 3; ++localIndex)
                        {
                            const int vi = vertexIndex[localIndex];
                            const Vec3 normal = mesh.normals ? meshNormalAt(vi) : faceNormal;
                            const float u = mesh.texcoords ? mesh.texcoords[vi * 2 + 0] : 0.0f;
                            const float v = mesh.texcoords ? mesh.texcoords[vi * 2 + 1] : 0.0f;
                            const Color vertexColor = MultiplyColor(baseColor, ReadMeshVertexColor(mesh, vi, WHITE));
                            writeVertex(triDst + static_cast<size_t>(localIndex) * floatsPerVertex,
                                        worldVerts[localIndex], normal, meshTangentAt(vi), vertexColor, u, v);
                        }
                    }
                });

            if (textured)
            {
                m_Commands.SubmitQueued3DTextured(sliceOffset, totalVertices,
                                                  const_cast<ID3D11ShaderResourceView*>(pTextureResource),
                                                  pNormalResource);
            }
            else
            {
                m_Commands.SubmitQueued3D(sliceOffset, totalVertices, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            }
            return;
        }

        const int rawTriangleCount = mesh.triangleCount > 0 ? mesh.triangleCount : mesh.vertexCount / 3;
        const int validTriangles = std::min(rawTriangleCount, mesh.vertexCount / 3);
        if (validTriangles <= 0)
        {
            return;
        }

        const UINT totalVertices = static_cast<UINT>(validTriangles) * 3u;
        const size_t sliceOffset = m_Commands.Allocate3DVertexSlice(totalVertices, static_cast<UINT>(floatsPerVertex));
        float* pOutBase = m_Commands.Get3DVertexPtr(sliceOffset);

        m_ThreadPool.ParallelFor(
            static_cast<size_t>(validTriangles), 256,
            [&](uint32_t, size_t beginTri, size_t endTri)
            {
                for (size_t t = beginTri; t < endTri; ++t)
                {
                    const int viBase = static_cast<int>(t) * 3;
                    float* triDst = pOutBase + t * 3u * floatsPerVertex;

                    const Vec4 worldVerts[3] = {worldPosAt(viBase + 0), worldPosAt(viBase + 1), worldPosAt(viBase + 2)};
                    Vec3 faceNormal{0.0f, 1.0f, 0.0f};
                    if (!mesh.normals)
                    {
                        faceNormal = SafeNormalized((Vec3{worldVerts[1].x, worldVerts[1].y, worldVerts[1].z} -
                                                     Vec3{worldVerts[0].x, worldVerts[0].y, worldVerts[0].z})
                                                        .cross(Vec3{worldVerts[2].x, worldVerts[2].y, worldVerts[2].z} -
                                                               Vec3{worldVerts[0].x, worldVerts[0].y, worldVerts[0].z}),
                                                    Vec3{0.0f, 1.0f, 0.0f});
                    }

                    for (int localIndex = 0; localIndex < 3; ++localIndex)
                    {
                        const int vi = viBase + localIndex;
                        const Vec3 normal = mesh.normals ? meshNormalAt(vi) : faceNormal;
                        const float u = mesh.texcoords ? mesh.texcoords[vi * 2 + 0] : 0.0f;
                        const float v = mesh.texcoords ? mesh.texcoords[vi * 2 + 1] : 0.0f;
                        const Color vertexColor = MultiplyColor(baseColor, ReadMeshVertexColor(mesh, vi, WHITE));
                        writeVertex(triDst + static_cast<size_t>(localIndex) * floatsPerVertex, worldVerts[localIndex],
                                    normal, meshTangentAt(vi), vertexColor, u, v);
                    }
                }
            });

        if (textured)
        {
            m_Commands.SubmitQueued3DTextured(sliceOffset, totalVertices,
                                              const_cast<ID3D11ShaderResourceView*>(pTextureResource), pNormalResource);
        }
        else
        {
            m_Commands.SubmitQueued3D(sliceOffset, totalVertices, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }
    }

    QuarkD3D11Renderer::ShaderProgramData* QuarkD3D11Renderer::Resolve3DShaderProgram(const Material& material)
    {
        uint32_t shaderId = (material.shader && material.shader->id != 0) ? material.shader->id : m_CurrentShaderId;
        if (shaderId == 0)
        {
            return nullptr;
        }

        ShaderProgramData* pProgram = GetShaderProgram(shaderId);
        if (!pProgram || !pProgram->m_VertexShader || !pProgram->m_PixelShader || !pProgram->m_InputLayout)
        {
            return nullptr;
        }

        if (!pProgram->m_HasPosition)
        {
            return nullptr;
        }

        return pProgram;
    }

    void QuarkD3D11Renderer::DrawMeshWithShader(const Mesh& mesh, const Material& material, const Mat4& transform,
                                                ShaderProgramData& program)
    {
        ID3D11ShaderResourceView* pAlbedoResource = nullptr;
        const MaterialMap* pAlbedoMap = nullptr;
        if (material.maps)
        {
            pAlbedoMap = &material.maps[MATERIAL_MAP_ALBEDO];
            if (pAlbedoMap->texture.valid || pAlbedoMap->texture.id != 0)
            {
                pAlbedoResource = m_Resources.ShaderResource(pAlbedoMap->texture.id);
            }
        }

        const uint32_t shaderId =
            (material.shader && material.shader->id != 0) ? material.shader->id : m_CurrentShaderId;

        const UINT floatsPerVertex = program.m_StrideBytes / sizeof(float);
        const auto offsetInFloats = [](UINT byteOffset) -> UINT
        {
            return byteOffset == 0xFFFFFFFFu ? 0xFFFFFFFFu : byteOffset / sizeof(float);
        };
        const UINT positionOffset = offsetInFloats(program.m_PositionOffset);
        const UINT texCoordOffset = offsetInFloats(program.m_TexCoordOffset);
        const UINT colorOffset = offsetInFloats(program.m_ColorOffset);
        const UINT normalOffset = offsetInFloats(program.m_NormalOffset);
        const UINT worldPositionOffset = offsetInFloats(program.m_WorldPositionOffset);
        const UINT tangentOffset = offsetInFloats(program.m_TangentOffset);
        const bool standardConvention = program.m_WorldPositionOffset == 0xFFFFFFFFu;

        const Mat4 finalTransform = m_CurrentMatrix * transform;
        const Color baseColor = pAlbedoMap ? pAlbedoMap->color : WHITE;

        if (standardConvention)
        {
            const Mat4 normalMatrix = TransposeMat4(finalTransform.inverted());
            const Mat4 mvp = m_ProjectionMatrix * (m_ViewMatrix * finalTransform);

            if (program.m_LocModel >= 0)
            {
                StoreUniformValue(program, program.m_LocModel, 0, finalTransform.m, sizeof(float) * 16, 1);
            }
            if (program.m_LocView >= 0)
            {
                StoreUniformValue(program, program.m_LocView, 0, m_ViewMatrix.m, sizeof(float) * 16, 1);
            }
            if (program.m_LocProjection >= 0)
            {
                StoreUniformValue(program, program.m_LocProjection, 0, m_ProjectionMatrix.m, sizeof(float) * 16, 1);
            }
            if (program.m_LocMvp >= 0)
            {
                StoreUniformValue(program, program.m_LocMvp, 0, mvp.m, sizeof(float) * 16, 1);
            }
            if (program.m_LocNormal >= 0)
            {
                StoreUniformValue(program, program.m_LocNormal, 0, normalMatrix.m, sizeof(float) * 16, 1);
            }
            if (program.m_LocUseInstancing >= 0)
            {
                const int zero = 0;
                StoreUniformValue(program, program.m_LocUseInstancing, 0, &zero, sizeof(int), 1);
            }

            const bool useStaticGpuMesh = standardConvention && mesh.vertexCount > 0;
            const float diffuseColor[4] = {
                (!useStaticGpuMesh && colorOffset != 0xFFFFFFFFu) ? 1.0f : baseColor.r / 255.0f,
                (!useStaticGpuMesh && colorOffset != 0xFFFFFFFFu) ? 1.0f : baseColor.g / 255.0f,
                (!useStaticGpuMesh && colorOffset != 0xFFFFFFFFu) ? 1.0f : baseColor.b / 255.0f,
                (!useStaticGpuMesh && colorOffset != 0xFFFFFFFFu) ? 1.0f : baseColor.a / 255.0f};
            if (program.m_LocColor >= 0)
            {
                StoreUniformValue(program, program.m_LocColor, 0, diffuseColor, sizeof(float) * 4, 1);
            }
        }

        D3D11CommandContext::ShaderOverride shaderOverride = BuildShaderOverride(shaderId, pAlbedoResource, false);
        if (!shaderOverride.Active())
        {
            return;
        }

        if (material.maps)
        {
            ID3D11ShaderResourceView* pWhiteFallback =
                m_WhiteShaderTexture.IsValid() ? m_Resources.ShaderResource(m_WhiteShaderTexture.m_Id) : nullptr;
            ID3D11ShaderResourceView* pBlackFallback =
                m_BlackShaderTexture.IsValid() ? m_Resources.ShaderResource(m_BlackShaderTexture.m_Id) : nullptr;
            ID3D11ShaderResourceView* pFlatNormalFallback =
                m_FlatNormalShaderTexture.IsValid() ? m_Resources.ShaderResource(m_FlatNormalShaderTexture.m_Id)
                                                    : nullptr;

            const auto fillMapSlot = [&](UINT slot, int mapIndex, ID3D11ShaderResourceView* fallback)
            {
                if (shaderOverride.m_apShaderResources[slot] != nullptr)
                {
                    return;
                }
                const MaterialMap& map = material.maps[mapIndex];
                if (map.texture.valid || map.texture.id != 0)
                {
                    shaderOverride.m_apShaderResources[slot] = m_Resources.ShaderResource(map.texture.id);
                }
                else if (fallback)
                {
                    shaderOverride.m_apShaderResources[slot] = fallback;
                }
            };

            fillMapSlot(1, MATERIAL_MAP_HEIGHT + 0, pWhiteFallback);
            fillMapSlot(2, MATERIAL_MAP_HEIGHT + 1, pWhiteFallback);
            fillMapSlot(3, MATERIAL_MAP_HEIGHT + 2, pWhiteFallback);
            fillMapSlot(4, MATERIAL_MAP_HEIGHT + 3, pWhiteFallback);
            fillMapSlot(5, MATERIAL_MAP_METALNESS, pWhiteFallback);
            fillMapSlot(6, MATERIAL_MAP_NORMAL, pFlatNormalFallback);
            fillMapSlot(7, MATERIAL_MAP_ROUGHNESS, pWhiteFallback);
            fillMapSlot(8, MATERIAL_MAP_OCCLUSION, pWhiteFallback);
            fillMapSlot(9, MATERIAL_MAP_EMISSION, pBlackFallback);
        }

        if (standardConvention && mesh.vertexCount > 0)
        {
            if (StaticGpuMesh* gpuMesh = GetOrCreateStaticGpuMesh(mesh, program))
            {
                m_Commands.SubmitQueued3DStaticMesh(gpuMesh->m_VertexBuffer.Get(), gpuMesh->m_IndexBuffer.Get(),
                                                    gpuMesh->m_VertexCount, gpuMesh->m_IndexCount, gpuMesh->m_Indexed,
                                                    shaderOverride);
                return;
            }
        }

        const auto transformNormal = [&](const Vec3& normal) -> Vec3
        {
            if (standardConvention)
            {
                return normal;
            }
            const Vec3 rotated{
                finalTransform.m[0] * normal.x + finalTransform.m[4] * normal.y + finalTransform.m[8] * normal.z,
                finalTransform.m[1] * normal.x + finalTransform.m[5] * normal.y + finalTransform.m[9] * normal.z,
                finalTransform.m[2] * normal.x + finalTransform.m[6] * normal.y + finalTransform.m[10] * normal.z};
            return SafeNormalized(rotated, Vec3{0.0f, 1.0f, 0.0f});
        };

        const auto meshNormalAt = [&](int vertexIndex) -> Vec3
        {
            if (mesh.normals && vertexIndex >= 0 && vertexIndex < mesh.vertexCount)
            {
                return transformNormal(Vec3{mesh.normals[vertexIndex * 3 + 0], mesh.normals[vertexIndex * 3 + 1],
                                            mesh.normals[vertexIndex * 3 + 2]});
            }
            return Vec3{0.0f, 1.0f, 0.0f};
        };

        const auto meshTangentAt = [&](int vertexIndex) -> Vec3
        {
            if (mesh.tangents && vertexIndex >= 0 && vertexIndex < mesh.vertexCount)
            {
                return transformNormal(Vec3{mesh.tangents[vertexIndex * 3 + 0], mesh.tangents[vertexIndex * 3 + 1],
                                            mesh.tangents[vertexIndex * 3 + 2]});
            }
            return Vec3{1.0f, 0.0f, 0.0f};
        };

        const auto localPosAt = [&](int vertexIndex) -> Vec4
        {
            return Vec4{mesh.vertices[vertexIndex * 3 + 0], mesh.vertices[vertexIndex * 3 + 1],
                        mesh.vertices[vertexIndex * 3 + 2], 1.0f};
        };

        const auto faceNormalOf = [&](int indexA, int indexB, int indexC) -> Vec3
        {
            if (!standardConvention)
            {
                const Vec4 worldA = finalTransform * localPosAt(indexA);
                const Vec4 worldB = finalTransform * localPosAt(indexB);
                const Vec4 worldC = finalTransform * localPosAt(indexC);
                return SafeNormalized((Vec3{worldB.x - worldA.x, worldB.y - worldA.y, worldB.z - worldA.z})
                                          .cross(Vec3{worldC.x - worldA.x, worldC.y - worldA.y, worldC.z - worldA.z}),
                                      Vec3{0.0f, 1.0f, 0.0f});
            }
            const Vec4 localA = localPosAt(indexA);
            const Vec4 localB = localPosAt(indexB);
            const Vec4 localC = localPosAt(indexC);
            return SafeNormalized((Vec3{localB.x - localA.x, localB.y - localA.y, localB.z - localA.z})
                                      .cross(Vec3{localC.x - localA.x, localC.y - localA.y, localC.z - localA.z}),
                                  Vec3{0.0f, 1.0f, 0.0f});
        };

        const Mat4 viewProj = m_ProjectionMatrix * m_ViewMatrix;

        const auto writeVec = [](float* vertex, UINT offset, const float* values, int count)
        {
            for (int index = 0; index < count; ++index)
            {
                vertex[offset + index] = values[index];
            }
        };

        const auto writeVertex = [&](float* vertex, int vertexIndex, const Vec3& normal, Color color, float u, float v)
        {
            std::memset(vertex, 0, static_cast<size_t>(floatsPerVertex) * sizeof(float));

            const Vec4 localPos = localPosAt(vertexIndex);
            if (positionOffset != 0xFFFFFFFFu)
            {
                if (standardConvention)
                {
                    const float position[4] = {localPos.x, localPos.y, localPos.z, localPos.w};
                    writeVec(vertex, positionOffset, position, std::min<int>(program.m_PositionComponents, 4));
                }
                else
                {
                    const Vec4 world = finalTransform * localPos;
                    const Vec4 clip = viewProj * world;
                    const float position[4] = {clip.x, clip.y, clip.z, clip.w};
                    writeVec(vertex, positionOffset, position, std::min<int>(program.m_PositionComponents, 4));
                }
            }
            if (texCoordOffset != 0xFFFFFFFFu)
            {
                const float texCoord[2] = {u, v};
                writeVec(vertex, texCoordOffset, texCoord, std::min<int>(program.m_TexCoordComponents, 2));
            }
            if (colorOffset != 0xFFFFFFFFu)
            {
                const float vertexColor[4] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
                writeVec(vertex, colorOffset, vertexColor, std::min<int>(program.m_ColorComponents, 4));
            }
            if (normalOffset != 0xFFFFFFFFu)
            {
                const float normalData[4] = {normal.x, normal.y, normal.z, 1.0f};
                writeVec(vertex, normalOffset, normalData, std::min<int>(program.m_NormalComponents, 4));
            }
            if (worldPositionOffset != 0xFFFFFFFFu)
            {
                const Vec4 world = finalTransform * localPos;
                const float worldData[4] = {world.x, world.y, world.z, world.w};
                writeVec(vertex, worldPositionOffset, worldData, std::min<int>(program.m_WorldPositionComponents, 4));
            }
            if (tangentOffset != 0xFFFFFFFFu)
            {
                const Vec3 tangent = meshTangentAt(vertexIndex);
                const bool hasTangent = (tangent.x != 0.0f || tangent.y != 0.0f || tangent.z != 0.0f);
                const float tangentData[4] = {tangent.x, tangent.y, tangent.z, hasTangent ? 1.0f : 0.0f};
                writeVec(vertex, tangentOffset, tangentData, std::min<int>(program.m_TangentComponents, 4));
            }
        };

        if (mesh.indices && mesh.triangleCount > 0)
        {
            const UINT totalVertices = static_cast<UINT>(mesh.triangleCount) * 3u;
            const size_t sliceOffset = m_Commands.Allocate3DVertexSlice(totalVertices, floatsPerVertex);
            float* pOutBase = m_Commands.Get3DVertexPtr(sliceOffset);

            m_ThreadPool.ParallelFor(
                static_cast<size_t>(mesh.triangleCount), 256,
                [&](uint32_t, size_t beginTri, size_t endTri)
                {
                    for (size_t t = beginTri; t < endTri; ++t)
                    {
                        const int triangleIndex = static_cast<int>(t);
                        float* triDst = pOutBase + t * 3u * static_cast<size_t>(floatsPerVertex);
                        int vertexIndex[3] = {-1, -1, -1};
                        for (int localIndex = 0; localIndex < 3; ++localIndex)
                        {
                            const int index = mesh.indices[triangleIndex * 3 + localIndex];
                            if (index >= 0 && index < mesh.vertexCount)
                            {
                                vertexIndex[localIndex] = index;
                            }
                        }
                        if (vertexIndex[0] < 0 || vertexIndex[1] < 0 || vertexIndex[2] < 0)
                        {
                            std::memset(triDst, 0, 3u * static_cast<size_t>(floatsPerVertex) * sizeof(float));
                            continue;
                        }

                        const Vec3 faceNormal = mesh.normals
                                                    ? Vec3{0.0f, 1.0f, 0.0f}
                                                    : faceNormalOf(vertexIndex[0], vertexIndex[1], vertexIndex[2]);
                        for (int localIndex = 0; localIndex < 3; ++localIndex)
                        {
                            const int vi = vertexIndex[localIndex];
                            const Vec3 normal = mesh.normals ? meshNormalAt(vi) : faceNormal;
                            const float u = mesh.texcoords ? mesh.texcoords[vi * 2 + 0] : 0.0f;
                            const float v = mesh.texcoords ? mesh.texcoords[vi * 2 + 1] : 0.0f;
                            const Color vertexColor = MultiplyColor(baseColor, ReadMeshVertexColor(mesh, vi, WHITE));
                            writeVertex(triDst + static_cast<size_t>(localIndex) * floatsPerVertex, vi, normal,
                                        vertexColor, u, v);
                        }
                    }
                });

            m_Commands.SubmitQueued3DShader(sliceOffset, totalVertices, shaderOverride);
            return;
        }

        const int rawTriangleCount = mesh.triangleCount > 0 ? mesh.triangleCount : mesh.vertexCount / 3;
        const int validTriangles = std::min(rawTriangleCount, mesh.vertexCount / 3);
        if (validTriangles <= 0)
        {
            return;
        }

        const UINT totalVertices = static_cast<UINT>(validTriangles) * 3u;
        const size_t sliceOffset = m_Commands.Allocate3DVertexSlice(totalVertices, floatsPerVertex);
        float* pOutBase = m_Commands.Get3DVertexPtr(sliceOffset);

        m_ThreadPool.ParallelFor(static_cast<size_t>(validTriangles), 256,
                                 [&](uint32_t, size_t beginTri, size_t endTri)
                                 {
                                     for (size_t t = beginTri; t < endTri; ++t)
                                     {
                                         const int viBase = static_cast<int>(t) * 3;
                                         float* triDst = pOutBase + t * 3u * static_cast<size_t>(floatsPerVertex);
                                         const Vec3 faceNormal = mesh.normals
                                                                     ? Vec3{0.0f, 1.0f, 0.0f}
                                                                     : faceNormalOf(viBase + 0, viBase + 1, viBase + 2);
                                         for (int localIndex = 0; localIndex < 3; ++localIndex)
                                         {
                                             const int vi = viBase + localIndex;
                                             const Vec3 normal = mesh.normals ? meshNormalAt(vi) : faceNormal;
                                             const float u = mesh.texcoords ? mesh.texcoords[vi * 2 + 0] : 0.0f;
                                             const float v = mesh.texcoords ? mesh.texcoords[vi * 2 + 1] : 0.0f;
                                             const Color vertexColor =
                                                 MultiplyColor(baseColor, ReadMeshVertexColor(mesh, vi, WHITE));
                                             writeVertex(triDst + static_cast<size_t>(localIndex) * floatsPerVertex, vi,
                                                         normal, vertexColor, u, v);
                                         }
                                     }
                                 });

        m_Commands.SubmitQueued3DShader(sliceOffset, totalVertices, shaderOverride);
    }

    void QuarkD3D11Renderer::InvalidateStaticGpuMesh(const float* pVertices)
    {
        if (!pVertices)
        {
            return;
        }
        const auto it = m_vStaticMeshKeysByVertices.find(pVertices);
        if (it == m_vStaticMeshKeysByVertices.end())
        {
            return;
        }
        for (uint64_t key : it->second)
        {
            m_StaticGpuMeshes.erase(key);
        }
        m_vStaticMeshKeysByVertices.erase(it);
    }

    QuarkD3D11Renderer::StaticGpuMesh* QuarkD3D11Renderer::GetOrCreateStaticGpuMesh(const Mesh& mesh,
                                                                                    const ShaderProgramData& program)
    {
        if (!m_Device.Get() || !mesh.vertices || mesh.vertexCount <= 0 || program.m_StrideBytes == 0)
        {
            return nullptr;
        }

        uint64_t key = 1469598103934665603ull;
        const uintptr_t ptrVal = reinterpret_cast<uintptr_t>(mesh.vertices);
        const auto hashBytes = [&](const void* data, size_t len)
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (size_t i = 0; i < len; ++i)
            {
                key ^= bytes[i];
                key *= 1099511628211ull;
            }
        };
        hashBytes(&ptrVal, sizeof(ptrVal));
        hashBytes(&program.m_StrideBytes, sizeof(program.m_StrideBytes));
        hashBytes(&program.m_PositionOffset, sizeof(program.m_PositionOffset));
        hashBytes(&program.m_TexCoordOffset, sizeof(program.m_TexCoordOffset));
        hashBytes(&program.m_ColorOffset, sizeof(program.m_ColorOffset));
        hashBytes(&program.m_NormalOffset, sizeof(program.m_NormalOffset));
        hashBytes(&program.m_TangentOffset, sizeof(program.m_TangentOffset));

        const auto existing = m_StaticGpuMeshes.find(key);
        if (existing != m_StaticGpuMeshes.end())
        {
            return &existing->second;
        }

        const UINT floatsPerVertex = program.m_StrideBytes / sizeof(float);
        const auto offsetInFloats = [](UINT byteOffset) -> UINT
        {
            return byteOffset == 0xFFFFFFFFu ? 0xFFFFFFFFu : byteOffset / sizeof(float);
        };
        const UINT positionOffset = offsetInFloats(program.m_PositionOffset);
        const UINT texCoordOffset = offsetInFloats(program.m_TexCoordOffset);
        const UINT colorOffset = offsetInFloats(program.m_ColorOffset);
        const UINT normalOffset = offsetInFloats(program.m_NormalOffset);
        const UINT tangentOffset = offsetInFloats(program.m_TangentOffset);

        const auto writeVec = [](float* vertex, UINT offset, const float* values, int count)
        {
            for (int idx = 0; idx < count; ++idx)
            {
                vertex[offset + idx] = values[idx];
            }
        };

        const bool hasIndices = (mesh.indices != nullptr && mesh.triangleCount > 0);
        std::vector<float> vVertexData(static_cast<size_t>(mesh.vertexCount) * floatsPerVertex, 0.0f);
        for (int vi = 0; vi < mesh.vertexCount; ++vi)
        {
            float* pDst = vVertexData.data() + static_cast<size_t>(vi) * floatsPerVertex;
            if (positionOffset != 0xFFFFFFFFu)
            {
                const float pos[4] = {mesh.vertices[vi * 3 + 0], mesh.vertices[vi * 3 + 1], mesh.vertices[vi * 3 + 2],
                                      1.0f};
                writeVec(pDst, positionOffset, pos, std::min<int>(program.m_PositionComponents, 4));
            }
            if (texCoordOffset != 0xFFFFFFFFu)
            {
                const float uv[2] = {mesh.texcoords ? mesh.texcoords[vi * 2 + 0] : 0.0f,
                                     mesh.texcoords ? mesh.texcoords[vi * 2 + 1] : 0.0f};
                writeVec(pDst, texCoordOffset, uv, std::min<int>(program.m_TexCoordComponents, 2));
            }
            if (colorOffset != 0xFFFFFFFFu)
            {
                const Color vc = ReadMeshVertexColor(mesh, vi, WHITE);
                const float col[4] = {vc.r / 255.0f, vc.g / 255.0f, vc.b / 255.0f, vc.a / 255.0f};
                writeVec(pDst, colorOffset, col, std::min<int>(program.m_ColorComponents, 4));
            }
            if (normalOffset != 0xFFFFFFFFu)
            {
                const float nrm[4] = {mesh.normals ? mesh.normals[vi * 3 + 0] : 0.0f,
                                      mesh.normals ? mesh.normals[vi * 3 + 1] : 1.0f,
                                      mesh.normals ? mesh.normals[vi * 3 + 2] : 0.0f, 1.0f};
                writeVec(pDst, normalOffset, nrm, std::min<int>(program.m_NormalComponents, 4));
            }
            if (tangentOffset != 0xFFFFFFFFu)
            {
                const float tx = mesh.tangents ? mesh.tangents[vi * 3 + 0] : 1.0f;
                const float ty = mesh.tangents ? mesh.tangents[vi * 3 + 1] : 0.0f;
                const float tz = mesh.tangents ? mesh.tangents[vi * 3 + 2] : 0.0f;
                const float tanData[4] = {tx, ty, tz, 1.0f};
                writeVec(pDst, tangentOffset, tanData, std::min<int>(program.m_TangentComponents, 4));
            }
        }

        StaticGpuMesh gpuMesh{};
        gpuMesh.m_VertexCount = static_cast<UINT>(mesh.vertexCount);
        gpuMesh.m_StrideBytes = program.m_StrideBytes;

        D3D11_BUFFER_DESC vbDesc{};
        vbDesc.ByteWidth = static_cast<UINT>(vVertexData.size() * sizeof(float));
        vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vbInit{};
        vbInit.pSysMem = vVertexData.data();
        if (FAILED(m_Device.Get()->CreateBuffer(&vbDesc, &vbInit, &gpuMesh.m_VertexBuffer)))
        {
            return nullptr;
        }

        if (hasIndices)
        {
            std::vector<uint32_t> vIndices;
            vIndices.reserve(static_cast<size_t>(mesh.triangleCount) * 3u);
            for (int tri = 0; tri < mesh.triangleCount; ++tri)
            {
                const int i0 = static_cast<int>(mesh.indices[tri * 3 + 0]);
                const int i1 = static_cast<int>(mesh.indices[tri * 3 + 1]);
                const int i2 = static_cast<int>(mesh.indices[tri * 3 + 2]);
                if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= mesh.vertexCount || i1 >= mesh.vertexCount ||
                    i2 >= mesh.vertexCount)
                {
                    continue;
                }
                vIndices.push_back(static_cast<uint32_t>(i0));
                vIndices.push_back(static_cast<uint32_t>(i1));
                vIndices.push_back(static_cast<uint32_t>(i2));
            }

            if (!vIndices.empty())
            {
                D3D11_BUFFER_DESC ibDesc{};
                ibDesc.ByteWidth = static_cast<UINT>(vIndices.size() * sizeof(uint32_t));
                ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
                ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
                D3D11_SUBRESOURCE_DATA ibInit{};
                ibInit.pSysMem = vIndices.data();
                if (SUCCEEDED(m_Device.Get()->CreateBuffer(&ibDesc, &ibInit, &gpuMesh.m_IndexBuffer)))
                {
                    gpuMesh.m_IndexCount = static_cast<UINT>(vIndices.size());
                    gpuMesh.m_Indexed = true;
                }
            }
        }

        auto [insertedIt, inserted] = m_StaticGpuMeshes.emplace(key, std::move(gpuMesh));
        m_vStaticMeshKeysByVertices[mesh.vertices].push_back(key);
        return &insertedIt->second;
    }

    void QuarkD3D11Renderer::DrawMeshInstancedWithShader(const Mesh& mesh, const Material& material,
                                                         const Mat4* pTransforms, int instances,
                                                         ShaderProgramData& program)
    {
        if (!pTransforms || instances <= 0 || !mesh.vertices || mesh.vertexCount <= 0 ||
            program.m_WorldPositionOffset != 0xFFFFFFFFu)
        {
            for (int i = 0; i < instances; ++i)
            {
                DrawMeshWithShader(mesh, material, pTransforms[i], program);
            }
            return;
        }

        StaticGpuMesh* pGpuMesh = GetOrCreateStaticGpuMesh(mesh, program);
        if (!pGpuMesh)
        {
            for (int i = 0; i < instances; ++i)
            {
                DrawMeshWithShader(mesh, material, pTransforms[i], program);
            }
            return;
        }

        const MaterialMap* pAlbedoMap = (material.maps != nullptr) ? &material.maps[MATERIAL_MAP_ALBEDO] : nullptr;
        ID3D11ShaderResourceView* pAlbedoResource = nullptr;
        if (pAlbedoMap && (pAlbedoMap->texture.valid || pAlbedoMap->texture.id != 0))
        {
            pAlbedoResource = m_Resources.ShaderResource(pAlbedoMap->texture.id);
        }
        const uint32_t shaderId =
            (material.shader && material.shader->id != 0) ? material.shader->id : m_CurrentShaderId;
        const Color baseColor = pAlbedoMap ? pAlbedoMap->color : WHITE;

        if (program.m_LocView >= 0)
        {
            StoreUniformValue(program, program.m_LocView, 0, m_ViewMatrix.m, sizeof(float) * 16, 1);
        }
        if (program.m_LocProjection >= 0)
        {
            StoreUniformValue(program, program.m_LocProjection, 0, m_ProjectionMatrix.m, sizeof(float) * 16, 1);
        }

        const float diffuseColor[4] = {baseColor.r / 255.0f, baseColor.g / 255.0f, baseColor.b / 255.0f,
                                       baseColor.a / 255.0f};
        if (program.m_LocColor >= 0)
        {
            StoreUniformValue(program, program.m_LocColor, 0, diffuseColor, sizeof(float) * 4, 1);
        }

        const bool canGpuInstance =
            (instances > 1 && program.m_LocUseInstancing >= 0 && program.m_LocInstanceBase >= 0);
        UINT gpuInstanceStart = 0xFFFFFFFFu;
        if (canGpuInstance)
        {
            const Mat4 viewProj = m_ProjectionMatrix * m_ViewMatrix;
            m_vGpuInstancesScratch.resize(static_cast<size_t>(instances));
            for (int i = 0; i < instances; ++i)
            {
                const Mat4 finalTransform = m_CurrentMatrix * pTransforms[i];
                const Mat4 normalMatrix = TransposeMat4(finalTransform.inverted());
                const Mat4 mvp = viewProj * finalTransform;
                std::memcpy(m_vGpuInstancesScratch[static_cast<size_t>(i)].m_aModel, finalTransform.m,
                            sizeof(float) * 16);
                std::memcpy(m_vGpuInstancesScratch[static_cast<size_t>(i)].m_aMvp, mvp.m, sizeof(float) * 16);
                std::memcpy(m_vGpuInstancesScratch[static_cast<size_t>(i)].m_aNormalMatrix, normalMatrix.m,
                            sizeof(float) * 16);
            }
            gpuInstanceStart =
                m_Commands.Append3DInstances(m_vGpuInstancesScratch.data(), static_cast<UINT>(instances));
        }

        if (gpuInstanceStart != 0xFFFFFFFFu)
        {
            const int one = 1;
            const int baseIdx = static_cast<int>(gpuInstanceStart);
            StoreUniformValue(program, program.m_LocUseInstancing, 0, &one, sizeof(int), 1);
            StoreUniformValue(program, program.m_LocInstanceBase, 0, &baseIdx, sizeof(int), 1);
        }
        else if (program.m_LocUseInstancing >= 0)
        {
            const int zero = 0;
            StoreUniformValue(program, program.m_LocUseInstancing, 0, &zero, sizeof(int), 1);
        }

        D3D11CommandContext::ShaderOverride shaderOverride = BuildShaderOverride(shaderId, pAlbedoResource, false);
        if (!shaderOverride.Active())
        {
            return;
        }

        if (material.maps)
        {
            ID3D11ShaderResourceView* pWhiteFallback =
                m_WhiteShaderTexture.IsValid() ? m_Resources.ShaderResource(m_WhiteShaderTexture.m_Id) : nullptr;
            ID3D11ShaderResourceView* pBlackFallback =
                m_BlackShaderTexture.IsValid() ? m_Resources.ShaderResource(m_BlackShaderTexture.m_Id) : nullptr;
            ID3D11ShaderResourceView* pFlatNormalFallback =
                m_FlatNormalShaderTexture.IsValid() ? m_Resources.ShaderResource(m_FlatNormalShaderTexture.m_Id)
                                                    : nullptr;

            const auto fillMapSlot = [&](UINT slot, int mapIndex, ID3D11ShaderResourceView* fallback)
            {
                if (shaderOverride.m_apShaderResources[slot] != nullptr)
                    return;
                const MaterialMap& map = material.maps[mapIndex];
                if (map.texture.valid || map.texture.id != 0)
                {
                    shaderOverride.m_apShaderResources[slot] = m_Resources.ShaderResource(map.texture.id);
                }
                else if (fallback)
                {
                    shaderOverride.m_apShaderResources[slot] = fallback;
                }
            };

            fillMapSlot(1, MATERIAL_MAP_HEIGHT + 0, pWhiteFallback);
            fillMapSlot(2, MATERIAL_MAP_HEIGHT + 1, pWhiteFallback);
            fillMapSlot(3, MATERIAL_MAP_HEIGHT + 2, pWhiteFallback);
            fillMapSlot(4, MATERIAL_MAP_HEIGHT + 3, pWhiteFallback);
            fillMapSlot(5, MATERIAL_MAP_METALNESS, pWhiteFallback);
            fillMapSlot(6, MATERIAL_MAP_NORMAL, pFlatNormalFallback);
            fillMapSlot(7, MATERIAL_MAP_ROUGHNESS, pWhiteFallback);
            fillMapSlot(8, MATERIAL_MAP_OCCLUSION, pWhiteFallback);
            fillMapSlot(9, MATERIAL_MAP_EMISSION, pBlackFallback);
        }

        if (gpuInstanceStart != 0xFFFFFFFFu)
        {
            m_Commands.SubmitQueued3DStaticMesh(pGpuMesh->m_VertexBuffer.Get(), pGpuMesh->m_IndexBuffer.Get(),
                                                pGpuMesh->m_VertexCount, pGpuMesh->m_IndexCount, pGpuMesh->m_Indexed,
                                                shaderOverride, static_cast<UINT>(instances), nullptr, 0, true);
            return;
        }

        const uint32_t singleConstantBytes = static_cast<uint32_t>(program.m_vConstantStaging.size());
        m_vInstancedConstantsScratch.clear();
        m_vInstancedConstantsScratch.reserve(static_cast<size_t>(singleConstantBytes) * static_cast<size_t>(instances));

        const Mat4 viewProj = m_ProjectionMatrix * m_ViewMatrix;

        for (int i = 0; i < instances; ++i)
        {
            const Mat4 finalTransform = m_CurrentMatrix * pTransforms[i];
            const Mat4 normalMatrix = TransposeMat4(finalTransform.inverted());
            const Mat4 mvp = viewProj * finalTransform;
            if (program.m_LocModel >= 0)
            {
                StoreUniformValue(program, program.m_LocModel, 0, finalTransform.m, sizeof(float) * 16, 1);
            }
            if (program.m_LocMvp >= 0)
            {
                StoreUniformValue(program, program.m_LocMvp, 0, mvp.m, sizeof(float) * 16, 1);
            }
            if (program.m_LocNormal >= 0)
            {
                StoreUniformValue(program, program.m_LocNormal, 0, normalMatrix.m, sizeof(float) * 16, 1);
            }
            if (singleConstantBytes > 0)
            {
                m_vInstancedConstantsScratch.insert(m_vInstancedConstantsScratch.end(),
                                                    program.m_vConstantStaging.begin(),
                                                    program.m_vConstantStaging.end());
            }
        }

        m_Commands.SubmitQueued3DStaticMesh(
            pGpuMesh->m_VertexBuffer.Get(), pGpuMesh->m_IndexBuffer.Get(), pGpuMesh->m_VertexCount,
            pGpuMesh->m_IndexCount, pGpuMesh->m_Indexed, shaderOverride, static_cast<UINT>(instances),
            m_vInstancedConstantsScratch.empty() ? nullptr : m_vInstancedConstantsScratch.data(), singleConstantBytes,
            false);
    }

    void QuarkD3D11Renderer::DrawMeshInstanced(const Mesh& mesh, const Material& material, const Mat4* pTransforms,
                                               int instances)
    {
        if (!pTransforms || instances <= 0)
        {
            return;
        }

        if (ShaderProgramData* program = Resolve3DShaderProgram(material))
        {
            DrawMeshInstancedWithShader(mesh, material, pTransforms, instances, *program);
            return;
        }

        for (int instanceIndex = 0; instanceIndex < instances; ++instanceIndex)
        {
            DrawMesh(mesh, material, pTransforms[instanceIndex]);
        }
    }

    namespace
    {
        std::vector<int> DefaultCodepoints()
        {
            std::vector<int> vCps;
            vCps.reserve(96);
            for (int c = 32; c <= 126; ++c)
                vCps.push_back(c);
            return vCps;
        }

        int DecodeUTF8(const char*& p)
        {
            const unsigned char lead = static_cast<unsigned char>(*p);
            int cp = 0, seq = 0;
            if ((lead & 0x80) == 0)
            {
                cp = lead;
                seq = 1;
            }
            else if ((lead & 0xE0) == 0xC0)
            {
                cp = lead & 0x1F;
                seq = 2;
            }
            else if ((lead & 0xF0) == 0xE0)
            {
                cp = lead & 0x0F;
                seq = 3;
            }
            else if ((lead & 0xF8) == 0xF0)
            {
                cp = lead & 0x07;
                seq = 4;
            }
            else
            {
                cp = lead;
                seq = 1;
            }
            for (int k = 1; k < seq && p[k] != '\0'; ++k)
                cp = (cp << 6) | (static_cast<unsigned char>(p[k]) & 0x3F);
            p += seq;
            return cp;
        }
    } // namespace

    bool QuarkD3D11Renderer::LoadFontData(const char* pFilePath, const unsigned char* pFileData, int dataSize,
                                          int fontSize, const int* pCodepoints, int codepointCount, FontData& fontData)
    {
        TraceLog(LogLevel::Trace, "FONT",
                 TextFormat("[D3D11] FreeType initializing font: %s (size: %d px)", pFilePath ? pFilePath : "<memory>",
                            fontSize));

        if ((!pFilePath && !pFileData) || fontSize <= 0)
        {
            TraceLog(LogLevel::Error, "FONT", "[D3D11] Invalid font source or point size.");
            return false;
        }

        FT_Library library = nullptr;
        if (FT_Init_FreeType(&library) != 0)
        {
            TraceLog(LogLevel::Error, "FONT", "[D3D11] Failed to initialize FreeType library.");
            return false;
        }

        bool syntheticItalic = false;
        std::string cleanFilePath = pFilePath ? pFilePath : "";
        const std::string italicTag = "|italic";
        if (cleanFilePath.size() > italicTag.size() &&
            cleanFilePath.compare(cleanFilePath.size() - italicTag.size(), italicTag.size(), italicTag) == 0)
        {
            syntheticItalic = true;
            cleanFilePath.erase(cleanFilePath.size() - italicTag.size());
        }

        FT_Face face = nullptr;
        if (pFileData != nullptr)
        {
            if (FT_New_Memory_Face(library, pFileData, static_cast<FT_Long>(dataSize), 0, &face) != 0)
            {
                TraceLog(LogLevel::Error, "FONT", "[D3D11] Failed to open font from memory.");
                FT_Done_FreeType(library);
                return false;
            }
        }
        else if (FT_New_Face(library, cleanFilePath.c_str(), 0, &face) != 0)
        {
            TraceLog(LogLevel::Error, "FONT",
                     TextFormat("[D3D11] Failed to open font file: %s", cleanFilePath.c_str()));
            FT_Done_FreeType(library);
            return false;
        }

        FT_Select_Charmap(face, FT_ENCODING_UNICODE);
        FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(fontSize));
        if (syntheticItalic)
        {
            FT_Matrix italicMatrix;
            italicMatrix.xx = 0x10000L;
            italicMatrix.xy = 0x0366AL;
            italicMatrix.yx = 0;
            italicMatrix.yy = 0x10000L;
            FT_Set_Transform(face, &italicMatrix, nullptr);
        }

        std::vector<int> vCps;
        const int* pCpsPtr = pCodepoints;
        int cpsCount = codepointCount;
        if (pCpsPtr == nullptr || cpsCount <= 0)
        {
            vCps = DefaultCodepoints();
            pCpsPtr = vCps.data();
            cpsCount = static_cast<int>(vCps.size());
        }

        constexpr int atlasWidth = 1024;
        constexpr int atlasHeight = 1024;
        std::vector<uint8_t> vAtlas(static_cast<size_t>(atlasWidth) * atlasHeight * 4, 0);
        int penX = 1;
        int penY = 1;
        int rowHeight = 0;
        int renderedGlyphs = 0;
        fontData.m_vGlyphs.clear();
        fontData.m_vGlyphs.reserve(static_cast<size_t>(cpsCount));

        for (int i = 0; i < cpsCount; ++i)
        {
            const int cp = pCpsPtr[i];
            if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0)
            {
                continue;
            }

            FT_GlyphSlot glyph = face->glyph;
            const int glyphWidth = static_cast<int>(glyph->bitmap.width);
            const int glyphHeight = static_cast<int>(glyph->bitmap.rows);

            if (penX + glyphWidth + 1 > atlasWidth)
            {
                penX = 1;
                penY += rowHeight + 1;
                rowHeight = 0;
            }

            if (penY + glyphHeight + 1 > atlasHeight)
            {
                TraceLog(LogLevel::Error, "FONT", "[D3D11] Font atlas capacity exceeded.");
                FT_Done_Face(face);
                FT_Done_FreeType(library);
                return false;
            }

            for (int row = 0; row < glyphHeight; ++row)
            {
                for (int column = 0; column < glyphWidth; ++column)
                {
                    const size_t destination = (static_cast<size_t>(penY + row) * atlasWidth + penX + column) * 4;
                    vAtlas[destination] = 255;
                    vAtlas[destination + 1] = 255;
                    vAtlas[destination + 2] = 255;
                    vAtlas[destination + 3] = glyph->bitmap.buffer[row * glyph->bitmap.pitch + column];
                }
            }

            GlyphData data;
            data.m_Value = cp;
            data.m_Uv = {static_cast<float>(penX) / atlasWidth, static_cast<float>(penY) / atlasHeight,
                         static_cast<float>(glyphWidth) / atlasWidth, static_cast<float>(glyphHeight) / atlasHeight};
            data.m_Rec = {static_cast<float>(penX), static_cast<float>(penY), static_cast<float>(glyphWidth),
                          static_cast<float>(glyphHeight)};
            data.m_AdvanceX = static_cast<float>(glyph->advance.x) / 64.0f;
            data.m_OffsetX = static_cast<float>(glyph->bitmap_left);
            data.m_OffsetY = static_cast<float>(glyph->bitmap_top);
            data.m_Width = glyphWidth;
            data.m_Height = glyphHeight;

            if (glyphWidth > 0 && glyphHeight > 0)
            {
                unsigned char* pGdata =
                    static_cast<unsigned char*>(std::malloc(static_cast<size_t>(glyphWidth) * glyphHeight * 4));
                if (pGdata)
                {
                    for (int row = 0; row < glyphHeight; ++row)
                    {
                        for (int column = 0; column < glyphWidth; ++column)
                        {
                            const unsigned char alpha = glyph->bitmap.buffer[row * glyph->bitmap.pitch + column];
                            pGdata[(static_cast<size_t>(row) * glyphWidth + column) * 4 + 0] = 255;
                            pGdata[(static_cast<size_t>(row) * glyphWidth + column) * 4 + 1] = 255;
                            pGdata[(static_cast<size_t>(row) * glyphWidth + column) * 4 + 2] = 255;
                            pGdata[(static_cast<size_t>(row) * glyphWidth + column) * 4 + 3] = alpha;
                        }
                    }
                    data.m_Image = Image{pGdata, glyphWidth, glyphHeight, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                }
            }

            penX += glyphWidth + 1;
            rowHeight = std::max(rowHeight, glyphHeight);
            ++renderedGlyphs;
            fontData.m_vGlyphs.push_back(data);
        }

        fontData.m_AtlasTexture = m_Resources.CreateTexture(m_Device.Get(), vAtlas.data(), atlasWidth, atlasHeight);
        fontData.m_BaseSize = fontSize;
        fontData.m_GlyphCount = renderedGlyphs;
        fontData.m_Ascent = static_cast<int>(face->size->metrics.ascender / 64);
        fontData.m_Descent = static_cast<int>(face->size->metrics.descender / 64);
        fontData.m_LineHeight = static_cast<int>(face->size->metrics.height / 64);

        TraceLog(LogLevel::Info, "FONT",
                 TextFormat("[D3D11] Font rasterized: %s (%d glyphs, Atlas: %dx%d, Ascent: %d, "
                            "Descent: %d, LineHeight: %d)",
                            pFilePath ? pFilePath : "<memory>", renderedGlyphs, atlasWidth, atlasHeight,
                            fontData.m_Ascent, fontData.m_Descent, fontData.m_LineHeight));

        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return fontData.m_AtlasTexture.IsValid();
    }

    int QuarkD3D11Renderer::FindGlyph(const FontData& fontData, int codepoint)
    {
        for (int i = 0; i < static_cast<int>(fontData.m_vGlyphs.size()); ++i)
        {
            if (fontData.m_vGlyphs[i].m_Value == codepoint)
                return i;
        }
        for (int i = 0; i < static_cast<int>(fontData.m_vGlyphs.size()); ++i)
        {
            if (fontData.m_vGlyphs[i].m_Value == 63)
                return i;
        }
        return -1;
    }

    const QuarkD3D11Renderer::FontData* QuarkD3D11Renderer::GetFontData(IFont font) const
    {
        const auto iterator = m_Fonts.find(font.m_Id);
        return iterator == m_Fonts.end() ? nullptr : &iterator->second;
    }

    uint32_t QuarkD3D11Renderer::EnsureDefaultFont()
    {
        if (m_DefaultFontId != 0)
        {
            return m_DefaultFontId;
        }

        FontData fontData{};
        if (!LoadFontData(nullptr, pixel_ttf, static_cast<int>(pixel_ttf_len), 32, nullptr, 0, fontData))
        {
            return 0;
        }

        m_DefaultFontId = m_NextFontId++;
        m_Fonts.emplace(m_DefaultFontId, std::move(fontData));
        TraceLog(LogLevel::Info, "FONT", TextFormat("[D3D11] Default font loaded (Font ID: %u)", m_DefaultFontId));
        return m_DefaultFontId;
    }

    void QuarkD3D11Renderer::DrawTextWithFontData(const FontData& fontData, const char* pText, Vec2 position,
                                                  float fontSize, float spacing, Color tint)
    {
        if (!pText || fontData.m_BaseSize <= 0 || fontSize <= 0.0f)
        {
            return;
        }

        const float scale = fontSize / fontData.m_BaseSize;
        const float baseline = fontData.m_Ascent * scale;
        const float lineHeight = fontData.m_LineHeight * scale;
        float cursorX = position.x;
        float cursorY = position.y;
        bool firstGlyph = true;

        for (const char* character = pText; *character != '\0';)
        {
            if (*character == '\n')
            {
                cursorX = position.x;
                cursorY += lineHeight;
                firstGlyph = true;
                ++character;
                continue;
            }

            const int cp = DecodeUTF8(character);
            const int idx = FindGlyph(fontData, cp);
            if (idx < 0)
            {
                continue;
            }

            const GlyphData& glyph = fontData.m_vGlyphs[static_cast<size_t>(idx)];
            if (!firstGlyph)
            {
                cursorX += spacing;
            }
            firstGlyph = false;

            const float glyphX = cursorX + glyph.m_OffsetX * scale;
            const float glyphY = cursorY + baseline - glyph.m_OffsetY * scale;
            const float glyphWidth = glyph.m_Width * scale;
            const float glyphHeight = glyph.m_Height * scale;

            if (glyphWidth > 0.0f && glyphHeight > 0.0f)
            {
                const Rectangle sourcePixels{glyph.m_Uv.x * fontData.m_AtlasTexture.m_Width,
                                             glyph.m_Uv.y * fontData.m_AtlasTexture.m_Height,
                                             glyph.m_Uv.width * fontData.m_AtlasTexture.m_Width,
                                             glyph.m_Uv.height * fontData.m_AtlasTexture.m_Height};

                m_Commands.DrawTextureQuad(fontData.m_AtlasTexture, sourcePixels,
                                           {glyphX, glyphY, glyphWidth, glyphHeight}, {0.0f, 0.0f}, 0.0f, tint, m_Width,
                                           m_Height);
            }

            cursorX += glyph.m_AdvanceX * scale;
        }
    }

    Vec2 QuarkD3D11Renderer::MeasureTextWithFontData(const FontData& fontData, const char* pText, float fontSize,
                                                     float spacing) const
    {
        if (!pText || fontData.m_BaseSize <= 0 || fontSize <= 0.0f)
        {
            return {};
        }

        const float scale = fontSize / fontData.m_BaseSize;
        const float lineHeight = fontData.m_LineHeight * scale;
        float width = 0.0f;
        float maximumWidth = 0.0f;
        bool firstGlyph = true;
        int lineCount = 1;

        for (const char* character = pText; *character != '\0';)
        {
            if (*character == '\n')
            {
                maximumWidth = std::max(maximumWidth, width);
                width = 0.0f;
                firstGlyph = true;
                ++lineCount;
                ++character;
                continue;
            }

            const int cp = DecodeUTF8(character);
            const int idx = FindGlyph(fontData, cp);
            if (idx < 0)
            {
                continue;
            }

            const GlyphData& glyph = fontData.m_vGlyphs[static_cast<size_t>(idx)];
            if (!firstGlyph)
            {
                width += spacing;
            }
            firstGlyph = false;
            width += glyph.m_AdvanceX * scale;
        }

        return {std::max(maximumWidth, width), lineHeight * lineCount};
    }

    IFont QuarkD3D11Renderer::LoadFont(const char* pFilePath, int fontSize, const int* pCodepoints, int codepointCount)
    {
        if (!pFilePath)
        {
            return IFont{EnsureDefaultFont()};
        }

        FontData fontData{};
        if (!LoadFontData(pFilePath, nullptr, 0, fontSize, pCodepoints, codepointCount, fontData))
        {
            TraceLog(LogLevel::Error, "FONT", TextFormat("[D3D11] Failed to load font: %s", pFilePath));
            return {};
        }

        const uint32_t id = m_NextFontId++;
        m_Fonts.emplace(id, std::move(fontData));
        TraceLog(LogLevel::Info, "FONT",
                 TextFormat("[D3D11] Font loaded successfully: %s (Font ID: %u)", pFilePath, id));
        return IFont{id};
    }

    IFont QuarkD3D11Renderer::LoadFontFromMemory(const char* pFileType, const unsigned char* pFileData, int dataSize,
                                                 int fontSize, const int* pCodepoints, int codepointCount)
    {
        if (!pFileData || dataSize <= 0)
        {
            TraceLog(LogLevel::Error, "FONT", "[D3D11] LoadFontFromMemory: invalid memory buffer");
            return {};
        }

        FontData fontData{};
        if (!LoadFontData(nullptr, pFileData, dataSize, fontSize, pCodepoints, codepointCount, fontData))
        {
            TraceLog(LogLevel::Error, "FONT", "[D3D11] Failed to load font from memory");
            return {};
        }

        const uint32_t id = m_NextFontId++;
        m_Fonts.emplace(id, std::move(fontData));
        TraceLog(
            LogLevel::Info, "FONT",
            TextFormat("[D3D11] Font loaded from memory (type: %s, Font ID: %u)", pFileType ? pFileType : ".ttf", id));
        return IFont{id};
    }

    void QuarkD3D11Renderer::UnloadFont(IFont& font)
    {
        const auto iterator = m_Fonts.find(font.m_Id);
        if (iterator != m_Fonts.end())
        {
            m_Resources.DestroyTexture(iterator->second.m_AtlasTexture.m_Id);

            for (GlyphData& g : iterator->second.m_vGlyphs)
            {
                std::free(g.m_Image.data);
                g.m_Image = Image{};
            }

            TraceLog(LogLevel::Info, "FONT",
                     TextFormat("[D3D11] Font unloaded (Font ID: %u, Atlas ID: %u)", font.m_Id,
                                iterator->second.m_AtlasTexture.m_Id));
            m_Fonts.erase(iterator);
        }

        if (font.m_Id == m_DefaultFontId)
        {
            m_DefaultFontId = 0;
        }
        font = {};
    }

    void QuarkD3D11Renderer::FillFont(IFont font, Font& out)
    {
        const FontData* pFd = GetFontData(font);
        if (!pFd)
        {
            const uint32_t id = EnsureDefaultFont();
            pFd = (id != 0) ? GetFontData(IFont{id}) : nullptr;
        }
        if (!pFd)
        {
            out.valid = false;
            return;
        }

        out.baseSize = pFd->m_BaseSize;
        out.glyphCount = pFd->m_GlyphCount;
        out.glyphPadding = 2;
        out.valid = true;
        out._rendererFontId = font.m_Id;
        out.texture = Texture2D{pFd->m_AtlasTexture.m_Id,          pFd->m_AtlasTexture.m_Width,
                                pFd->m_AtlasTexture.m_Height,      1,
                                PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, true};

        delete[] out.recs;
        delete[] out.glyphs;
        out.recs = new Rectangle[pFd->m_GlyphCount];
        out.glyphs = new GlyphInfo[pFd->m_GlyphCount];

        for (int i = 0; i < pFd->m_GlyphCount; ++i)
        {
            const GlyphData& g = pFd->m_vGlyphs[static_cast<size_t>(i)];
            out.recs[i] = g.m_Rec;
            out.glyphs[i].value = g.m_Value;
            out.glyphs[i].offsetX = static_cast<int>(g.m_OffsetX);
            out.glyphs[i].offsetY = static_cast<int>(g.m_OffsetY);
            out.glyphs[i].advanceX = static_cast<int>(g.m_AdvanceX);
            out.glyphs[i].image = g.m_Image;
        }
    }

    void QuarkD3D11Renderer::DrawDebugText(const char* pText, int x, int y, int fontSize, Color color)
    {
        if (!pText || !*pText || fontSize <= 0)
        {
            return;
        }

        static std::unordered_map<int, IFont> s_debugFonts;
        auto it = s_debugFonts.find(fontSize);
        if (it == s_debugFonts.end())
        {
            IFont font = LoadFontFromMemory("ttf", tahoma_ttf, static_cast<int>(tahoma_ttf_len), fontSize, nullptr, 0);
            if (!font.IsValid())
            {
                return;
            }
            it = s_debugFonts.emplace(fontSize, font).first;
        }

        const Color shadow = {0, 0, 0, 255};
        const std::array<Vec2, 9> offsets = {Vec2{-1.f, -1.f}, Vec2{0.f, -1.f}, Vec2{1.f, -1.f},
                                             Vec2{-1.f, 0.f},  Vec2{0.f, 0.f},  Vec2{1.f, 0.f},
                                             Vec2{-1.f, 1.f},  Vec2{0.f, 1.f},  Vec2{1.f, 1.f}};

        m_Commands.SetDrawingDebugOverlay(true);

        for (const Vec2& delta : offsets)
        {
            if (delta.x == 0.f && delta.y == 0.f)
            {
                continue;
            }
            DrawTextEx(it->second, pText, Vec2{static_cast<float>(x) + delta.x, static_cast<float>(y) + delta.y},
                       static_cast<float>(fontSize), 0.0f, shadow);
        }

        DrawTextEx(it->second, pText, Vec2{static_cast<float>(x) + 1.f, static_cast<float>(y) + 1.f},
                   static_cast<float>(fontSize), 0.0f, shadow);
        DrawTextEx(it->second, pText, Vec2{static_cast<float>(x), static_cast<float>(y)}, static_cast<float>(fontSize),
                   0.0f, color);
        m_Commands.SetDrawingDebugOverlay(false);
    }

    void QuarkD3D11Renderer::DrawText(const char* pText, int x, int y, int fontSize, Color color)
    {
        const uint32_t fontId = EnsureDefaultFont();
        const FontData* pFontData = GetFontData(IFont{fontId});
        if (pFontData)
        {
            DrawTextWithFontData(*pFontData, pText, {static_cast<float>(x), static_cast<float>(y)},
                                 static_cast<float>(fontSize), 0.0f, color);
            m_Commands.SetDrawingDebugOverlay(false);
        }
    }

    void QuarkD3D11Renderer::DrawTextEx(IFont font, const char* pText, Vec2 position, float fontSize, float spacing,
                                        Color tint)
    {
        const FontData* pFontData = GetFontData(font);
        if (pFontData)
        {
            DrawTextWithFontData(*pFontData, pText, position, fontSize, spacing, tint);
        }
    }

    Vec2 QuarkD3D11Renderer::MeasureTextEx(IFont font, const char* pText, float fontSize, float spacing)
    {
        const FontData* pFontData = GetFontData(font);
        return pFontData ? MeasureTextWithFontData(*pFontData, pText, fontSize, spacing) : Vec2{};
    }

    int QuarkD3D11Renderer::MeasureText(const char* pText, int fontSize)
    {
        const uint32_t fontId = EnsureDefaultFont();
        const FontData* pFontData = GetFontData(IFont{fontId});
        return pFontData ? static_cast<int>(std::round(
                               MeasureTextWithFontData(*pFontData, pText, static_cast<float>(fontSize), 0.0f).x))
                         : 0;
    }

    ITexture QuarkD3D11Renderer::LoadTexture(const char* pFilePath)
    {
        TraceLog(LogLevel::Trace, "TEXTURE",
                 TextFormat("[D3D11] Loading texture from: %s", pFilePath ? pFilePath : "<null>"));

        if (!pFilePath)
        {
            return ITexture{};
        }

        std::error_code pathError;
        std::string cacheKey = std::filesystem::weakly_canonical(pFilePath, pathError).string();
        if (pathError || cacheKey.empty())
        {
            cacheKey = std::filesystem::path(pFilePath).lexically_normal().string();
        }

        const auto cached = m_TextureCache.find(cacheKey);
        if (cached != m_TextureCache.end())
        {
            cached->second.m_References++;
            TraceLog(LogLevel::Trace, "TEXTURE",
                     TextFormat("[D3D11] Reusing cached texture: %s (ID: %u, References: %d)", pFilePath,
                                cached->second.m_Texture.m_Id, cached->second.m_References));
            return cached->second.m_Texture;
        }

        ImageFileData image;
        ITexture texture{};

        if (!LoadImageFile(pFilePath, image, 4))
        {
            TraceLog(LogLevel::Error, "TEXTURE",
                     TextFormat("[D3D11] Failed to load texture: %s", pFilePath ? pFilePath : "<null>"));
            return texture;
        }

        texture = m_Resources.CreateTexture(m_Device.Get(), image.m_vPixels.data(), image.m_Width, image.m_Height);

        if (texture.IsValid())
        {
            m_TextureCache.emplace(cacheKey, CachedTexture{texture, 1});
            m_TextureCacheKeys.emplace(texture.m_Id, cacheKey);
            TraceLog(LogLevel::Info, "TEXTURE",
                     TextFormat("[D3D11] Texture loaded successfully: %s (%dx%d, %zu bytes, ID: %u)",
                                pFilePath ? pFilePath : "<null>", texture.m_Width, texture.m_Height,
                                image.m_vPixels.size(), texture.m_Id));
        }
        else
        {
            TraceLog(LogLevel::Error, "TEXTURE",
                     TextFormat("[D3D11] Failed to upload texture: %s", pFilePath ? pFilePath : "<null>"));
        }

        return texture;
    }

    ITexture QuarkD3D11Renderer::LoadTextureFromImage(const Image& image)
    {
        TraceLog(LogLevel::Trace, "TEXTURE",
                 TextFormat("[D3D11] Uploading texture from Image (%dx%d, format %d)", image.width, image.height,
                            image.format));
        if (!image.data || image.width <= 0 || image.height <= 0 || image.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
        {
            TraceLog(LogLevel::Error, "TEXTURE",
                     "[D3D11] LoadTextureFromImage: unsupported or invalid image (requires UNCOMPRESSED_R8G8B8A8)");
            return ITexture{};
        }

        ITexture texture = m_Resources.CreateTexture(m_Device.Get(), static_cast<const uint8_t*>(image.data),
                                                     image.width, image.height);
        if (!texture.IsValid())
        {
            TraceLog(LogLevel::Error, "TEXTURE", "[D3D11] LoadTextureFromImage: upload failed");
            return texture;
        }

        TraceLog(
            LogLevel::Info, "TEXTURE",
            TextFormat("[D3D11] Texture uploaded from Image (%dx%d, ID: %u)", image.width, image.height, texture.m_Id));
        return texture;
    }

    ITexture QuarkD3D11Renderer::LoadTextureCubemap(const unsigned char* pRgbaFaces, int faceSize)
    {
        ITexture texture = m_Resources.CreateCubemap(m_Device.Get(), pRgbaFaces, faceSize);
        if (!texture.IsValid())
        {
            TraceLog(LogLevel::Error, "TEXTURE", "[D3D11] LoadTextureCubemap: upload failed");
        }
        return texture;
    }

    ITexture QuarkD3D11Renderer::GetRenderTextureTexture(IRenderTexture target)
    {
        return target.m_Texture;
    }

    void QuarkD3D11Renderer::UnloadTexture(ITexture& texture)
    {
        if (texture.m_Id != 0)
        {
            const auto cacheKey = m_TextureCacheKeys.find(texture.m_Id);
            if (cacheKey != m_TextureCacheKeys.end())
            {
                const auto cached = m_TextureCache.find(cacheKey->second);
                if (cached != m_TextureCache.end())
                {
                    cached->second.m_References--;
                    if (cached->second.m_References > 0)
                    {
                        TraceLog(LogLevel::Trace, "TEXTURE",
                                 TextFormat("[D3D11] Released cached texture (ID: %u, References: %d)", texture.m_Id,
                                            cached->second.m_References));
                        texture = {};
                        return;
                    }
                    m_TextureCache.erase(cached);
                }
                m_TextureCacheKeys.erase(cacheKey);
            }

            TraceLog(LogLevel::Info, "TEXTURE",
                     TextFormat("[D3D11] Texture unloaded (ID: %u, %dx%d)", texture.m_Id, texture.m_Width,
                                texture.m_Height));
            m_Resources.DestroyTexture(texture.m_Id);
        }
        texture = {};
    }

    IRenderTexture QuarkD3D11Renderer::LoadRenderTexture(int width, int height)
    {
        TraceLog(LogLevel::Trace, "RENDER_TARGET", TextFormat("[D3D11] Creating render texture: %dx%d", width, height));

        IRenderTexture target = m_Resources.CreateRenderTexture(m_Device.Get(), width, height);
        if (isRenderTextureValid(target))
        {
            TraceLog(LogLevel::Info, "RENDER_TARGET",
                     TextFormat("[D3D11] Render texture created: %dx%d (Target ID: %u, Color Tex ID: %u)", width,
                                height, target.m_Id, target.m_Texture.m_Id));
        }

        return target;
    }

    void QuarkD3D11Renderer::UnloadRenderTexture(IRenderTexture target)
    {
        if (target.m_Id != 0)
        {
            TraceLog(LogLevel::Info, "RENDER_TARGET",
                     TextFormat("[D3D11] Render texture unloaded (Target ID: %u, Color Tex ID: %u)", target.m_Id,
                                target.m_Texture.m_Id));
            m_Resources.DestroyRenderTexture(target.m_Id);
        }
    }

    ITexture QuarkD3D11Renderer::GenCheckerTexture(int width, int height, int cellSize, Color colorA, Color colorB)
    {
        if (width <= 0 || height <= 0 || cellSize <= 0)
        {
            return {};
        }

        std::vector<uint8_t> vPixels(static_cast<size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const Color color = ((x / cellSize + y / cellSize) % 2 == 0) ? colorA : colorB;
                const size_t index = (static_cast<size_t>(y) * width + x) * 4;
                vPixels[index] = color.r;
                vPixels[index + 1] = color.g;
                vPixels[index + 2] = color.b;
                vPixels[index + 3] = color.a;
            }
        }

        ITexture texture = m_Resources.CreateTexture(m_Device.Get(), vPixels.data(), width, height);

        if (texture.IsValid())
        {
            TraceLog(LogLevel::Info, "TEXTURE",
                     TextFormat("[D3D11] Generated checker texture: %dx%d (Cell: %dpx, ID: %u)", width, height,
                                cellSize, texture.m_Id));
        }

        return texture;
    }

    bool QuarkD3D11Renderer::isTextureValid(ITexture& texture)
    {
        return texture.IsValid() && m_Resources.ShaderResource(texture.m_Id) != nullptr;
    }

    bool QuarkD3D11Renderer::isRenderTextureValid(IRenderTexture& target)
    {
        return target.m_Id != 0 && target.m_Texture.IsValid() && m_Resources.RenderTarget(target.m_Id) != nullptr;
    }

    Image QuarkD3D11Renderer::ReadTextureImage(const ITexture& texture)
    {
        if (!texture.IsValid() || texture.m_Id == 0)
            return Image{};

        m_Commands.FlushBatch();

        const size_t bytes = static_cast<size_t>(texture.m_Width) * texture.m_Height * 4;
        void* pBuf = MemAlloc(bytes);
        if (!pBuf)
            return Image{};

        if (!m_Resources.ReadPixels(m_Device.Context(), texture.m_Id, pBuf, texture.m_Width, texture.m_Height))
        {
            MemFree(pBuf);
            return Image{};
        }

        Image img{};
        img.data = pBuf;
        img.width = texture.m_Width;
        img.height = texture.m_Height;
        img.mipmaps = 1;
        img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        TraceLog(LogLevel::Info, "IMAGE",
                 TextFormat("[D3D11] Read texture pixels back to CPU: %dx%d (ID: %u)", texture.m_Width,
                            texture.m_Height, texture.m_Id));
        return img;
    }

    Image QuarkD3D11Renderer::ReadScreenImage()
    {
        if (m_Width <= 0 || m_Height <= 0)
            return Image{};

        m_Commands.FlushBatch();

        const size_t bytes = static_cast<size_t>(m_Width) * m_Height * 4;
        void* pBuf = MemAlloc(bytes);
        if (!pBuf)
            return Image{};

        if (!m_SwapChain.ReadBackBufferPixels(pBuf))
        {
            MemFree(pBuf);
            return Image{};
        }

        Image img{};
        img.data = pBuf;
        img.width = m_Width;
        img.height = m_Height;
        img.mipmaps = 1;
        img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        TraceLog(LogLevel::Info, "IMAGE",
                 TextFormat("[D3D11] Read backbuffer pixels to CPU: %dx%d", m_Width, m_Height));
        return img;
    }

    void QuarkD3D11Renderer::DrawTexture(const ITexture& texture, float x, float y, Color tint)
    {
        DrawTexturePro(texture, {0.0f, 0.0f, static_cast<float>(texture.m_Width), static_cast<float>(texture.m_Height)},
                       {x, y, static_cast<float>(texture.m_Width), static_cast<float>(texture.m_Height)}, {0.0f, 0.0f},
                       0.0f, tint);
    }

    void QuarkD3D11Renderer::DrawTextureV(const ITexture& texture, Vec2 position, Color tint)
    {
        DrawTexture(texture, position.x, position.y, tint);
    }

    void QuarkD3D11Renderer::DrawTextureEx(const ITexture& texture, Vec2 position, float rotation, float scale,
                                           Color tint)
    {
        DrawTexturePro(texture, {0.0f, 0.0f, static_cast<float>(texture.m_Width), static_cast<float>(texture.m_Height)},
                       {position.x, position.y, texture.m_Width * scale, texture.m_Height * scale},
                       {texture.m_Width * scale * 0.5f, texture.m_Height * scale * 0.5f}, rotation, tint);
    }

    void QuarkD3D11Renderer::DrawTextureRec(const ITexture& texture, Rectangle source, Vec2 position, Color tint)
    {
        DrawTexturePro(texture, source, {position.x, position.y, source.width, source.height}, {0.0f, 0.0f}, 0.0f,
                       tint);
    }

    void QuarkD3D11Renderer::DrawTexturePro(ITexture texture, Rectangle source, Rectangle destination, Vec2 origin,
                                            float rotation, Color tint)
    {
        if (isTextureValid(texture))
        {
            m_Commands.DrawTextureQuad(texture, source, destination, origin, rotation, tint, m_Width, m_Height);
        }
    }

    void QuarkD3D11Renderer::DrawTextureTiled(ITexture texture, float scale, Vec2 offset, Color tint)
    {
        if (!isTextureValid(texture) || scale <= 0.0f)
        {
            return;
        }

        const int columns = static_cast<int>(std::ceil(m_Width / (texture.m_Width * scale))) + 1;
        const int rows = static_cast<int>(std::ceil(m_Height / (texture.m_Height * scale))) + 1;

        for (int y = -1; y < rows; ++y)
        {
            for (int x = -1; x < columns; ++x)
            {
                DrawTexture(texture, offset.x + x * texture.m_Width * scale, offset.y + y * texture.m_Height * scale,
                            tint);
            }
        }
    }

    void QuarkD3D11Renderer::DrawTextureNPatch(ITexture texture, NPatchInfo np, Rectangle dst, Vec2 origin,
                                               float rotation, Color tint)
    {
        if (texture.m_Id == 0)
            return;

        const float dLeft = static_cast<float>(np.left);
        const float dTop = static_cast<float>(np.top);
        const float dRight = static_cast<float>(np.right);
        const float dBottom = static_cast<float>(np.bottom);
        const float dMiddleW = dst.width - dLeft - dRight;
        const float dMiddleH = dst.height - dTop - dBottom;

        const float sLeft = np.source.x + dLeft;
        const float sTop = np.source.y + dTop;
        const float sRight = np.source.x + np.source.width - dRight;
        const float sBottom = np.source.y + np.source.height - dBottom;

        auto patch = [&](float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh)
        {
            if (sw <= 0.f || sh <= 0.f || dw <= 0.f || dh <= 0.f)
                return;
            Rectangle src{sx, sy, sw, sh};
            Rectangle dpt{dst.x + dx, dst.y + dy, dw, dh};
            DrawTexturePro(texture, src, dpt, origin, rotation, tint);
        };

        if (np.layout == NPATCH_THREE_PATCH_HORIZONTAL)
        {
            patch(np.source.x, np.source.y, dLeft, np.source.height, 0.f, 0.f, dLeft, dst.height);
            patch(sLeft, np.source.y, np.source.width - dLeft - dRight, np.source.height, dLeft, 0.f, dMiddleW,
                  dst.height);
            patch(sRight, np.source.y, dRight, np.source.height, dLeft + dMiddleW, 0.f, dRight, dst.height);
            return;
        }

        if (np.layout == NPATCH_THREE_PATCH_VERTICAL)
        {
            patch(np.source.x, np.source.y, np.source.width, dTop, 0.f, 0.f, dst.width, dTop);
            patch(np.source.x, sTop, np.source.width, np.source.height - dTop - dBottom, 0.f, dTop, dst.width,
                  dMiddleH);
            patch(np.source.x, sBottom, np.source.width, dBottom, 0.f, dTop + dMiddleH, dst.width, dBottom);
            return;
        }

        patch(np.source.x, np.source.y, dLeft, dTop, 0.f, 0.f, dLeft, dTop);
        patch(sRight, np.source.y, dRight, dTop, dLeft + dMiddleW, 0.f, dRight, dTop);
        patch(np.source.x, sBottom, dLeft, dBottom, 0.f, dTop + dMiddleH, dLeft, dBottom);
        patch(sRight, sBottom, dRight, dBottom, dLeft + dMiddleW, dTop + dMiddleH, dRight, dBottom);

        patch(sLeft, np.source.y, np.source.width - dLeft - dRight, dTop, dLeft, 0.f, dMiddleW, dTop);
        patch(np.source.x, sTop, dLeft, np.source.height - dTop - dBottom, 0.f, dTop, dLeft, dMiddleH);
        patch(sRight, sTop, dRight, np.source.height - dTop - dBottom, dLeft + dMiddleW, dTop, dRight, dMiddleH);
        patch(sLeft, sBottom, np.source.width - dLeft - dRight, dBottom, dLeft, dTop + dMiddleH, dMiddleW, dBottom);
        patch(sLeft, sTop, np.source.width - dLeft - dRight, np.source.height - dTop - dBottom, dLeft, dTop, dMiddleW,
              dMiddleH);
    }

    void QuarkD3D11Renderer::BeginMode2D(const Camera2D& camera)
    {
        m_Commands.BeginMode2D(camera);
    }

    void QuarkD3D11Renderer::EndMode2D()
    {
        m_Commands.EndMode2D();
    }

    void QuarkD3D11Renderer::BeginTextureMode(IRenderTexture target)
    {
        m_Commands.BeginTextureMode(target);
    }

    void QuarkD3D11Renderer::EndTextureMode()
    {
        m_Commands.EndTextureMode(m_Width, m_Height);
    }

    QuarkD3D11Renderer::ShaderProgramData* QuarkD3D11Renderer::GetShaderProgram(uint32_t shaderId)
    {
        if (shaderId == 0)
        {
            return nullptr;
        }

        const auto iterator = m_ShaderPrograms.find(shaderId);
        return iterator == m_ShaderPrograms.end() ? nullptr : &iterator->second;
    }

    void QuarkD3D11Renderer::BuildShaderProgram(ShaderProgramData& program, const char* pVsSource,
                                                const char* pFsSource)
    {
        ID3D11Device* pDevice = m_Device.Get();

        const auto vertexBytecode = m_ShaderCompiler.Compile(pVsSource, "main", "vs_5_0");
        const auto pixelBytecode = m_ShaderCompiler.Compile(pFsSource, "main", "ps_5_0");

        d3d11::ThrowIfFailed(pDevice->CreateVertexShader(vertexBytecode->GetBufferPointer(),
                                                         vertexBytecode->GetBufferSize(), nullptr,
                                                         &program.m_VertexShader),
                             "ID3D11Device::CreateVertexShader");
        d3d11::ThrowIfFailed(pDevice->CreatePixelShader(pixelBytecode->GetBufferPointer(),
                                                        pixelBytecode->GetBufferSize(), nullptr,
                                                        &program.m_PixelShader),
                             "ID3D11Device::CreatePixelShader");

        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> vertexReflector = nullptr;
        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> pixelReflector = nullptr;

        d3d11::ThrowIfFailed(D3DReflect(vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
                                        IID_ID3D11ShaderReflection,
                                        reinterpret_cast<void**>(vertexReflector.GetAddressOf())),
                             "D3DReflect");
        d3d11::ThrowIfFailed(D3DReflect(pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(),
                                        IID_ID3D11ShaderReflection,
                                        reinterpret_cast<void**>(pixelReflector.GetAddressOf())),
                             "D3DReflect");

        D3D11_SHADER_DESC vertexDescription{};
        vertexReflector->GetDesc(&vertexDescription);

        std::vector<D3D11_INPUT_ELEMENT_DESC> vInputElements;
        vInputElements.reserve(vertexDescription.InputParameters);

        UINT byteOffset = 0;
        for (UINT index = 0; index < vertexDescription.InputParameters; ++index)
        {
            D3D11_SIGNATURE_PARAMETER_DESC signature{};
            vertexReflector->GetInputParameterDesc(index, &signature);

            if (signature.SystemValueType != D3D_NAME_UNDEFINED)
            {
                continue;
            }

            const UINT components = CountMaskComponents(signature.Mask);
            D3D11_INPUT_ELEMENT_DESC element{};
            element.SemanticName = signature.SemanticName;
            element.SemanticIndex = signature.SemanticIndex;
            element.Format = SignatureFormat(signature.ComponentType, components);
            element.InputSlot = 0;
            element.AlignedByteOffset = byteOffset;
            element.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            element.InstanceDataStepRate = 0;
            vInputElements.push_back(element);

            program.m_Attributes[signature.SemanticName] = static_cast<int>(index);

            if (SemanticEquals(signature.SemanticName, "POSITION"))
            {
                program.m_PositionOffset = byteOffset;
                program.m_PositionComponents = components;
                program.m_HasPosition = true;
            }
            else if (SemanticEquals(signature.SemanticName, "TEXCOORD") && program.m_TexCoordOffset == 0xFFFFFFFFu)
            {
                program.m_TexCoordOffset = byteOffset;
                program.m_TexCoordComponents = components;
            }
            else if (SemanticEquals(signature.SemanticName, "COLOR"))
            {
                program.m_ColorOffset = byteOffset;
                program.m_ColorComponents = components;
            }
            else if (SemanticEquals(signature.SemanticName, "NORMAL"))
            {
                program.m_NormalOffset = byteOffset;
                program.m_NormalComponents = components;
            }
            else if (SemanticEquals(signature.SemanticName, "WORLD_POSITION"))
            {
                program.m_WorldPositionOffset = byteOffset;
                program.m_WorldPositionComponents = components;
            }
            else if (SemanticEquals(signature.SemanticName, "TANGENT"))
            {
                program.m_TangentOffset = byteOffset;
                program.m_TangentComponents = components;
            }

            byteOffset += components * sizeof(float);
        }

        program.m_StrideBytes = byteOffset;

        d3d11::ThrowIfFailed(pDevice->CreateInputLayout(vInputElements.data(), static_cast<UINT>(vInputElements.size()),
                                                        vertexBytecode->GetBufferPointer(),
                                                        vertexBytecode->GetBufferSize(), &program.m_InputLayout),
                             "ID3D11Device::CreateInputLayout shader");

        std::unordered_map<std::string, UINT> constantBufferBases;
        UINT totalConstantSize = 0;

        const auto registerUniformEntry = [&](const std::string& name, UINT offset, UINT size)
        {
            if (name.empty() || program.m_Uniforms.count(name))
            {
                return;
            }
            ShaderUniformInfo info{};
            info.m_Name = name;
            info.m_Offset = offset;
            info.m_Size = size;
            program.m_vUniformInfos.push_back(std::move(info));
            program.m_Uniforms.emplace(name, static_cast<int>(program.m_vUniformInfos.size()) - 1);
        };

        const auto reflectTypeMembers = [&](auto& self, ID3D11ShaderReflectionType* type, const std::string& prefix,
                                            UINT offset, UINT totalSize) -> void
        {
            if (!type)
            {
                return;
            }
            D3D11_SHADER_TYPE_DESC typeDesc{};
            if (FAILED(type->GetDesc(&typeDesc)))
            {
                return;
            }

            if (typeDesc.Elements > 0)
            {
                const UINT elemStride = AlignTo16(totalSize / typeDesc.Elements);
                for (UINT elemIndex = 0; elemIndex < typeDesc.Elements; ++elemIndex)
                {
                    const std::string elemPrefix = prefix + "[" + std::to_string(elemIndex) + "]";
                    const UINT elemOffset = offset + elemIndex * elemStride;
                    registerUniformEntry(elemPrefix, elemOffset, elemStride);

                    for (UINT memberIndex = 0; memberIndex < typeDesc.Members; ++memberIndex)
                    {
                        ID3D11ShaderReflectionType* memberType = type->GetMemberTypeByIndex(memberIndex);
                        const char* memberName = type->GetMemberTypeName(memberIndex);
                        if (!memberType || !memberName)
                        {
                            continue;
                        }
                        D3D11_SHADER_TYPE_DESC memberDesc{};
                        if (FAILED(memberType->GetDesc(&memberDesc)))
                        {
                            continue;
                        }
                        UINT memberSize = sizeof(float) * 4;
                        if (memberIndex + 1 < typeDesc.Members)
                        {
                            D3D11_SHADER_TYPE_DESC nextDesc{};
                            if (SUCCEEDED(type->GetMemberTypeByIndex(memberIndex + 1)->GetDesc(&nextDesc)) &&
                                nextDesc.Offset > memberDesc.Offset)
                            {
                                memberSize = nextDesc.Offset - memberDesc.Offset;
                            }
                        }
                        else if (elemStride > memberDesc.Offset)
                        {
                            memberSize = elemStride - memberDesc.Offset;
                        }
                        const std::string fullMemberName = elemPrefix + "." + memberName;
                        registerUniformEntry(fullMemberName, elemOffset + memberDesc.Offset, memberSize);
                        self(self, memberType, fullMemberName, elemOffset + memberDesc.Offset, memberSize);
                    }
                }
                return;
            }

            for (UINT memberIndex = 0; memberIndex < typeDesc.Members; ++memberIndex)
            {
                ID3D11ShaderReflectionType* memberType = type->GetMemberTypeByIndex(memberIndex);
                const char* memberName = type->GetMemberTypeName(memberIndex);
                if (!memberType || !memberName)
                {
                    continue;
                }
                D3D11_SHADER_TYPE_DESC memberDesc{};
                if (FAILED(memberType->GetDesc(&memberDesc)))
                {
                    continue;
                }
                UINT memberSize = sizeof(float) * 4;
                if (memberIndex + 1 < typeDesc.Members)
                {
                    D3D11_SHADER_TYPE_DESC nextDesc{};
                    if (SUCCEEDED(type->GetMemberTypeByIndex(memberIndex + 1)->GetDesc(&nextDesc)) &&
                        nextDesc.Offset > memberDesc.Offset)
                    {
                        memberSize = nextDesc.Offset - memberDesc.Offset;
                    }
                }
                else if (totalSize > memberDesc.Offset)
                {
                    memberSize = totalSize - memberDesc.Offset;
                }
                const std::string fullMemberName = prefix + "." + memberName;
                registerUniformEntry(fullMemberName, offset + memberDesc.Offset, memberSize);
                self(self, memberType, fullMemberName, offset + memberDesc.Offset, memberSize);
            }
        };

        const auto reflectStage = [&](ID3D11ShaderReflection* reflector)
        {
            if (!reflector)
            {
                return;
            }

            D3D11_SHADER_DESC stageDescription{};
            reflector->GetDesc(&stageDescription);

            for (UINT bufferIndex = 0; bufferIndex < stageDescription.ConstantBuffers; ++bufferIndex)
            {
                auto* constantBuffer = reflector->GetConstantBufferByIndex(bufferIndex);
                D3D11_SHADER_BUFFER_DESC bufferDescription{};
                if (FAILED(constantBuffer->GetDesc(&bufferDescription)) || bufferDescription.Type != D3D11_CT_CBUFFER)
                {
                    continue;
                }

                const std::string bufferName = bufferDescription.Name ? bufferDescription.Name : "";
                UINT baseOffset = 0;
                const auto baseIterator = constantBufferBases.find(bufferName);
                if (baseIterator != constantBufferBases.end())
                {
                    baseOffset = baseIterator->second;
                }
                else
                {
                    baseOffset = AlignTo16(totalConstantSize);
                    totalConstantSize = baseOffset + AlignTo16(bufferDescription.Size);
                    constantBufferBases.emplace(bufferName, baseOffset);
                }

                for (UINT variableIndex = 0; variableIndex < bufferDescription.Variables; ++variableIndex)
                {
                    auto* variable = constantBuffer->GetVariableByIndex(variableIndex);
                    D3D11_SHADER_VARIABLE_DESC variableDescription{};
                    if (FAILED(variable->GetDesc(&variableDescription)))
                    {
                        continue;
                    }

                    const std::string uniformName = variableDescription.Name ? variableDescription.Name : "";
                    if (uniformName.empty())
                    {
                        continue;
                    }

                    const UINT varOffset = baseOffset + variableDescription.StartOffset;
                    registerUniformEntry(uniformName, varOffset, variableDescription.Size);
                    reflectTypeMembers(reflectTypeMembers, variable->GetType(), uniformName, varOffset,
                                       variableDescription.Size);
                }
            }
        };

        reflectStage(vertexReflector.Get());
        reflectStage(pixelReflector.Get());

        const auto findFirstUniform = [&](std::initializer_list<const char*> names) -> int
        {
            for (const char* name : names)
            {
                const auto it = program.m_Uniforms.find(name);
                if (it != program.m_Uniforms.end())
                {
                    return it->second;
                }
            }
            return -1;
        };
        program.m_LocModel = findFirstUniform({"uModel", "model", "matModel", "MATRIX_MODEL"});
        program.m_LocView = findFirstUniform({"uView", "view", "matView", "MATRIX_VIEW"});
        program.m_LocProjection = findFirstUniform({"uProjection", "projection", "matProjection", "MATRIX_PROJECTION"});
        program.m_LocMvp = findFirstUniform({"uMvp", "mvp", "MATRIX_MVP"});
        program.m_LocNormal = findFirstUniform({"uNormal", "normalMatrix", "matNormal", "MATRIX_NORMAL"});
        program.m_LocColor = findFirstUniform({"uColor", "colDiffuse", "COLOR_DIFFUSE"});
        program.m_LocUseInstancing = findFirstUniform({"useInstancing"});
        program.m_LocInstanceBase = findFirstUniform({"instanceBase"});

        if (totalConstantSize > 0)
        {
            EnsureConstantCapacity(program, AlignTo16(totalConstantSize));
            TraceLog(LogLevel::Trace, "SHADER",
                     TextFormat("[D3D11] Reflected shader constants (%u bytes, %zu uniforms)",
                                program.m_ConstantBufferSize, program.m_vUniformInfos.size()));
        }
    }

    void QuarkD3D11Renderer::EnsureConstantCapacity(ShaderProgramData& program, size_t byteCount)
    {
        const size_t aligned = static_cast<size_t>(
            AlignTo16(static_cast<UINT>(byteCount > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<UINT>(byteCount))));

        if (aligned > program.m_vConstantStaging.size())
        {
            const size_t grown = std::max(aligned, program.m_vConstantStaging.size() * 2);
            program.m_vConstantStaging.resize(grown, 0);
        }

        if (program.m_vConstantStaging.size() > program.m_ConstantBufferSize && m_Device.Get() != nullptr)
        {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = static_cast<UINT>(program.m_vConstantStaging.size());
            description.Usage = D3D11_USAGE_DYNAMIC;
            description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            d3d11::ThrowIfFailed(m_Device.Get()->CreateBuffer(&description, nullptr, &program.m_ConstantBuffer),
                                 "ID3D11Device::CreateBuffer shader constants");
            program.m_ConstantBufferSize = description.ByteWidth;
            program.m_Dirty = true;
        }
    }

    void QuarkD3D11Renderer::UploadConstantBuffer(ShaderProgramData& program)
    {
        ID3D11DeviceContext* pContext = m_Device.Context();
        if (!pContext || !program.m_ConstantBuffer || !program.m_Dirty || program.m_vConstantStaging.empty())
        {
            return;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(pContext->Map(program.m_ConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            TraceLog(LogLevel::Warn, "SHADER", "[D3D11] Failed to map shader constant buffer.");
            return;
        }

        const size_t bytes = std::min<size_t>(program.m_vConstantStaging.size(), program.m_ConstantBufferSize);
        std::memcpy(mapped.pData, program.m_vConstantStaging.data(), bytes);
        pContext->Unmap(program.m_ConstantBuffer.Get(), 0);
        program.m_Dirty = false;
    }

    void QuarkD3D11Renderer::StoreUniformValue(ShaderProgramData& program, int locIndex, int uniformType,
                                               const void* pValue, size_t elementBytes, int count)
    {
        if (locIndex < 0 || pValue == nullptr || count <= 0 || elementBytes == 0)
        {
            return;
        }

        if (locIndex >= static_cast<int>(program.m_vUniformInfos.size()))
        {
            TraceLog(LogLevel::Warn, "SHADER",
                     TextFormat("[D3D11] Ignoring uniform write to unknown location %d.", locIndex));
            return;
        }

        const ShaderUniformInfo& info = program.m_vUniformInfos[static_cast<size_t>(locIndex)];
        const size_t totalBytes = static_cast<size_t>(count) * elementBytes;

        EnsureConstantCapacity(program, static_cast<size_t>(info.m_Offset) + totalBytes);
        std::memcpy(program.m_vConstantStaging.data() + info.m_Offset, pValue, totalBytes);
        (void)uniformType;
        program.m_Dirty = true;
    }

    void QuarkD3D11Renderer::RegisterShaderTexture(ShaderProgramData& program, int locIndex, uint32_t textureId)
    {
        if (locIndex < 0)
        {
            return;
        }

        if (textureId != 0 && program.m_TextureSlots.find(locIndex) == program.m_TextureSlots.end())
        {
            constexpr UINT maxShaderSlots = 7;
            if (program.m_NextTextureSlot > maxShaderSlots)
            {
                TraceLog(LogLevel::Warn, "SHADER", "[D3D11] Shader texture slot limit reached (slots 1..7 available).");
                program.m_TextureSlots[locIndex] = maxShaderSlots;
            }
            else
            {
                program.m_TextureSlots[locIndex] = program.m_NextTextureSlot++;
            }
        }

        program.m_TextureIds[locIndex] = textureId;
        program.m_Dirty = true;
    }

    D3D11CommandContext::ShaderOverride
    QuarkD3D11Renderer::BuildShaderOverride(uint32_t shaderId, ID3D11ShaderResourceView* pSlot0Fallback,
                                            bool uploadConstants)
    {
        D3D11CommandContext::ShaderOverride shaderOverride{};

        ShaderProgramData* pProgram = GetShaderProgram(shaderId);
        if (!pProgram || !pProgram->m_VertexShader || !pProgram->m_PixelShader || !pProgram->m_InputLayout ||
            !pProgram->m_HasPosition)
        {
            return shaderOverride;
        }

        if (uploadConstants)
        {
            UploadConstantBuffer(*pProgram);
        }

        shaderOverride.m_pVertexShader = pProgram->m_VertexShader.Get();
        shaderOverride.m_pPixelShader = pProgram->m_PixelShader.Get();
        shaderOverride.m_pInputLayout = pProgram->m_InputLayout.Get();
        shaderOverride.m_pConstantBuffer = pProgram->m_ConstantBuffer ? pProgram->m_ConstantBuffer.Get() : nullptr;
        shaderOverride.m_pConstantData =
            pProgram->m_vConstantStaging.empty() ? nullptr : pProgram->m_vConstantStaging.data();
        shaderOverride.m_ConstantDataSize = static_cast<uint32_t>(pProgram->m_vConstantStaging.size());
        shaderOverride.m_StrideBytes = pProgram->m_StrideBytes;
        shaderOverride.m_PositionOffset = pProgram->m_PositionOffset;
        shaderOverride.m_TexCoordOffset = pProgram->m_TexCoordOffset;
        shaderOverride.m_ColorOffset = pProgram->m_ColorOffset;

        for (const auto& [location, textureId] : pProgram->m_TextureIds)
        {
            if (textureId == 0)
            {
                continue;
            }

            UINT slot = 0;
            const auto slotIterator = pProgram->m_TextureSlots.find(location);
            if (slotIterator != pProgram->m_TextureSlots.end())
            {
                slot = slotIterator->second;
            }
            else if (shaderOverride.m_apShaderResources[0] == nullptr)
            {
                slot = 0;
            }
            else
            {
                continue;
            }

            if (slot < 8)
            {
                shaderOverride.m_apShaderResources[slot] = m_Resources.ShaderResource(textureId);
            }
        }

        if (!shaderOverride.m_apShaderResources[0])
        {
            if (pSlot0Fallback)
            {
                shaderOverride.m_apShaderResources[0] = pSlot0Fallback;
            }
            else if (m_WhiteShaderTexture.IsValid())
            {
                shaderOverride.m_apShaderResources[0] = m_Resources.ShaderResource(m_WhiteShaderTexture.m_Id);
            }
        }

        return shaderOverride;
    }

    void QuarkD3D11Renderer::BeginShaderMode(const Shader& shader)
    {
        if (shader.id == 0)
        {
            EndShaderMode();
            return;
        }

        if (m_ShaderPrograms.find(shader.id) == m_ShaderPrograms.end())
        {
            TraceLog(LogLevel::Warn, "SHADER",
                     TextFormat("[D3D11] BeginShaderMode ignored unknown shader (ID: %u)", shader.id));
            return;
        }

        m_CurrentShaderId = shader.id;
        const D3D11CommandContext::ShaderOverride shaderOverride = BuildShaderOverride(shader.id);
        if (!shaderOverride.Active())
        {
            m_CurrentShaderId = 0;
            m_Commands.SetShaderOverride({});
            TraceLog(LogLevel::Warn, "SHADER",
                     TextFormat("[D3D11] BeginShaderMode ignored incomplete shader (ID: %u)", shader.id));
            return;
        }

        m_Commands.SetShaderOverride(shaderOverride);
    }

    void QuarkD3D11Renderer::EndShaderMode()
    {
        if (m_CurrentShaderId == 0)
        {
            return;
        }

        m_CurrentShaderId = 0;
        m_Commands.SetShaderOverride({});
    }

    Shader QuarkD3D11Renderer::LoadShader(const char* pVsFileName, const char* pFsFileName)
    {
        TraceLog(LogLevel::Trace, "SHADER",
                 TextFormat("[D3D11] Loading shader files: VS='%s', FS='%s'", pVsFileName ? pVsFileName : "<default>",
                            pFsFileName ? pFsFileName : "<default>"));

        std::string vsSource;
        std::string fsSource;

        if (pVsFileName && !ReadTextFile(pVsFileName, vsSource))
        {
            TraceLog(LogLevel::Error, "SHADER",
                     TextFormat("[D3D11] Failed to open vertex shader file: %s", pVsFileName));
            return Shader{};
        }

        if (pFsFileName && !ReadTextFile(pFsFileName, fsSource))
        {
            TraceLog(LogLevel::Error, "SHADER",
                     TextFormat("[D3D11] Failed to open fragment shader file: %s", pFsFileName));
            return Shader{};
        }

        if (!pVsFileName && !pFsFileName)
        {
            return Shader{};
        }

        return LoadShaderFromMemory(pVsFileName ? vsSource.c_str() : DEFAULT_HLSL_VERTEX_SOURCE,
                                    pFsFileName ? fsSource.c_str() : DEFAULT_HLSL_PIXEL_SOURCE);
    }

    Shader QuarkD3D11Renderer::LoadShaderFromMemory(const char* pVsSource, const char* pFsSource)
    {
        if (!pVsSource && !pFsSource)
        {
            return Shader{};
        }

        const uint32_t shaderId = m_NextShaderId++;
        ShaderProgramData& program = m_ShaderPrograms[shaderId];

        try
        {
            BuildShaderProgram(program, pVsSource ? pVsSource : DEFAULT_HLSL_VERTEX_SOURCE,
                               pFsSource ? pFsSource : DEFAULT_HLSL_PIXEL_SOURCE);
        }
        catch (const std::exception& exception)
        {
            TraceLog(LogLevel::Error, "SHADER",
                     TextFormat("[D3D11] Shader creation failed (ID: %u): %s", shaderId, exception.what()));
            m_ShaderPrograms.erase(shaderId);
            return Shader{};
        }

        TraceLog(LogLevel::Info, "SHADER",
                 TextFormat("[D3D11] Shader created successfully (ID: %u, Attributes: %zu, "
                            "Uniforms: %zu)",
                            shaderId, program.m_Attributes.size(), program.m_Uniforms.size()));

        return Shader{shaderId};
    }

    void QuarkD3D11Renderer::UnloadShader(Shader& shader)
    {
        if (shader.id == 0)
        {
            return;
        }

        if (m_ShaderPrograms.erase(shader.id) > 0)
        {
            TraceLog(LogLevel::Info, "SHADER", TextFormat("[D3D11] Shader unloaded (ID: %u)", shader.id));
        }

        if (m_CurrentShaderId == shader.id)
        {
            m_CurrentShaderId = 0;
            m_Commands.SetShaderOverride({});
        }

        shader = Shader{};
    }

    bool QuarkD3D11Renderer::isShaderValid(Shader& shader)
    {
        return shader.id != 0 && m_ShaderPrograms.find(shader.id) != m_ShaderPrograms.end();
    }

    int QuarkD3D11Renderer::GetShaderLocation(const Shader& shader, const char* pUniformName)
    {
        if (!pUniformName || shader.id == 0)
        {
            return -1;
        }

        ShaderProgramData* pProgram = GetShaderProgram(shader.id);
        if (!pProgram)
        {
            return -1;
        }

        const auto locationIterator = pProgram->m_Uniforms.find(pUniformName);
        if (locationIterator != pProgram->m_Uniforms.end())
        {
            return locationIterator->second;
        }

        ShaderUniformInfo info{};
        info.m_Name = pUniformName;
        info.m_Offset = AlignTo16(static_cast<UINT>(pProgram->m_vConstantStaging.size()));
        info.m_Size = sizeof(float) * 4;
        pProgram->m_vUniformInfos.push_back(std::move(info));

        const int location = static_cast<int>(pProgram->m_vUniformInfos.size()) - 1;
        pProgram->m_Uniforms.emplace(pUniformName, location);

        TraceLog(LogLevel::Trace, "SHADER",
                 TextFormat("[D3D11] Allocated virtual uniform '%s' (Location: %d)", pUniformName, location));

        return location;
    }

    int QuarkD3D11Renderer::GetShaderLocation(const Shader& shader, ShaderLocationIndex locIndex)
    {
        if (locIndex < SHADER_LOC_VERTEX_POSITION || locIndex >= SHADER_LOC_COUNT)
        {
            return -1;
        }

        return GetShaderLocation(shader, s_apShaderLocationNames[locIndex]);
    }

    int QuarkD3D11Renderer::GetShaderAttributeLocation(const Shader& shader, const char* pAttribName)
    {
        if (!pAttribName || shader.id == 0)
        {
            return -1;
        }

        ShaderProgramData* pProgram = GetShaderProgram(shader.id);
        if (!pProgram)
        {
            return -1;
        }

        const auto attributeIterator = pProgram->m_Attributes.find(pAttribName);
        if (attributeIterator != pProgram->m_Attributes.end())
        {
            return attributeIterator->second;
        }

        const int newLocation = static_cast<int>(pProgram->m_Attributes.size());
        pProgram->m_Attributes.emplace(pAttribName, newLocation);
        return newLocation;
    }

    void QuarkD3D11Renderer::SetShaderValue(const Shader& shader, int locIndex, float value)
    {
        ShaderProgramData* pProgram = GetShaderProgram(shader.id);
        if (!pProgram)
        {
            return;
        }

        StoreUniformValue(*pProgram, locIndex, SHADER_UNIFORM_FLOAT, &value, sizeof(value), 1);
        if (m_CurrentShaderId == shader.id)
        {
            m_Commands.SetShaderOverride(BuildShaderOverride(shader.id));
        }
    }

    void QuarkD3D11Renderer::SetShaderValue(const Shader& shader, int locIndex, int value)
    {
        ShaderProgramData* pProgram = GetShaderProgram(shader.id);
        if (!pProgram)
        {
            return;
        }

        StoreUniformValue(*pProgram, locIndex, SHADER_UNIFORM_INT, &value, sizeof(value), 1);
        if (m_CurrentShaderId == shader.id)
        {
            m_Commands.SetShaderOverride(BuildShaderOverride(shader.id));
        }
    }

    void QuarkD3D11Renderer::SetShaderValue(const Shader& shader, int locIndex, const Color& value)
    {
        ShaderProgramData* pProgram = GetShaderProgram(shader.id);
        if (!pProgram)
        {
            return;
        }

        const float rgba[4] = {value.r / 255.0f, value.g / 255.0f, value.b / 255.0f, value.a / 255.0f};
        StoreUniformValue(*pProgram, locIndex, SHADER_UNIFORM_VEC4, rgba, sizeof(float), 4);
        if (m_CurrentShaderId == shader.id)
        {
            m_Commands.SetShaderOverride(BuildShaderOverride(shader.id));
        }
    }

    void QuarkD3D11Renderer::SetShaderValue(const Shader& shader, int locIndex, const Vec2& value)
    {
        ShaderProgramData* pProgram = GetShaderProgram(shader.id);
        if (!pProgram)
        {
            return;
        }

        const float vec[2] = {value.x, value.y};
        StoreUniformValue(*pProgram, locIndex, SHADER_UNIFORM_VEC2, vec, sizeof(float), 2);
        if (m_CurrentShaderId == shader.id)
        {
            m_Commands.SetShaderOverride(BuildShaderOverride(shader.id));
        }
    }

    void QuarkD3D11Renderer::SetShaderValue(const Shader& shader, int locIndex, const Vec3& value)
    {
        ShaderProgramData* pProgram = GetShaderProgram(shader.id);
        if (!pProgram)
        {
            return;
        }

        const float vec[3] = {value.x, value.y, value.z};
        StoreUniformValue(*pProgram, locIndex, SHADER_UNIFORM_VEC3, vec, sizeof(float), 3);
        if (m_CurrentShaderId == shader.id)
        {
            m_Commands.SetShaderOverride(BuildShaderOverride(shader.id));
        }
    }

    void QuarkD3D11Renderer::SetShaderValue(const Shader& shader, int locIndex, const Vec4& value)
    {
        ShaderProgramData* pProgram = GetShaderProgram(shader.id);
        if (!pProgram)
        {
            return;
        }

        const float vec[4] = {value.x, value.y, value.z, value.w};
        StoreUniformValue(*pProgram, locIndex, SHADER_UNIFORM_VEC4, vec, sizeof(float), 4);
        if (m_CurrentShaderId == shader.id)
        {
            m_Commands.SetShaderOverride(BuildShaderOverride(shader.id));
        }
    }

    void QuarkD3D11Renderer::SetShaderValueMatrix(const Shader& shader, int locIndex, const float* pMat)
    {
        ShaderProgramData* pProgram = GetShaderProgram(shader.id);
        if (!pProgram || pMat == nullptr)
        {
            return;
        }

        StoreUniformValue(*pProgram, locIndex, SHADER_UNIFORM_FLOAT, pMat, sizeof(float), 16);
        if (m_CurrentShaderId == shader.id)
        {
            m_Commands.SetShaderOverride(BuildShaderOverride(shader.id));
        }
    }

    void QuarkD3D11Renderer::SetShaderValueMatrix(const Shader& shader, int locIndex, const Matrix& mat)
    {
        SetShaderValueMatrix(shader, locIndex, mat.m);
    }

    void QuarkD3D11Renderer::SetShaderValueSampler(const Shader& shader, int locIndex, int textureUnit)
    {
        ShaderProgramData* pProgram = GetShaderProgram(shader.id);
        if (!pProgram)
        {
            return;
        }

        StoreUniformValue(*pProgram, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureUnit, sizeof(int), 1);
        RegisterShaderTexture(*pProgram, locIndex, static_cast<uint32_t>(textureUnit));
        if (m_CurrentShaderId == shader.id)
        {
            m_Commands.SetShaderOverride(BuildShaderOverride(shader.id));
        }
    }

    void QuarkD3D11Renderer::SetShaderValue(const Shader& shader, int locIndex, const void* pValue, int uniformType)
    {
        ShaderProgramData* pProgram = GetShaderProgram(shader.id);
        if (!pProgram || pValue == nullptr)
        {
            return;
        }

        switch (uniformType)
        {
        case SHADER_UNIFORM_FLOAT:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(float), 1);
            break;
        case SHADER_UNIFORM_VEC2:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(float), 2);
            break;
        case SHADER_UNIFORM_VEC3:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(float), 3);
            break;
        case SHADER_UNIFORM_VEC4:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(float), 4);
            break;
        case SHADER_UNIFORM_INT:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(int), 1);
            break;
        case SHADER_UNIFORM_SAMPLER2D:
        {
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(int), 1);
            int textureId = 0;
            std::memcpy(&textureId, pValue, sizeof(textureId));
            RegisterShaderTexture(*pProgram, locIndex, static_cast<uint32_t>(textureId));
            break;
        }
        case SHADER_UNIFORM_IVEC2:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(int), 2);
            break;
        case SHADER_UNIFORM_IVEC3:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(int), 3);
            break;
        case SHADER_UNIFORM_IVEC4:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(int), 4);
            break;
        default:
            break;
        }

        if (m_CurrentShaderId == shader.id)
        {
            m_Commands.SetShaderOverride(BuildShaderOverride(shader.id));
        }
    }

    void QuarkD3D11Renderer::SetShaderValueV(const Shader& shader, int locIndex, const void* pValue, int uniformType,
                                             int count)
    {
        ShaderProgramData* pProgram = GetShaderProgram(shader.id);
        if (!pProgram || pValue == nullptr || count <= 0)
        {
            return;
        }

        switch (uniformType)
        {
        case SHADER_UNIFORM_FLOAT:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(float), count);
            break;
        case SHADER_UNIFORM_VEC2:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(float) * 2, count);
            break;
        case SHADER_UNIFORM_VEC3:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(float) * 3, count);
            break;
        case SHADER_UNIFORM_VEC4:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(float) * 4, count);
            break;
        case SHADER_UNIFORM_INT:
        case SHADER_UNIFORM_SAMPLER2D:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(int), count);
            break;
        case SHADER_UNIFORM_IVEC2:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(int) * 2, count);
            break;
        case SHADER_UNIFORM_IVEC3:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(int) * 3, count);
            break;
        case SHADER_UNIFORM_IVEC4:
            StoreUniformValue(*pProgram, locIndex, uniformType, pValue, sizeof(int) * 4, count);
            break;
        default:
            break;
        }

        if (m_CurrentShaderId == shader.id)
        {
            m_Commands.SetShaderOverride(BuildShaderOverride(shader.id));
        }
    }

    void QuarkD3D11Renderer::SetShaderValueTexture(const Shader& shader, int locIndex, const ITexture& texture)
    {
        ShaderProgramData* pProgram = GetShaderProgram(shader.id);
        if (!pProgram)
        {
            return;
        }

        const int textureUnit = static_cast<int>(texture.m_Id);
        StoreUniformValue(*pProgram, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureUnit, sizeof(int), 1);
        RegisterShaderTexture(*pProgram, locIndex, texture.m_Id);
        if (m_CurrentShaderId == shader.id)
        {
            m_Commands.SetShaderOverride(BuildShaderOverride(shader.id));
        }
    }

    void QuarkD3D11Renderer::SetShaderValueTextureUnit(const Shader& shader, int locIndex, const ITexture& texture,
                                                       int textureUnit)
    {
        ShaderProgramData* pProgram = GetShaderProgram(shader.id);
        if (!pProgram)
        {
            return;
        }

        const int textureId = texture.m_Valid ? static_cast<int>(texture.m_Id) : textureUnit;
        StoreUniformValue(*pProgram, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureId, sizeof(int), 1);
        RegisterShaderTexture(*pProgram, locIndex, static_cast<uint32_t>(textureId));
        if (m_CurrentShaderId == shader.id)
        {
            m_Commands.SetShaderOverride(BuildShaderOverride(shader.id));
        }
    }

    bool QuarkD3D11Renderer::UpdateTexture(const ITexture& texture, const void* pPixels)
    {
        if (!pPixels || texture.m_Id == 0)
            return false;
        return m_Resources.UpdateTexture(m_Device.Context(), texture.m_Id, static_cast<const uint8_t*>(pPixels),
                                         texture.m_Width, texture.m_Height);
    }

    bool QuarkD3D11Renderer::UpdateTextureRegion(const ITexture& texture, Rectangle region, const void* pPixels)
    {
        if (!pPixels || texture.m_Id == 0)
            return false;
        return m_Resources.UpdateTextureRegion(m_Device.Context(), texture.m_Id, static_cast<const uint8_t*>(pPixels),
                                               static_cast<int>(region.x), static_cast<int>(region.y),
                                               static_cast<int>(region.width), static_cast<int>(region.height));
    }
} // namespace qci
#endif
