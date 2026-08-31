#include "QuarkD3D11Renderer.hpp"
#include "../../QuarkInternal.hpp"

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
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace qc
{

namespace {

static Color MultiplyColor(Color lhs, Color rhs)
{
    return Color{
        static_cast<unsigned char>((static_cast<unsigned int>(lhs.r) * rhs.r) / 255u),
        static_cast<unsigned char>((static_cast<unsigned int>(lhs.g) * rhs.g) / 255u),
        static_cast<unsigned char>((static_cast<unsigned int>(lhs.b) * rhs.b) / 255u),
        static_cast<unsigned char>((static_cast<unsigned int>(lhs.a) * rhs.a) / 255u)
    };
}

static Mat4 TransposeMat4(const Mat4& matrix)
{
    Mat4 result{};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            result.m[row * 4 + col] = matrix.m[col * 4 + row];
        }
    }
    return result;
}

const char* ShaderLocationNames[SHADER_LOC_COUNT] = {
    "POSITION",
    "TEXCOORD0",
    "TEXCOORD1",
    "NORMAL",
    "TANGENT",
    "COLOR",
    "mvp",
    "view",
    "projection",
    "model",
    "normalMatrix",
    "viewPos",
    "colDiffuse",
    "colSpecular",
    "colAmbient",
    "albedo",
    "metalness",
    "normal",
    "roughness",
    "occlusion",
    "emission",
    "height",
    "cubemap",
    "irradiance",
    "prefilter",
    "brdf",
    "boneIds",
    "boneWeights",
    "boneTransforms",
    "instanceTransform"
};

constexpr char kDefaultHlslVertexSource[] = R"(
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

constexpr char kDefaultHlslPixelSource[] = R"(
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

bool ReadTextFile(const char* path, std::string& out)
{
    if (!path) {
        return false;
    }

    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
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
    for (BYTE bit = 1; bit != 0; bit <<= 1) {
        if (mask & bit) {
            ++count;
        }
    }
    return count > 0 ? count : 1;
}

DXGI_FORMAT SignatureFormat(D3D_REGISTER_COMPONENT_TYPE componentType, UINT components)
{
    switch (componentType) {
        case D3D_REGISTER_COMPONENT_SINT32:
            return components >= 4 ? DXGI_FORMAT_R32G32B32A32_SINT :
                   components == 3 ? DXGI_FORMAT_R32G32B32_SINT :
                   components == 2 ? DXGI_FORMAT_R32G32_SINT : DXGI_FORMAT_R32_SINT;
        case D3D_REGISTER_COMPONENT_UINT32:
            return components >= 4 ? DXGI_FORMAT_R32G32B32A32_UINT :
                   components == 3 ? DXGI_FORMAT_R32G32B32_UINT :
                   components == 2 ? DXGI_FORMAT_R32G32_UINT : DXGI_FORMAT_R32_UINT;
        case D3D_REGISTER_COMPONENT_FLOAT32:
        default:
            return components >= 4 ? DXGI_FORMAT_R32G32B32A32_FLOAT :
                   components == 3 ? DXGI_FORMAT_R32G32B32_FLOAT :
                   components == 2 ? DXGI_FORMAT_R32G32_FLOAT : DXGI_FORMAT_R32_FLOAT;
    }
}

bool SemanticEquals(const char* semantic, const char* name)
{
    return semantic != nullptr && _stricmp(semantic, name) == 0;
}

const char* DefaultFontPath()
{
    static const char* paths[] = {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/verdana.ttf",
        nullptr
    };

    for (int index = 0; paths[index] != nullptr; ++index) {
        std::ifstream file(paths[index], std::ios::binary);
        if (file.good()) {
            return paths[index];
        }
    }

    return nullptr;
}

void DrawThickLine(D3D11CommandContext &commands,
                   Vec2 start,
                   Vec2 end,
                   float lineWidth,
                   Color color,
                   int screenWidth,
                   int screenHeight)
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
    const Vec2 points[] = {
        {start.x + normal.x, start.y + normal.y},
        {end.x + normal.x, end.y + normal.y},
        {end.x - normal.x, end.y - normal.y},
        {start.x - normal.x, start.y - normal.y}
    };

    commands.DrawTriangle(points[0], points[1], points[2], color, screenWidth, screenHeight);
    commands.DrawTriangle(points[0], points[2], points[3], color, screenWidth, screenHeight);
}

} // namespace

QuarkD3D11Renderer::~QuarkD3D11Renderer()
{
    Shutdown();
}

void QuarkD3D11Renderer::Init(SDL_Window *window, int width, int height)
{
    TraceLog(LogLevel::Info, "D3D11",
             TextFormat("Initializing renderer (Window: %dx%d)", width, height));
    m_window = window;
    SDL_GetWindowSizeInPixels(window, &m_width, &m_height);
    if (m_width <= 0 || m_height <= 0)
    {
        m_width = width;
        m_height = height;
    }
    m_lastFrame = std::chrono::steady_clock::now();

    m_device.Initialize();
    m_resources.Initialize(m_device.Get());
    m_swapChain.Initialize(m_device, window, m_width, m_height);
    m_pipeline.Initialize(m_device.Get(), m_shaderCompiler, m_resources);
    m_commands.Initialize(m_device, m_swapChain, m_pipeline, m_resources, m_width, m_height);
    {
        const uint8_t white[4] = {255, 255, 255, 255};
        m_whiteShaderTexture = m_resources.CreateTexture(m_device.Get(), white, 1, 1);
    }
    {
        const uint8_t black[4] = {0, 0, 0, 255};
        m_blackShaderTexture = m_resources.CreateTexture(m_device.Get(), black, 1, 1);
    }
    {
        const uint8_t flatNormal[4] = {128, 128, 255, 255};
        m_flatNormalShaderTexture = m_resources.CreateTexture(m_device.Get(), flatNormal, 1, 1);
    }
    if (m_requestedMsaaSamples > 1)
    {
        m_swapChain.SetMSAASamples(static_cast<UINT>(m_requestedMsaaSamples));
    }
    m_textureFilterMode = gTextureFilterMode;
    m_pipeline.SetTextureFilterMode(m_textureFilterMode);
    RefreshViewport();
    TraceLog(LogLevel::Info, "D3D11", "Renderer initialized successfully.");
}

void QuarkD3D11Renderer::Shutdown()
{
    TraceLog(LogLevel::Info, "D3D11", "Shutting down D3D11 renderer...");

    m_currentShaderId = 0;
    m_shaderPrograms.clear();
    if (m_whiteShaderTexture.IsValid()) {
        m_resources.DestroyTexture(m_whiteShaderTexture.id);
        m_whiteShaderTexture = {};
    }
    if (m_blackShaderTexture.IsValid()) {
        m_resources.DestroyTexture(m_blackShaderTexture.id);
        m_blackShaderTexture = {};
    }
    if (m_flatNormalShaderTexture.IsValid()) {
        m_resources.DestroyTexture(m_flatNormalShaderTexture.id);
        m_flatNormalShaderTexture = {};
    }
    for (const auto& [_, cachedTexture] : m_textureCache) {
        if (cachedTexture.texture.IsValid()) {
            m_resources.DestroyTexture(cachedTexture.texture.id);
        }
    }
    m_textureCache.clear();
    m_textureCacheKeys.clear();
    m_commands.Shutdown();
    m_pipeline.Shutdown();
    m_resources.Shutdown();
    m_swapChain.Shutdown();
    m_device.Shutdown();
    m_window = nullptr;
    m_drawing = false;

    TraceLog(LogLevel::Info, "D3D11", "D3D11 renderer shut down successfully.");
}

void QuarkD3D11Renderer::SetMSAASamples(int samples)
{
    m_requestedMsaaSamples = (samples == 2 || samples == 4 || samples == 8) ? samples : 1;
    if (m_swapChain.Get() != nullptr)
    {
        m_swapChain.SetMSAASamples(static_cast<UINT>(m_requestedMsaaSamples));
    }
}

void QuarkD3D11Renderer::SetTextureFilterMode(TextureFilterMode mode)
{
    m_textureFilterMode = mode;
    m_pipeline.SetTextureFilterMode(mode);
}

void QuarkD3D11Renderer::Set3DLightEnabled(int index, bool enabled)
{
    if (index < 0 || static_cast<std::size_t>(index) >= m_lights.size())
    {
        return;
    }

    m_lights[static_cast<std::size_t>(index)].enabled = enabled;
    TraceLog(LogLevel::Trace, "D3D11",
             TextFormat("3D light %d %s.", index, enabled ? "enabled" : "disabled"));
}

void QuarkD3D11Renderer::RefreshViewport()
{
    if (m_window)
    {
        int pixelWidth = 0;
        int pixelHeight = 0;
        SDL_GetWindowSizeInPixels(m_window, &pixelWidth, &pixelHeight);

        if (pixelWidth > 0 && pixelHeight > 0 &&
            (pixelWidth != m_width || pixelHeight != m_height))
        {
            m_windowResized = true;
        }
    }

    m_commands.SetDefaultViewportSize(m_width, m_height);
    m_commands.RefreshViewport(m_width, m_height);
}

void QuarkD3D11Renderer::ResizeSwapChain()
{
    if (!m_window)
    {
        return;
    }

    int pixelWidth = 0;
    int pixelHeight = 0;
    SDL_GetWindowSizeInPixels(m_window, &pixelWidth, &pixelHeight);

    if (pixelWidth <= 0 || pixelHeight <= 0)
    {
        return;
    }

    m_commands.ReleaseRenderTargets();
    m_swapChain.Resize(m_device, pixelWidth, pixelHeight);
    m_width = pixelWidth;
    m_height = pixelHeight;
    m_commands.SetDefaultViewportSize(m_width, m_height);
    m_commands.RefreshViewport(m_width, m_height);
}

void QuarkD3D11Renderer::BeginDrawing()
{
    const auto now = std::chrono::steady_clock::now();
    m_frameTime = std::chrono::duration<float>(now - m_lastFrame).count();
    m_lastFrame = now;
    m_drawing = true;
    m_commands.BeginDrawing();

    D3D11LightConstantData lightData{};
    const float ambient[3] = {0.1f, 0.1f, 0.1f};
    lightData.SetAmbient(ambient);
    lightData.viewPosition[0] = m_viewPos.x;
    lightData.viewPosition[1] = m_viewPos.y;
    lightData.viewPosition[2] = m_viewPos.z;
    lightData.viewPosition[3] = 1.0f;

    for (std::size_t i = 0; i < m_lights.size(); ++i)
    {
        const Light3D &light = m_lights[i];
        lightData.lightPositions[i][0] = light.position.x;
        lightData.lightPositions[i][1] = light.position.y;
        lightData.lightPositions[i][2] = light.position.z;
        lightData.lightPositions[i][3] = 1.0f;
        lightData.lightColors[i][0] = light.color.x;
        lightData.lightColors[i][1] = light.color.y;
        lightData.lightColors[i][2] = light.color.z;
        lightData.lightColors[i][3] = 1.0f;
        lightData.lightParams[i][0] = light.attenuation;
        lightData.lightParams[i][1] = light.enabled ? 1.0f : 0.0f;
        lightData.lightParams[i][2] = static_cast<float>(light.type);
        lightData.lightParams[i][3] = 0.0f;
    }

    m_pipeline.UpdateLights(m_device.Context(), lightData);
}

void QuarkD3D11Renderer::EndDrawing()
{
    m_commands.FlushBatch();

    if (const D3D11RenderCallback callback = GetD3D11RenderCallback()) {
        callback(m_device.Context());
    }

    const bool vsync = m_vsyncExplicitlySet ? m_vsync : (m_targetFps != 0);
    m_commands.Present(vsync);

    if (m_windowResized)
    {
        m_windowResized = false;
        ResizeSwapChain();
    }

    const std::uint64_t freq = SDL_GetPerformanceFrequency();
    if (m_targetFps > 0) {
        const std::uint64_t targetTicks = freq / static_cast<std::uint64_t>(m_targetFps);
        while (true) {
            const std::uint64_t now = SDL_GetPerformanceCounter();
            const std::uint64_t elapsed = now - m_lastFrameCounter;
            if (elapsed >= targetTicks) {
                break;
            }
            const std::uint64_t remaining = targetTicks - elapsed;
            if (remaining > freq / 500) {
                SDL_Delay(1);
            }
        }
    }

    m_lastFrameCounter = SDL_GetPerformanceCounter();
    m_drawing = false;
}

void QuarkD3D11Renderer::ClearBackground(Color color)
{
    m_commands.Clear(color);
}

void QuarkD3D11Renderer::DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color)
{
    m_commands.DrawTriangle(v1, v2, v3, color, m_width, m_height);
}

void QuarkD3D11Renderer::DrawRectangle(float x,
                                       float y,
                                       float width,
                                       float height,
                                       Color color)
{
    DrawRectangle(Rectangle{x, y, width, height}, color);
}

void QuarkD3D11Renderer::DrawRectangle(const Rectangle &rectangle, Color color)
{
    DrawRectangleV(
        Vec2{rectangle.x, rectangle.y},
        Vec2{rectangle.width, rectangle.height},
        color);
}

void QuarkD3D11Renderer::DrawRectangleV(Vec2 position, Vec2 size, Color color)
{
    const Vec2 topLeft{position.x, position.y};
    const Vec2 topRight{position.x + size.x, position.y};
    const Vec2 bottomRight{position.x + size.x, position.y + size.y};
    const Vec2 bottomLeft{position.x, position.y + size.y};

    m_commands.DrawTriangle(topLeft, topRight, bottomRight, color, m_width, m_height);
    m_commands.DrawTriangle(topLeft, bottomRight, bottomLeft, color, m_width, m_height);
}

void QuarkD3D11Renderer::DrawRectangleLines(Rectangle rectangle,
                                             float lineWidth,
                                             Color color)
{
    const Vec2 topLeft{rectangle.x, rectangle.y};
    const Vec2 topRight{rectangle.x + rectangle.width, rectangle.y};
    const Vec2 bottomRight{rectangle.x + rectangle.width, rectangle.y + rectangle.height};
    const Vec2 bottomLeft{rectangle.x, rectangle.y + rectangle.height};

    DrawThickLine(m_commands, topLeft, topRight, lineWidth, color, m_width, m_height);
    DrawThickLine(m_commands, topRight, bottomRight, lineWidth, color, m_width, m_height);
    DrawThickLine(m_commands, bottomRight, bottomLeft, lineWidth, color, m_width, m_height);
    DrawThickLine(m_commands, bottomLeft, topLeft, lineWidth, color, m_width, m_height);
}

void QuarkD3D11Renderer::DrawRectangleRounded(Rectangle rectangle,
                                                float roundness,
                                                int segments,
                                                Color color)
{
    const float shortestSide = std::min(std::abs(rectangle.width), std::abs(rectangle.height));
    const float radius = std::clamp(roundness * shortestSide, 0.0f, shortestSide * 0.5f);
    const int cornerSegments = std::max(segments, 1);

    if (radius <= 0.0f)
    {
        DrawRectangle(rectangle, color);
        return;
    }

    const Vec2 centers[] = {
        {rectangle.x + radius, rectangle.y + radius},
        {rectangle.x + rectangle.width - radius, rectangle.y + radius},
        {rectangle.x + rectangle.width - radius, rectangle.y + rectangle.height - radius},
        {rectangle.x + radius, rectangle.y + rectangle.height - radius}
    };
    const Vec2 center{rectangle.x + rectangle.width * 0.5f,
                      rectangle.y + rectangle.height * 0.5f};

    std::vector<Vec2> points;
    points.reserve(static_cast<size_t>(cornerSegments * 4));

    for (int corner = 0; corner < 4; ++corner)
    {
        const float startAngle = -PI * 0.5f + corner * PI * 0.5f;

        for (int segment = 0; segment < cornerSegments; ++segment)
        {
            const float angle = startAngle + PI * 0.5f * segment / cornerSegments;
            points.push_back({
                centers[corner].x + std::cos(angle) * radius,
                centers[corner].y + std::sin(angle) * radius
            });
        }
    }

    for (size_t index = 1; index + 1 < points.size(); ++index)
    {
        m_commands.DrawTriangle(center, points[0], points[index], color, m_width, m_height);
    }
}

void QuarkD3D11Renderer::DrawCircle(float centerX,
                                    float centerY,
                                    float radius,
                                    Color color)
{
    DrawEllipse(centerX, centerY, radius, radius, color);
}

void QuarkD3D11Renderer::DrawCircleLines(float centerX,
                                          float centerY,
                                          float radius,
                                          Color color)
{
    constexpr int segments = 32;
    Vec2 previous{centerX + radius, centerY};

    for (int index = 1; index <= segments; ++index)
    {
        const float angle = 2.0f * PI * index / segments;
        const Vec2 current{centerX + std::cos(angle) * radius,
                           centerY + std::sin(angle) * radius};
        DrawThickLine(m_commands, previous, current, 1.0f, color, m_width, m_height);
        previous = current;
    }
}

void QuarkD3D11Renderer::DrawEllipse(float centerX,
                                     float centerY,
                                     float radiusH,
                                     float radiusV,
                                     Color color)
{
    constexpr int segments = 32;
    const Vec2 center{centerX, centerY};

    for (int index = 0; index < segments; ++index)
    {
        const float firstAngle = 2.0f * PI * index / segments;
        const float secondAngle = 2.0f * PI * (index + 1) / segments;
        const Vec2 first{centerX + std::cos(firstAngle) * radiusH,
                         centerY + std::sin(firstAngle) * radiusV};
        const Vec2 second{centerX + std::cos(secondAngle) * radiusH,
                          centerY + std::sin(secondAngle) * radiusV};

        m_commands.DrawTriangle(center, first, second, color, m_width, m_height);
    }
}

void QuarkD3D11Renderer::DrawLine(float x1, float y1, float x2, float y2, Color color)
{
    DrawLineV(Vec2{x1, y1}, Vec2{x2, y2}, color);
}

void QuarkD3D11Renderer::DrawLineV(Vec2 start, Vec2 end, Color color)
{
    DrawThickLine(m_commands, start, end, 1.0f, color, m_width, m_height);
}

void QuarkD3D11Renderer::DrawPoly(Vec2 center,
                                   int sides,
                                   float radius,
                                   float rotation,
                                   Color color)
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
        const Vec2 first{center.x + std::cos(firstAngle) * radius,
                         center.y + std::sin(firstAngle) * radius};
        const Vec2 second{center.x + std::cos(secondAngle) * radius,
                          center.y + std::sin(secondAngle) * radius};

        m_commands.DrawTriangle(center, first, second, color, m_width, m_height);
    }
}

void QuarkD3D11Renderer::BeginMode3D(const Camera3D &camera)
{
    m_viewMatrix = Mat4::lookAt(camera.position, camera.target, camera.up);
    m_viewPos = camera.position;

    if (camera.projection == CAMERA_PERSPECTIVE)
    {
        const float aspect = m_height > 0 ? static_cast<float>(m_width) / static_cast<float>(m_height)
                                          : 1.0f;
        if (camera.fovy > 0.0f && camera.fovy < 180.0f)
        {
            m_projectionMatrix = Mat4::perspective(camera.fovy * PI / 180.0f, aspect, 0.1f, 1000.0f);
        }
        else
        {
            m_projectionMatrix = Mat4::perspective(45.0f * PI / 180.0f, aspect, 0.1f, 1000.0f);
        }
    }
    else
    {
        m_projectionMatrix = Mat4::ortho(static_cast<float>(-m_width) * 0.5f,
                                         static_cast<float>(m_width) * 0.5f,
                                         static_cast<float>(-m_height) * 0.5f,
                                         static_cast<float>(m_height) * 0.5f,
                                         0.1f, 1000.0f);
    }
}

void QuarkD3D11Renderer::EndMode3D()
{
    m_matrixStack.clear();
    m_currentMatrix = Mat4::identity();
}

void QuarkD3D11Renderer::PushMatrix()
{
    m_matrixStack.push_back(m_currentMatrix);
}

void QuarkD3D11Renderer::PopMatrix()
{
    if (!m_matrixStack.empty())
    {
        m_currentMatrix = m_matrixStack.back();
        m_matrixStack.pop_back();
    }
    else
    {
        m_currentMatrix = Mat4::identity();
    }
}

void QuarkD3D11Renderer::Translate(const Vec3 &translation)
{
    m_currentMatrix = m_currentMatrix * Mat4::translation(translation.x, translation.y, translation.z);
}

void QuarkD3D11Renderer::Rotate(float angle, const Vec3 &axis)
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

    m_currentMatrix = m_currentMatrix * rotation;
}

void QuarkD3D11Renderer::Scale(const Vec3 &scale)
{
    m_currentMatrix = m_currentMatrix * Mat4::scale(scale.x, scale.y, scale.z);
}

void QuarkD3D11Renderer::MultMatrix(const Mat4 &matrix)
{
    m_currentMatrix = m_currentMatrix * matrix;
}

const float *QuarkD3D11Renderer::GetMatrixModelview()
{
    m_modelviewCapture = m_viewMatrix * m_currentMatrix;
    return m_modelviewCapture.m;
}

const float *QuarkD3D11Renderer::GetMatrixProjection()
{
    return m_projectionMatrix.m;
}

void QuarkD3D11Renderer::EnableBackfaceCulling()
{
    m_pipeline.SetBackfaceCulling(true);
}

void QuarkD3D11Renderer::DisableBackfaceCulling()
{
    m_pipeline.SetBackfaceCulling(false);
}

Mat4 QuarkD3D11Renderer::CurrentMVP() const
{
    return m_projectionMatrix * m_viewMatrix * m_currentMatrix;
}

namespace {

Vec3 SafeNormalized(const Vec3 &value, const Vec3 &fallback)
{
    const float length = value.length();
    if (length <= 1e-6f)
    {
        return fallback;
    }
    return value.normalized();
}

} // namespace

void QuarkD3D11Renderer::DrawTris3D(const Vec3 *positions, const Vec3 *normals,
                                    size_t vertexCount, const Mat4 &mvp, Color color)
{
    if (!positions || vertexCount == 0 || vertexCount % 3 != 0)
    {
        return;
    }

    std::vector<float> vertices(vertexCount * 16);
    const float r = color.r / 255.0f;
    const float g = color.g / 255.0f;
    const float b = color.b / 255.0f;
    const float a = color.a / 255.0f;

    for (size_t index = 0; index < vertexCount; ++index)
    {
        const Vec4 clip = mvp * Vec4{positions[index].x, positions[index].y,
                                     positions[index].z, 1.0f};
        float *vertex = vertices.data() + index * 16;
        vertex[0] = clip.x;
        vertex[1] = clip.y;
        vertex[2] = clip.z;
        vertex[3] = clip.w;
        vertex[4] = r;
        vertex[5] = g;
        vertex[6] = b;
        vertex[7] = a;
        vertex[8] = positions[index].x;
        vertex[9] = positions[index].y;
        vertex[10] = positions[index].z;
        vertex[11] = 1.0f;

        if (normals)
        {
            vertex[12] = normals[index].x;
            vertex[13] = normals[index].y;
            vertex[14] = normals[index].z;
            vertex[15] = 1.0f;
        }
        else
        {
            vertex[12] = 0.0f;
            vertex[13] = 0.0f;
            vertex[14] = 0.0f;
            vertex[15] = 0.0f;
        }
    }

    m_commands.Draw3D(vertices.data(), static_cast<UINT>(vertexCount),
                      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void QuarkD3D11Renderer::DrawLines3D(const Vec3 *positions, size_t vertexCount,
                                     const Mat4 &mvp, Color color)
{
    if (!positions || vertexCount == 0 || vertexCount % 2 != 0)
    {
        return;
    }

    std::vector<float> vertices(vertexCount * 16);
    const float r = color.r / 255.0f;
    const float g = color.g / 255.0f;
    const float b = color.b / 255.0f;
    const float a = color.a / 255.0f;

    for (size_t index = 0; index < vertexCount; ++index)
    {
        const Vec4 clip = mvp * Vec4{positions[index].x, positions[index].y,
                                     positions[index].z, 1.0f};
        float *vertex = vertices.data() + index * 16;
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

    m_commands.Draw3D(vertices.data(), static_cast<UINT>(vertexCount),
                      D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
}

void QuarkD3D11Renderer::Set3DView(const Mat4 &view, const Mat4 &projection)
{
    m_viewMatrix = view;
    m_projectionMatrix = projection;
}

void QuarkD3D11Renderer::DrawLine3D(Vec3 startPos, Vec3 endPos, Color color)
{
    const Vec3 positions[2] = {startPos, endPos};
    DrawLines3D(positions, 2, CurrentMVP(), color);
}

void QuarkD3D11Renderer::DrawPlane(Vec3 center, Vec2 size, Color color)
{
    const Vec3 positions[6] = {
        center + Vec3{-size.x * 0.5f, 0.0f, -size.y * 0.5f},
        center + Vec3{ size.x * 0.5f, 0.0f, -size.y * 0.5f},
        center + Vec3{ size.x * 0.5f, 0.0f,  size.y * 0.5f},
        center + Vec3{-size.x * 0.5f, 0.0f, -size.y * 0.5f},
        center + Vec3{ size.x * 0.5f, 0.0f,  size.y * 0.5f},
        center + Vec3{-size.x * 0.5f, 0.0f,  size.y * 0.5f}
    };

    const Vec3 normal{0.0f, 1.0f, 0.0f};
    const Vec3 normals[6] = {
        normal, normal, normal, normal, normal, normal
    };

    DrawTris3D(positions, normals, 6, CurrentMVP(), color);
}

void QuarkD3D11Renderer::DrawCube(Vec3 position, float width, float height, float length,
                                  Color color)
{
    const float hw = width * 0.5f;
    const float hh = height * 0.5f;
    const float hl = length * 0.5f;

    const Vec3 v[8] = {
        position + Vec3{-hw, -hh, -hl},
        position + Vec3{ hw, -hh, -hl},
        position + Vec3{ hw,  hh, -hl},
        position + Vec3{-hw,  hh, -hl},
        position + Vec3{-hw, -hh,  hl},
        position + Vec3{ hw, -hh,  hl},
        position + Vec3{ hw,  hh,  hl},
        position + Vec3{-hw,  hh,  hl}
    };

    const Vec3 positions[36] = {
        v[0], v[1], v[2],
        v[0], v[2], v[3],

        v[4], v[6], v[5],
        v[4], v[7], v[6],

        v[4], v[5], v[1],
        v[4], v[1], v[0],

        v[3], v[2], v[6],
        v[3], v[6], v[7],

        v[1], v[5], v[6],
        v[1], v[6], v[2],

        v[4], v[0], v[3],
        v[4], v[3], v[7]
    };

    const Vec3 nZ{0.0f, 0.0f, -1.0f};
    const Vec3 pZ{0.0f, 0.0f, 1.0f};
    const Vec3 nX{-1.0f, 0.0f, 0.0f};
    const Vec3 pX{1.0f, 0.0f, 0.0f};
    const Vec3 pY{0.0f, 1.0f, 0.0f};
    const Vec3 nY{0.0f, -1.0f, 0.0f};

    const Vec3 normals[36] = {
        nZ, nZ, nZ,
        nZ, nZ, nZ,

        pZ, pZ, pZ,
        pZ, pZ, pZ,

        nX, nX, nX,
        nX, nX, nX,

        pX, pX, pX,
        pX, pX, pX,

        pY, pY, pY,
        pY, pY, pY,

        nY, nY, nY,
        nY, nY, nY
    };

    DrawTris3D(positions, normals, 36, CurrentMVP(), color);
}

void QuarkD3D11Renderer::DrawCubeV(Vec3 position, Vec3 size, Color color)
{
    DrawCube(position, size.x, size.y, size.z, color);
}

void QuarkD3D11Renderer::DrawCubeWires(Vec3 position, float width, float height, float length,
                                       Color color)
{
    const float hw = width * 0.5f;
    const float hh = height * 0.5f;
    const float hl = length * 0.5f;

    const Vec3 v[8] = {
        position + Vec3{-hw, -hh, -hl},
        position + Vec3{ hw, -hh, -hl},
        position + Vec3{ hw,  hh, -hl},
        position + Vec3{-hw,  hh, -hl},
        position + Vec3{-hw, -hh,  hl},
        position + Vec3{ hw, -hh,  hl},
        position + Vec3{ hw,  hh,  hl},
        position + Vec3{-hw,  hh,  hl}
    };

    const Vec3 positions[24] = {
        v[0], v[1],
        v[1], v[2],
        v[2], v[3],
        v[3], v[0],

        v[4], v[5],
        v[5], v[6],
        v[6], v[7],
        v[7], v[4],

        v[0], v[4],
        v[1], v[5],
        v[2], v[6],
        v[3], v[7]
    };

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

void QuarkD3D11Renderer::DrawSphereEx(Vec3 centerPos, float radius, int rings, int slices,
                                      Color color)
{
    if (rings < 2 || slices < 3)
    {
        return;
    }

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    positions.reserve(static_cast<size_t>(rings) * slices * 6);
    normals.reserve(static_cast<size_t>(rings) * slices * 6);

    for (int ri = 0; ri < rings; ++ri)
    {
        for (int si = 0; si < slices; ++si)
        {
            const float phi1 = PI * static_cast<float>(ri) / static_cast<float>(rings);
            const float phi2 = PI * static_cast<float>(ri + 1) / static_cast<float>(rings);
            const float theta1 = 2.0f * PI * static_cast<float>(si) / static_cast<float>(slices);
            const float theta2 = 2.0f * PI * static_cast<float>(si + 1) / static_cast<float>(slices);

            const Vec3 a{
                radius * std::sin(phi1) * std::cos(theta1),
                radius * std::cos(phi1),
                radius * std::sin(phi1) * std::sin(theta1)
            };
            const Vec3 b{
                radius * std::sin(phi1) * std::cos(theta2),
                radius * std::cos(phi1),
                radius * std::sin(phi1) * std::sin(theta2)
            };
            const Vec3 d{
                radius * std::sin(phi2) * std::cos(theta1),
                radius * std::cos(phi2),
                radius * std::sin(phi2) * std::sin(theta1)
            };
            const Vec3 e{
                radius * std::sin(phi2) * std::cos(theta2),
                radius * std::cos(phi2),
                radius * std::sin(phi2) * std::sin(theta2)
            };

            positions.push_back(centerPos + a);
            positions.push_back(centerPos + b);
            positions.push_back(centerPos + e);
            positions.push_back(centerPos + a);
            positions.push_back(centerPos + e);
            positions.push_back(centerPos + d);

            normals.push_back(a.normalized());
            normals.push_back(b.normalized());
            normals.push_back(e.normalized());
            normals.push_back(a.normalized());
            normals.push_back(e.normalized());
            normals.push_back(d.normalized());
        }
    }

    DrawTris3D(positions.data(), normals.data(), positions.size(), CurrentMVP(), color);
}

void QuarkD3D11Renderer::DrawSphereWires(Vec3 centerPos, float radius, int rings, int slices,
                                         Color color)
{
    if (rings < 2 || slices < 3)
    {
        return;
    }

    std::vector<Vec3> positions;
    positions.reserve(static_cast<size_t>(rings + 1 + slices) * slices * 2);

    for (int ri = 0; ri <= rings; ++ri)
    {
        const float phi = PI * static_cast<float>(ri) / static_cast<float>(rings);
        for (int si = 0; si < slices; ++si)
        {
            const float t1 = 2.0f * PI * static_cast<float>(si) / static_cast<float>(slices);
            const float t2 = 2.0f * PI * static_cast<float>(si + 1) / static_cast<float>(slices);
            positions.push_back(centerPos +
                                Vec3{radius * std::sin(phi) * std::cos(t1),
                                     radius * std::cos(phi),
                                     radius * std::sin(phi) * std::sin(t1)});
            positions.push_back(centerPos +
                                Vec3{radius * std::sin(phi) * std::cos(t2),
                                     radius * std::cos(phi),
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
            positions.push_back(centerPos +
                                Vec3{radius * std::sin(p1) * std::cos(th),
                                     radius * std::cos(p1),
                                     radius * std::sin(p1) * std::sin(th)});
            positions.push_back(centerPos +
                                Vec3{radius * std::sin(p2) * std::cos(th),
                                     radius * std::cos(p2),
                                     radius * std::sin(p2) * std::sin(th)});
        }
    }

    DrawLines3D(positions.data(), positions.size(), CurrentMVP(), color);
}

void QuarkD3D11Renderer::DrawCylinder(Vec3 position, float radiusTop, float radiusBottom,
                                      float height, int slices, Color color)
{
    DrawCylinderEx(position + Vec3{0, -height * 0.5f, 0},
                   position + Vec3{0, height * 0.5f, 0},
                   radiusBottom, radiusTop, slices, color);
}

void QuarkD3D11Renderer::DrawCylinderEx(Vec3 startPos, Vec3 endPos, float startRadius,
                                        float endRadius, int sides, Color color)
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

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    positions.reserve(static_cast<size_t>(sides) * 6);
    normals.reserve(static_cast<size_t>(sides) * 6);

    for (int i = 0; i < sides; ++i)
    {
        const float a1 = 2.0f * PI * static_cast<float>(i) / static_cast<float>(sides);
        const float a2 = 2.0f * PI * static_cast<float>(i + 1) / static_cast<float>(sides);

        const Vec3 p1 = startPos + xDir * std::cos(a1) * startRadius +
                        yDir * std::sin(a1) * startRadius;
        const Vec3 p2 = startPos + xDir * std::cos(a2) * startRadius +
                        yDir * std::sin(a2) * startRadius;
        const Vec3 p3 = endPos + xDir * std::cos(a2) * endRadius +
                        yDir * std::sin(a2) * endRadius;
        const Vec3 p4 = endPos + xDir * std::cos(a1) * endRadius +
                        yDir * std::sin(a1) * endRadius;

        const Vec3 n1 = SafeNormalized(p1 - startPos, xDir);
        const Vec3 n2 = SafeNormalized(p2 - startPos, xDir);
        const Vec3 n3 = SafeNormalized(p3 - endPos, xDir);
        const Vec3 n4 = SafeNormalized(p4 - endPos, xDir);

        positions.push_back(p1);
        positions.push_back(p2);
        positions.push_back(p3);
        positions.push_back(p1);
        positions.push_back(p3);
        positions.push_back(p4);

        normals.push_back(n1);
        normals.push_back(n2);
        normals.push_back(n3);
        normals.push_back(n1);
        normals.push_back(n3);
        normals.push_back(n4);

        positions.push_back(startPos);
        positions.push_back(p2);
        positions.push_back(p1);
        positions.push_back(endPos);
        positions.push_back(p3);
        positions.push_back(p4);

        normals.push_back(Vec3{-dir.x, -dir.y, -dir.z});
        normals.push_back(Vec3{-dir.x, -dir.y, -dir.z});
        normals.push_back(Vec3{-dir.x, -dir.y, -dir.z});
        normals.push_back(dir);
        normals.push_back(dir);
        normals.push_back(dir);
    }

    DrawTris3D(positions.data(), normals.data(), positions.size(), CurrentMVP(), color);
}

void QuarkD3D11Renderer::DrawCylinderWires(Vec3 position, float radiusTop, float radiusBottom,
                                           float height, int slices, Color color)
{
    DrawCylinderWiresEx(position + Vec3{0, -height * 0.5f, 0},
                        position + Vec3{0, height * 0.5f, 0},
                        radiusBottom, radiusTop, slices, color);
}

void QuarkD3D11Renderer::DrawCylinderWiresEx(Vec3 startPos, Vec3 endPos, float startRadius,
                                             float endRadius, int slices, Color color)
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

    std::vector<Vec3> positions;
    positions.reserve(static_cast<size_t>(slices) * 6);

    for (int i = 0; i < slices; ++i)
    {
        const float a1 = 2.0f * PI * static_cast<float>(i) / static_cast<float>(slices);
        const float a2 = 2.0f * PI * static_cast<float>(i + 1) / static_cast<float>(slices);

        const Vec3 p1 = startPos + xDir * std::cos(a1) * startRadius +
                        yDir * std::sin(a1) * startRadius;
        const Vec3 p2 = startPos + xDir * std::cos(a2) * startRadius +
                        yDir * std::sin(a2) * startRadius;
        const Vec3 p3 = endPos + xDir * std::cos(a1) * endRadius +
                        yDir * std::sin(a1) * endRadius;
        const Vec3 p4 = endPos + xDir * std::cos(a2) * endRadius +
                        yDir * std::sin(a2) * endRadius;

        positions.push_back(p1);
        positions.push_back(p2);
        positions.push_back(p3);
        positions.push_back(p4);
        positions.push_back(p1);
        positions.push_back(p3);
    }

    DrawLines3D(positions.data(), positions.size(), CurrentMVP(), color);
}

void QuarkD3D11Renderer::DrawGrid(int slices, float spacing, Color color)
{
    if (slices < 1 || spacing <= 0.0f)
    {
        return;
    }

    const float half = static_cast<float>(slices) * spacing * 0.5f;

    std::vector<Vec3> positions;
    positions.reserve(static_cast<size_t>(slices + 1) * 4);

    for (int i = 0; i <= slices; ++i)
    {
        const float f = -half + static_cast<float>(i) * spacing;
        positions.push_back({f, 0.01f, -half});
        positions.push_back({f, 0.01f, half});
        positions.push_back({-half, 0.01f, f});
        positions.push_back({half, 0.01f, f});
    }

    DrawLines3D(positions.data(), positions.size(), CurrentMVP(), color);
}

namespace {

Color ReadMeshVertexColor(const Mesh& mesh, int index, Color fallback)
{
    if (!mesh.colors || index < 0 || index * 4 + 3 >= mesh.vertexCount * 4)
    {
        return fallback;
    }

    return Color{
        mesh.colors[index * 4 + 0],
        mesh.colors[index * 4 + 1],
        mesh.colors[index * 4 + 2],
        mesh.colors[index * 4 + 3]
    };
}

std::string GetModelDirectory(const char* filePath)
{
    if (!filePath)
    {
        return {};
    }

    const std::string path(filePath);
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
    {
        return {};
    }

    return path.substr(0, slash + 1);
}

} // namespace

Model QuarkD3D11Renderer::LoadModel(const char* filePath)
{
    TraceLog(LogLevel::Info, "MODEL",
             TextFormat("[D3D11] Loading 3D model: %s", filePath ? filePath : "<null>"));

    if (!filePath)
    {
        return {};
    }

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        filePath,
        aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || !scene->mRootNode)
    {
        TraceLog(LogLevel::Error, "MODEL",
                 TextFormat("[D3D11] Failed to load model %s: %s",
                            filePath, importer.GetErrorString()));
        return {};
    }

    Model model{};
    model.directory = GetModelDirectory(filePath);
    model.meshCount = static_cast<int>(scene->mNumMeshes);
    model.materialCount = static_cast<int>(scene->mNumMaterials);
    model.meshes = (model.meshCount > 0) ? new Mesh[model.meshCount]{} : nullptr;
    model.materials = (model.materialCount > 0) ? new Material[model.materialCount]{} : nullptr;
    model.meshMaterial = (model.meshCount > 0) ? new int[model.meshCount]{} : nullptr;

    for (int materialIndex = 0; materialIndex < model.materialCount; ++materialIndex)
    {
        Material& material = model.materials[materialIndex];
        material.maps = new MaterialMap[MATERIAL_MAP_BRDF + 1]{};
        material.maps[MATERIAL_MAP_ALBEDO].color = WHITE;

        aiMaterial* sourceMaterial = scene->mMaterials[materialIndex];
        aiColor4D diffuseColor{};
        if (AI_SUCCESS == aiGetMaterialColor(sourceMaterial, AI_MATKEY_COLOR_DIFFUSE, &diffuseColor))
        {
            material.maps[MATERIAL_MAP_ALBEDO].color = Color{
                static_cast<unsigned char>(std::clamp(diffuseColor.r * 255.0f, 0.0f, 255.0f)),
                static_cast<unsigned char>(std::clamp(diffuseColor.g * 255.0f, 0.0f, 255.0f)),
                static_cast<unsigned char>(std::clamp(diffuseColor.b * 255.0f, 0.0f, 255.0f)),
                static_cast<unsigned char>(std::clamp(diffuseColor.a * 255.0f, 0.0f, 255.0f))
            };
        }

        const std::array<std::pair<int, aiTextureType>, 7> textureTypes = {{
            { MATERIAL_MAP_ALBEDO, aiTextureType_BASE_COLOR },
            { MATERIAL_MAP_ALBEDO, aiTextureType_DIFFUSE },
            { MATERIAL_MAP_METALNESS, aiTextureType_METALNESS },
            { MATERIAL_MAP_NORMAL, aiTextureType_NORMALS },
            { MATERIAL_MAP_ROUGHNESS, aiTextureType_DIFFUSE_ROUGHNESS },
            { MATERIAL_MAP_OCCLUSION, aiTextureType_AMBIENT_OCCLUSION },
            { MATERIAL_MAP_EMISSION, aiTextureType_EMISSION_COLOR }
        }};

        for (const auto& [mapIndex, textureType] : textureTypes)
        {
            if (material.maps[mapIndex].texture.valid)
            {
                continue;
            }

            aiString texturePath{};
            if (AI_SUCCESS != sourceMaterial->GetTexture(textureType, 0, &texturePath))
            {
                continue;
            }

            const std::string fullTexturePath = model.directory + texturePath.C_Str();
            const ITexture loadedTexture = LoadTexture(fullTexturePath.c_str());
            material.maps[mapIndex].texture = Texture2D{
                loadedTexture.id,
                loadedTexture.width,
                loadedTexture.height,
                loadedTexture.mipmaps,
                loadedTexture.format,
                loadedTexture.valid
            };
        }
    }

    for (int meshIndex = 0; meshIndex < model.meshCount; ++meshIndex)
    {
        aiMesh* sourceMesh = scene->mMeshes[meshIndex];
        if (!sourceMesh)
        {
            continue;
        }

        Mesh& destinationMesh = model.meshes[meshIndex];
        destinationMesh.vertexCount = static_cast<int>(sourceMesh->mNumVertices);
        destinationMesh.triangleCount = static_cast<int>(sourceMesh->mNumFaces);

        if (destinationMesh.vertexCount > 0)
        {
            destinationMesh.vertices = new float[destinationMesh.vertexCount * 3]{};
            destinationMesh.normals = new float[destinationMesh.vertexCount * 3]{};
            destinationMesh.texcoords = new float[destinationMesh.vertexCount * 2]{};
        }

        if (destinationMesh.triangleCount > 0)
        {
            destinationMesh.indices = new unsigned short[destinationMesh.triangleCount * 3]{};
        }

        if (sourceMesh->HasVertexColors(0))
        {
            destinationMesh.colors = new unsigned char[destinationMesh.vertexCount * 4]{};
        }

        for (int vertexIndex = 0; vertexIndex < destinationMesh.vertexCount; ++vertexIndex)
        {
            const aiVector3D& position = sourceMesh->mVertices[vertexIndex];
            destinationMesh.vertices[vertexIndex * 3 + 0] = position.x;
            destinationMesh.vertices[vertexIndex * 3 + 1] = position.y;
            destinationMesh.vertices[vertexIndex * 3 + 2] = position.z;

            if (sourceMesh->HasNormals())
            {
                const aiVector3D& normal = sourceMesh->mNormals[vertexIndex];
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

            if (sourceMesh->HasTextureCoords(0))
            {
                const aiVector3D& uv = sourceMesh->mTextureCoords[0][vertexIndex];
                destinationMesh.texcoords[vertexIndex * 2 + 0] = uv.x;
                destinationMesh.texcoords[vertexIndex * 2 + 1] = uv.y;
            }
            else
            {
                destinationMesh.texcoords[vertexIndex * 2 + 0] = 0.0f;
                destinationMesh.texcoords[vertexIndex * 2 + 1] = 0.0f;
            }

            if (sourceMesh->HasVertexColors(0))
            {
                const aiColor4D& color = sourceMesh->mColors[0][vertexIndex];
                destinationMesh.colors[vertexIndex * 4 + 0] = static_cast<unsigned char>(std::clamp(color.r * 255.0f, 0.0f, 255.0f));
                destinationMesh.colors[vertexIndex * 4 + 1] = static_cast<unsigned char>(std::clamp(color.g * 255.0f, 0.0f, 255.0f));
                destinationMesh.colors[vertexIndex * 4 + 2] = static_cast<unsigned char>(std::clamp(color.b * 255.0f, 0.0f, 255.0f));
                destinationMesh.colors[vertexIndex * 4 + 3] = static_cast<unsigned char>(std::clamp(color.a * 255.0f, 0.0f, 255.0f));
            }
        }

        for (int faceIndex = 0; faceIndex < destinationMesh.triangleCount; ++faceIndex)
        {
            const aiFace& face = sourceMesh->mFaces[faceIndex];
            if (face.mNumIndices < 3)
            {
                continue;
            }

            destinationMesh.indices[faceIndex * 3 + 0] = static_cast<unsigned short>(face.mIndices[0]);
            destinationMesh.indices[faceIndex * 3 + 1] = static_cast<unsigned short>(face.mIndices[1]);
            destinationMesh.indices[faceIndex * 3 + 2] = static_cast<unsigned short>(face.mIndices[2]);
        }

        model.meshMaterial[meshIndex] = static_cast<int>(sourceMesh->mMaterialIndex);
    }

    TraceLog(LogLevel::Info, "MODEL",
             TextFormat("[D3D11] Model loaded successfully: %s (%d meshes, %d materials)",
                        filePath, model.meshCount, model.materialCount));
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
                            material.maps[mapIndex].texture.id,
                            material.maps[mapIndex].texture.width,
                            material.maps[mapIndex].texture.height,
                            material.maps[mapIndex].texture.mipmaps,
                            material.maps[mapIndex].texture.format,
                            material.maps[mapIndex].texture.valid
                        };
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
    model.meshCount = 0;
    model.materialCount = 0;
    model.directory.clear();
    model.id = 0;
    model.transform = Mat4::identity();
}

void QuarkD3D11Renderer::DrawModel(const Model& model,
                                   const Vec3& position,
                                   float scale,
                                   float rotationX,
                                   float rotationY,
                                   float rotationZ)
{
    Mat4 transform = Mat4::translation(position.x, position.y, position.z)
                   * Mat4::rotationY(rotationY)
                   * Mat4::rotationX(rotationX)
                   * Mat4::rotationZ(rotationZ)
                   * Mat4::scale(scale, scale, scale);
    DrawModelEx(model, transform);
}

void QuarkD3D11Renderer::DrawModelEx(const Model& model, const Mat4& transform)
{
    const Mat4 modelTransform = transform * model.transform;

    for (int i = 0; i < model.meshCount; ++i)
    {
        const Mesh& mesh = model.meshes[i];
        const Material* material = nullptr;
        if (model.meshMaterial && i >= 0 && i < model.meshCount &&
            model.meshMaterial[i] >= 0 && model.meshMaterial[i] < model.materialCount)
        {
            material = &model.materials[model.meshMaterial[i]];
        }

        DrawMesh(mesh, material ? *material : Material{}, modelTransform);
    }
}

void QuarkD3D11Renderer::DrawModelEx(const Model& model, const Mat4& transform, Color tint)
{
    const Mat4 modelTransform = transform * model.transform;
    std::vector<MaterialMap> adjustedMaps(MATERIAL_MAP_BRDF + 1, MaterialMap{});

    for (int i = 0; i < model.meshCount; ++i)
    {
        const Mesh& mesh = model.meshes[i];
        Material adjustedMaterial{};
        const Material* sourceMaterial = nullptr;

        if (model.meshMaterial && i >= 0 && i < model.meshCount &&
            model.meshMaterial[i] >= 0 && model.meshMaterial[i] < model.materialCount)
        {
            sourceMaterial = &model.materials[model.meshMaterial[i]];
        }

        if (sourceMaterial && sourceMaterial->maps)
        {
            std::copy(sourceMaterial->maps,
                      sourceMaterial->maps + static_cast<int>(adjustedMaps.size()),
                      adjustedMaps.begin());
            adjustedMaterial = *sourceMaterial;
            adjustedMaterial.maps = adjustedMaps.data();
            adjustedMaterial.maps[MATERIAL_MAP_ALBEDO].color =
                MultiplyColor(adjustedMaterial.maps[MATERIAL_MAP_ALBEDO].color, tint);
        }
        else
        {
            adjustedMaterial.maps = adjustedMaps.data();
            adjustedMaterial.maps[MATERIAL_MAP_ALBEDO].color = tint;
        }

        DrawMesh(mesh, adjustedMaterial, modelTransform);
    }
}

void QuarkD3D11Renderer::UploadMesh(Mesh& mesh, bool dynamic)
{
    (void)dynamic;
    mesh.vaoId = 0;
    mesh.vboId = 0;
    mesh.eboId = 0;
}

void QuarkD3D11Renderer::UpdateMeshBuffer(Mesh& mesh, int index, const void* data,
                                          int dataSize, int offset)
{
    if (!data || dataSize <= 0 || offset < 0)
    {
        return;
    }

    auto copyBytes = [&](void* destination, std::size_t destinationBytes)
    {
        if (!destination || static_cast<std::size_t>(offset) >= destinationBytes)
        {
            return;
        }

        const std::size_t bytesToCopy = std::min<std::size_t>(
            static_cast<std::size_t>(dataSize),
            destinationBytes - static_cast<std::size_t>(offset));
        std::memcpy(static_cast<unsigned char*>(destination) + offset, data, bytesToCopy);
    };

    switch (index)
    {
    case 0:
        copyBytes(mesh.vertices, static_cast<std::size_t>(mesh.vertexCount) * 3u * sizeof(float));
        break;
    case 1:
        copyBytes(mesh.normals, static_cast<std::size_t>(mesh.vertexCount) * 3u * sizeof(float));
        break;
    case 2:
        copyBytes(mesh.texcoords, static_cast<std::size_t>(mesh.vertexCount) * 2u * sizeof(float));
        break;
    case 6:
        copyBytes(mesh.indices, static_cast<std::size_t>(mesh.triangleCount) * 3u * sizeof(unsigned short));
        break;
    default:
        break;
    }
}

void QuarkD3D11Renderer::UnloadMesh(Mesh& mesh)
{
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

void QuarkD3D11Renderer::DrawMesh(const Mesh& mesh, const Material& material, const Mat4& transform)
{
    if (!mesh.vertices || mesh.vertexCount <= 0)
    {
        return;
    }

    ShaderProgramData *customProgram = Resolve3DShaderProgram(material);
    if (customProgram != nullptr)
    {
        DrawMeshWithShader(mesh, material, transform, *customProgram);
        return;
    }

    const ID3D11ShaderResourceView *textureResource = nullptr;
    const MaterialMap* albedoMap = nullptr;
    if (material.maps)
    {
        albedoMap = &material.maps[MATERIAL_MAP_ALBEDO];
        if (albedoMap->texture.valid)
        {
            textureResource = m_resources.ShaderResource(albedoMap->texture.id);
        }
    }

    const Mat4 finalTransform = m_currentMatrix * transform;
    const Color baseColor = albedoMap ? albedoMap->color : WHITE;
    const bool textured = textureResource != nullptr;
    const std::size_t floatsPerVertex = textured ? 18u : 16u;

    const auto transformNormal = [&](const Vec3 &normal) -> Vec3 {
        const Vec3 rotated{
            finalTransform.m[0] * normal.x + finalTransform.m[4] * normal.y +
                finalTransform.m[8] * normal.z,
            finalTransform.m[1] * normal.x + finalTransform.m[5] * normal.y +
                finalTransform.m[9] * normal.z,
            finalTransform.m[2] * normal.x + finalTransform.m[6] * normal.y +
                finalTransform.m[10] * normal.z
        };
        return SafeNormalized(rotated, Vec3{0.0f, 1.0f, 0.0f});
    };

    const auto meshNormalAt = [&](int vertexIndex) -> Vec3 {
        if (mesh.normals && vertexIndex >= 0 && vertexIndex < mesh.vertexCount)
        {
            return transformNormal(Vec3{mesh.normals[vertexIndex * 3 + 0],
                                        mesh.normals[vertexIndex * 3 + 1],
                                        mesh.normals[vertexIndex * 3 + 2]});
        }
        return Vec3{0.0f, 1.0f, 0.0f};
    };

    const auto appendVertex = [&](std::vector<float> &out, const Vec4 &world, const Vec3 &normal,
                                  Color color, float u, float v)
    {
        const Vec4 clip = m_projectionMatrix * (m_viewMatrix * world);
        out.push_back(clip.x);
        out.push_back(clip.y);
        out.push_back(clip.z);
        out.push_back(clip.w);

        if (textured)
        {
            out.push_back(u);
            out.push_back(v);
        }

        out.push_back(color.r / 255.0f);
        out.push_back(color.g / 255.0f);
        out.push_back(color.b / 255.0f);
        out.push_back(color.a / 255.0f);

        out.push_back(world.x);
        out.push_back(world.y);
        out.push_back(world.z);
        out.push_back(1.0f);

        out.push_back(normal.x);
        out.push_back(normal.y);
        out.push_back(normal.z);
        out.push_back(1.0f);
    };

    const auto worldPosAt = [&](int vertexIndex) -> Vec4 {
        return finalTransform * Vec4{
            mesh.vertices[vertexIndex * 3 + 0],
            mesh.vertices[vertexIndex * 3 + 1],
            mesh.vertices[vertexIndex * 3 + 2],
            1.0f
        };
    };

    if (mesh.indices && mesh.triangleCount > 0)
    {
        std::vector<float> indexedVertices;
        indexedVertices.reserve(static_cast<std::size_t>(mesh.triangleCount) * 3u * floatsPerVertex);

        for (int triangleIndex = 0; triangleIndex < mesh.triangleCount; ++triangleIndex)
        {
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
                continue;
            }

            const Vec4 worldA = worldPosAt(vertexIndex[0]);
            const Vec4 worldB = worldPosAt(vertexIndex[1]);
            const Vec4 worldC = worldPosAt(vertexIndex[2]);
            const Vec3 faceNormal = SafeNormalized(
                (Vec3{worldB.x, worldB.y, worldB.z} - Vec3{worldA.x, worldA.y, worldA.z})
                    .cross(Vec3{worldC.x, worldC.y, worldC.z} - Vec3{worldA.x, worldA.y, worldA.z}),
                Vec3{0.0f, 1.0f, 0.0f});

            for (int localIndex = 0; localIndex < 3; ++localIndex)
            {
                const int vi = vertexIndex[localIndex];
                const Vec3 normal = mesh.normals ? meshNormalAt(vi) : faceNormal;
                const float u = mesh.texcoords ? mesh.texcoords[vi * 2 + 0] : 0.0f;
                const float v = mesh.texcoords ? mesh.texcoords[vi * 2 + 1] : 0.0f;
                const Color vertexColor =
                    MultiplyColor(baseColor, ReadMeshVertexColor(mesh, vi, WHITE));
                appendVertex(indexedVertices, worldPosAt(vi), normal, vertexColor, u, v);
            }
        }

        if (!indexedVertices.empty())
        {
            if (textured)
            {
                m_commands.Draw3DTextured(indexedVertices.data(),
                                         static_cast<UINT>(indexedVertices.size() / 18),
                                         const_cast<ID3D11ShaderResourceView *>(textureResource));
            }
            else
            {
                m_commands.Draw3D(indexedVertices.data(),
                                  static_cast<UINT>(indexedVertices.size() / 16),
                                  D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            }
        }
        return;
    }

    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>(mesh.vertexCount) * floatsPerVertex);

    const int triangleCount = mesh.triangleCount > 0 ? mesh.triangleCount : mesh.vertexCount / 3;
    for (int i = 0; i < triangleCount; ++i)
    {
        const int viBase = i * 3;
        if (viBase + 2 >= mesh.vertexCount)
        {
            break;
        }

        const Vec4 worldA = worldPosAt(viBase + 0);
        const Vec4 worldB = worldPosAt(viBase + 1);
        const Vec4 worldC = worldPosAt(viBase + 2);
        const Vec3 faceNormal = SafeNormalized(
            (Vec3{worldB.x, worldB.y, worldB.z} - Vec3{worldA.x, worldA.y, worldA.z})
                .cross(Vec3{worldC.x, worldC.y, worldC.z} - Vec3{worldA.x, worldA.y, worldA.z}),
            Vec3{0.0f, 1.0f, 0.0f});

        for (int localIndex = 0; localIndex < 3; ++localIndex)
        {
            const int vi = viBase + localIndex;
            const Vec3 normal = mesh.normals ? meshNormalAt(vi) : faceNormal;
            const float u = mesh.texcoords ? mesh.texcoords[vi * 2 + 0] : 0.0f;
            const float v = mesh.texcoords ? mesh.texcoords[vi * 2 + 1] : 0.0f;
            const Color vertexColor = MultiplyColor(baseColor, ReadMeshVertexColor(mesh, vi, WHITE));
            appendVertex(vertices, worldPosAt(vi), normal, vertexColor, u, v);
        }
    }

    if (!vertices.empty())
    {
        if (textured)
        {
            m_commands.Draw3DTextured(vertices.data(),
                                     static_cast<UINT>(vertices.size() / 18),
                                     const_cast<ID3D11ShaderResourceView *>(textureResource));
        }
        else
        {
            m_commands.Draw3D(vertices.data(),
                              static_cast<UINT>(vertices.size() / 16),
                              D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }
    }
}

QuarkD3D11Renderer::ShaderProgramData *QuarkD3D11Renderer::Resolve3DShaderProgram(
    const Material &material)
{
    uint32_t shaderId = (material.shader && material.shader->id != 0) ? material.shader->id
                                                                      : m_currentShaderId;
    if (shaderId == 0) {
        return nullptr;
    }

    ShaderProgramData *program = GetShaderProgram(shaderId);
    if (!program || !program->vertexShader || !program->pixelShader || !program->inputLayout) {
        return nullptr;
    }

    if (!program->hasPosition) {
        return nullptr;
    }

    return program;
}

void QuarkD3D11Renderer::DrawMeshWithShader(const Mesh &mesh, const Material &material,
                                            const Mat4 &transform, ShaderProgramData &program)
{
    ID3D11ShaderResourceView *albedoResource = nullptr;
    const MaterialMap *albedoMap = nullptr;
    if (material.maps) {
        albedoMap = &material.maps[MATERIAL_MAP_ALBEDO];
        if (albedoMap->texture.valid) {
            albedoResource = m_resources.ShaderResource(albedoMap->texture.id);
        }
    }

    const uint32_t shaderId = (material.shader && material.shader->id != 0)
                                  ? material.shader->id
                                  : m_currentShaderId;

    const UINT floatsPerVertex = program.strideBytes / sizeof(float);
    const auto offsetInFloats = [](UINT byteOffset) -> UINT {
        return byteOffset == 0xFFFFFFFFu ? 0xFFFFFFFFu : byteOffset / sizeof(float);
    };
    const UINT positionOffset = offsetInFloats(program.positionOffset);
    const UINT texCoordOffset = offsetInFloats(program.texCoordOffset);
    const UINT colorOffset = offsetInFloats(program.colorOffset);
    const UINT normalOffset = offsetInFloats(program.normalOffset);
    const UINT worldPositionOffset = offsetInFloats(program.worldPositionOffset);
    const bool standardConvention = program.worldPositionOffset == 0xFFFFFFFFu;

    const Mat4 finalTransform = m_currentMatrix * transform;
    const Color baseColor = albedoMap ? albedoMap->color : WHITE;

    if (standardConvention) {
        const Mat4 normalMatrix = TransposeMat4(finalTransform.inverted());
        const Mat4 mvp = m_projectionMatrix * (m_viewMatrix * finalTransform);

        const auto setMatrixUniform = [&](const char *names[3], const Mat4 &value) {
            for (int aliasIndex = 0; aliasIndex < 3; ++aliasIndex) {
                const auto iterator = program.uniforms.find(names[aliasIndex]);
                if (iterator == program.uniforms.end()) {
                    continue;
                }
                StoreUniformValue(program, iterator->second, 0, value.m,
                                  sizeof(float) * 16, 1);
                break;
            }
        };

        const char *modelNames[3] = {"uModel", "model", "MATRIX_MODEL"};
        setMatrixUniform(modelNames, finalTransform);

        const char *viewNames[3] = {"uView", "view", "MATRIX_VIEW"};
        setMatrixUniform(viewNames, m_viewMatrix);

        const char *projectionNames[3] = {"uProjection", "projection", "MATRIX_PROJECTION"};
        setMatrixUniform(projectionNames, m_projectionMatrix);

        const char *mvpNames[3] = {"uMvp", "mvp", "MATRIX_MVP"};
        setMatrixUniform(mvpNames, mvp);

        const char *normalNames[3] = {"uNormal", "normalMatrix", "MATRIX_NORMAL"};
        setMatrixUniform(normalNames, normalMatrix);

        const float whiteColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        for (const char *name : {"uColor", "colDiffuse", "COLOR_DIFFUSE"}) {
            const auto iterator = program.uniforms.find(name);
            if (iterator == program.uniforms.end()) {
                continue;
            }
            StoreUniformValue(program, iterator->second, 0, whiteColor,
                              sizeof(float) * 4, 1);
            break;
        }
    }

    D3D11CommandContext::ShaderOverride shaderOverride = BuildShaderOverride(shaderId,
                                                                            albedoResource);
    if (!shaderOverride.Active()) {
        return;
    }

    if (material.maps)
    {
        ID3D11ShaderResourceView *whiteFallback =
            m_whiteShaderTexture.IsValid() ? m_resources.ShaderResource(m_whiteShaderTexture.id) : nullptr;
        ID3D11ShaderResourceView *blackFallback =
            m_blackShaderTexture.IsValid() ? m_resources.ShaderResource(m_blackShaderTexture.id) : nullptr;
        ID3D11ShaderResourceView *flatNormalFallback =
            m_flatNormalShaderTexture.IsValid() ? m_resources.ShaderResource(m_flatNormalShaderTexture.id) : nullptr;

        const auto fillMapSlot = [&](UINT slot, int mapIndex, ID3D11ShaderResourceView *fallback) {
            if (shaderOverride.shaderResources[slot] != nullptr) {
                return;
            }
            const MaterialMap &map = material.maps[mapIndex];
            if (map.texture.valid) {
                shaderOverride.shaderResources[slot] = m_resources.ShaderResource(map.texture.id);
            } else if (fallback) {
                shaderOverride.shaderResources[slot] = fallback;
            }
        };

        fillMapSlot(1, MATERIAL_MAP_HEIGHT, whiteFallback);
        fillMapSlot(2, MATERIAL_MAP_IRRADIANCE, whiteFallback);
        fillMapSlot(3, MATERIAL_MAP_PREFILTER, whiteFallback);
        fillMapSlot(4, MATERIAL_MAP_BRDF, whiteFallback);
        fillMapSlot(5, MATERIAL_MAP_METALNESS, whiteFallback);
        fillMapSlot(6, MATERIAL_MAP_NORMAL, flatNormalFallback);
        fillMapSlot(7, MATERIAL_MAP_ROUGHNESS, whiteFallback);
        fillMapSlot(8, MATERIAL_MAP_OCCLUSION, whiteFallback);
        fillMapSlot(9, MATERIAL_MAP_EMISSION, blackFallback);
    }

    const auto transformNormal = [&](const Vec3 &normal) -> Vec3 {
        if (standardConvention) {
            return normal;
        }
        const Vec3 rotated{
            finalTransform.m[0] * normal.x + finalTransform.m[4] * normal.y +
                finalTransform.m[8] * normal.z,
            finalTransform.m[1] * normal.x + finalTransform.m[5] * normal.y +
                finalTransform.m[9] * normal.z,
            finalTransform.m[2] * normal.x + finalTransform.m[6] * normal.y +
                finalTransform.m[10] * normal.z
        };
        return SafeNormalized(rotated, Vec3{0.0f, 1.0f, 0.0f});
    };

    const auto meshNormalAt = [&](int vertexIndex) -> Vec3 {
        if (mesh.normals && vertexIndex >= 0 && vertexIndex < mesh.vertexCount) {
            return transformNormal(Vec3{mesh.normals[vertexIndex * 3 + 0],
                                        mesh.normals[vertexIndex * 3 + 1],
                                        mesh.normals[vertexIndex * 3 + 2]});
        }
        return Vec3{0.0f, 1.0f, 0.0f};
    };

    const auto localPosAt = [&](int vertexIndex) -> Vec4 {
        return Vec4{mesh.vertices[vertexIndex * 3 + 0],
                    mesh.vertices[vertexIndex * 3 + 1],
                    mesh.vertices[vertexIndex * 3 + 2],
                    1.0f};
    };

    const auto faceNormalOf = [&](int indexA, int indexB, int indexC) -> Vec3 {
        if (!standardConvention) {
            const Vec4 worldA = finalTransform * localPosAt(indexA);
            const Vec4 worldB = finalTransform * localPosAt(indexB);
            const Vec4 worldC = finalTransform * localPosAt(indexC);
            return SafeNormalized(
                (Vec3{worldB.x - worldA.x, worldB.y - worldA.y, worldB.z - worldA.z})
                    .cross(Vec3{worldC.x - worldA.x, worldC.y - worldA.y,
                                worldC.z - worldA.z}),
                Vec3{0.0f, 1.0f, 0.0f});
        }
        const Vec4 localA = localPosAt(indexA);
        const Vec4 localB = localPosAt(indexB);
        const Vec4 localC = localPosAt(indexC);
        return SafeNormalized(
            (Vec3{localB.x - localA.x, localB.y - localA.y, localB.z - localA.z})
                .cross(Vec3{localC.x - localA.x, localC.y - localA.y, localC.z - localA.z}),
            Vec3{0.0f, 1.0f, 0.0f});
    };

    const auto writeVec = [](float *vertex, UINT offset, const float *values, int count) {
        for (int index = 0; index < count; ++index) {
            vertex[offset + index] = values[index];
        }
    };

    const auto appendVertex = [&](std::vector<float> &out, int vertexIndex,
                                  const Vec3 &normal, Color color, float u, float v)
    {
        const size_t base = out.size();
        out.resize(base + floatsPerVertex, 0.0f);
        float *vertex = out.data() + base;

        const Vec4 localPos = localPosAt(vertexIndex);
        if (positionOffset != 0xFFFFFFFFu) {
            if (standardConvention) {
                const float position[4] = {localPos.x, localPos.y, localPos.z, localPos.w};
                writeVec(vertex, positionOffset, position,
                         std::min<int>(program.positionComponents, 4));
            } else {
                const Vec4 world = finalTransform * localPos;
                const Vec4 clip = m_projectionMatrix * (m_viewMatrix * world);
                const float position[4] = {clip.x, clip.y, clip.z, clip.w};
                writeVec(vertex, positionOffset, position,
                         std::min<int>(program.positionComponents, 4));
            }
        }
        if (texCoordOffset != 0xFFFFFFFFu) {
            const float texCoord[2] = {u, v};
            writeVec(vertex, texCoordOffset, texCoord,
                     std::min<int>(program.texCoordComponents, 2));
        }
        if (colorOffset != 0xFFFFFFFFu) {
            const float vertexColor[4] = {color.r / 255.0f, color.g / 255.0f,
                                          color.b / 255.0f, color.a / 255.0f};
            writeVec(vertex, colorOffset, vertexColor,
                     std::min<int>(program.colorComponents, 4));
        }
        if (normalOffset != 0xFFFFFFFFu) {
            const float normalData[4] = {normal.x, normal.y, normal.z, 1.0f};
            writeVec(vertex, normalOffset, normalData,
                     std::min<int>(program.normalComponents, 4));
        }
        if (worldPositionOffset != 0xFFFFFFFFu) {
            const Vec4 world = finalTransform * localPos;
            const float worldData[4] = {world.x, world.y, world.z, world.w};
            writeVec(vertex, worldPositionOffset, worldData,
                     std::min<int>(program.worldPositionComponents, 4));
        }
    };

    const auto drawFilled = [&](const std::vector<float> &builtVertices) {
        if (!builtVertices.empty()) {
            m_commands.Draw3DShader(builtVertices.data(),
                                    static_cast<UINT>(builtVertices.size() / floatsPerVertex),
                                    shaderOverride);
        }
    };

    if (mesh.indices && mesh.triangleCount > 0)
    {
        std::vector<float> indexedVertices;
        indexedVertices.reserve(static_cast<std::size_t>(mesh.triangleCount) * 3u *
                                floatsPerVertex);

        for (int triangleIndex = 0; triangleIndex < mesh.triangleCount; ++triangleIndex)
        {
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
                continue;
            }

            const Vec3 faceNormal = faceNormalOf(vertexIndex[0], vertexIndex[1],
                                                 vertexIndex[2]);
            for (int localIndex = 0; localIndex < 3; ++localIndex)
            {
                const int vi = vertexIndex[localIndex];
                const Vec3 normal = mesh.normals ? meshNormalAt(vi) : faceNormal;
                const float u = mesh.texcoords ? mesh.texcoords[vi * 2 + 0] : 0.0f;
                const float v = mesh.texcoords ? mesh.texcoords[vi * 2 + 1] : 0.0f;
                const Color vertexColor =
                    MultiplyColor(baseColor, ReadMeshVertexColor(mesh, vi, WHITE));
                appendVertex(indexedVertices, vi, normal, vertexColor, u, v);
            }
        }

        drawFilled(indexedVertices);
        return;
    }

    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>(mesh.vertexCount) * floatsPerVertex);

    const int triangleCount = mesh.triangleCount > 0 ? mesh.triangleCount : mesh.vertexCount / 3;
    for (int i = 0; i < triangleCount; ++i)
    {
        const int viBase = i * 3;
        if (viBase + 2 >= mesh.vertexCount)
        {
            break;
        }

        const Vec3 faceNormal = faceNormalOf(viBase + 0, viBase + 1, viBase + 2);
        for (int localIndex = 0; localIndex < 3; ++localIndex)
        {
            const int vi = viBase + localIndex;
            const Vec3 normal = mesh.normals ? meshNormalAt(vi) : faceNormal;
            const float u = mesh.texcoords ? mesh.texcoords[vi * 2 + 0] : 0.0f;
            const float v = mesh.texcoords ? mesh.texcoords[vi * 2 + 1] : 0.0f;
            const Color vertexColor =
                MultiplyColor(baseColor, ReadMeshVertexColor(mesh, vi, WHITE));
            appendVertex(vertices, vi, normal, vertexColor, u, v);
        }
    }

    drawFilled(vertices);
}

void QuarkD3D11Renderer::DrawMeshInstanced(const Mesh& mesh,
                                           const Material& material,
                                           const Mat4* transforms,
                                           int instances)
{
    if (!transforms || instances <= 0)
    {
        return;
    }

    for (int instanceIndex = 0; instanceIndex < instances; ++instanceIndex)
    {
        DrawMesh(mesh, material, transforms[instanceIndex]);
    }
}

bool QuarkD3D11Renderer::LoadFontData(const char* filePath,
                                      int fontSize,
                                      FontData& fontData)
{
    TraceLog(LogLevel::Trace,
             "FONT",
             TextFormat("[D3D11] FreeType initializing font: %s (size: %d px)",
                        filePath ? filePath : "<null>",
                        fontSize));

    if (!filePath || fontSize <= 0) {
        TraceLog(LogLevel::Error, "FONT", "[D3D11] Invalid font path or point size.");
        return false;
    }

    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) {
        TraceLog(LogLevel::Error, "FONT", "[D3D11] Failed to initialize FreeType library.");
        return false;
    }

    FT_Face face = nullptr;
    if (FT_New_Face(library, filePath, 0, &face) != 0) {
        TraceLog(LogLevel::Error,
                 "FONT",
                 TextFormat("[D3D11] Failed to open font file: %s", filePath));
        FT_Done_FreeType(library);
        return false;
    }

    FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(fontSize));

    constexpr int atlasWidth = 1024;
    constexpr int atlasHeight = 1024;
    std::vector<uint8_t> atlas(static_cast<size_t>(atlasWidth) * atlasHeight * 4, 0);
    int penX = 1;
    int penY = 1;
    int rowHeight = 0;
    int renderedGlyphs = 0;

    for (unsigned char character = 32; character < 127; ++character) {
        if (FT_Load_Char(face, character, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) {
            continue;
        }

        FT_GlyphSlot glyph = face->glyph;
        const int glyphWidth = static_cast<int>(glyph->bitmap.width);
        const int glyphHeight = static_cast<int>(glyph->bitmap.rows);

        if (penX + glyphWidth + 1 > atlasWidth) {
            penX = 1;
            penY += rowHeight + 1;
            rowHeight = 0;
        }

        if (penY + glyphHeight + 1 > atlasHeight) {
            TraceLog(LogLevel::Error, "FONT", "[D3D11] Font atlas capacity exceeded.");
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            return false;
        }

        for (int row = 0; row < glyphHeight; ++row) {
            for (int column = 0; column < glyphWidth; ++column) {
                const size_t destination =
                    (static_cast<size_t>(penY + row) * atlasWidth + penX + column) * 4;
                atlas[destination] = 255;
                atlas[destination + 1] = 255;
                atlas[destination + 2] = 255;
                atlas[destination + 3] = glyph->bitmap.buffer[row * glyph->bitmap.pitch + column];
            }
        }

        GlyphData& data = fontData.glyphs[character - 32];
        data.uv = {
            static_cast<float>(penX) / atlasWidth,
            static_cast<float>(penY) / atlasHeight,
            static_cast<float>(glyphWidth) / atlasWidth,
            static_cast<float>(glyphHeight) / atlasHeight
        };
        data.advanceX = static_cast<float>(glyph->advance.x) / 64.0f;
        data.offsetX = static_cast<float>(glyph->bitmap_left);
        data.offsetY = static_cast<float>(glyph->bitmap_top);
        data.width = glyphWidth;
        data.height = glyphHeight;

        penX += glyphWidth + 1;
        rowHeight = std::max(rowHeight, glyphHeight);
        ++renderedGlyphs;
    }

    fontData.atlasTexture = m_resources.CreateTexture(
        m_device.Get(), atlas.data(), atlasWidth, atlasHeight);
    fontData.baseSize = fontSize;
    fontData.ascent = static_cast<int>(face->size->metrics.ascender / 64);
    fontData.descent = static_cast<int>(face->size->metrics.descender / 64);
    fontData.lineHeight = static_cast<int>(face->size->metrics.height / 64);

    TraceLog(LogLevel::Info,
             "FONT",
             TextFormat("[D3D11] Font rasterized: %s (%d glyphs, Atlas: %dx%d, Ascent: %d, "
                        "Descent: %d, LineHeight: %d)",
                        filePath,
                        renderedGlyphs,
                        atlasWidth,
                        atlasHeight,
                        fontData.ascent,
                        fontData.descent,
                        fontData.lineHeight));

    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return fontData.atlasTexture.IsValid();
}

const QuarkD3D11Renderer::FontData* QuarkD3D11Renderer::GetFontData(IFont font) const
{
    const auto iterator = m_fonts.find(font.id);
    return iterator == m_fonts.end() ? nullptr : &iterator->second;
}

uint32_t QuarkD3D11Renderer::EnsureDefaultFont()
{
    if (m_defaultFontId != 0) {
        return m_defaultFontId;
    }

    const char* path = DefaultFontPath();
    if (!path) {
        TraceLog(LogLevel::Error, "FONT", "[D3D11] No default system font was found.");
        return 0;
    }

    FontData fontData{};
    if (!LoadFontData(path, 32, fontData)) {
        return 0;
    }

    m_defaultFontId = m_nextFontId++;
    m_fonts.emplace(m_defaultFontId, std::move(fontData));
    TraceLog(LogLevel::Info,
             "FONT",
             TextFormat("[D3D11] Default font loaded (Font ID: %u)", m_defaultFontId));
    return m_defaultFontId;
}

void QuarkD3D11Renderer::DrawTextWithFontData(const FontData& fontData,
                                              const char* text,
                                              Vec2 position,
                                              float fontSize,
                                              float spacing,
                                              Color tint)
{
    if (!text || fontData.baseSize <= 0 || fontSize <= 0.0f) {
        return;
    }

    const float scale = fontSize / fontData.baseSize;
    const float baseline = fontData.ascent * scale;
    const float lineHeight = fontData.lineHeight * scale;
    float cursorX = position.x;
    float cursorY = position.y;
    bool firstGlyph = true;

    for (const char* character = text; *character != '\0'; ++character) {
        if (*character == '\n') {
            cursorX = position.x;
            cursorY += lineHeight;
            firstGlyph = true;
            continue;
        }

        const unsigned char code = static_cast<unsigned char>(*character);
        if (code < 32 || code >= 127) {
            continue;
        }

        const GlyphData& glyph = fontData.glyphs[code - 32];
        if (!firstGlyph) {
            cursorX += spacing;
        }
        firstGlyph = false;

        const float glyphX = cursorX + glyph.offsetX * scale;
        const float glyphY = cursorY + baseline - glyph.offsetY * scale;
        const float glyphWidth = glyph.width * scale;
        const float glyphHeight = glyph.height * scale;

        if (glyphWidth > 0.0f && glyphHeight > 0.0f) {
            const Rectangle sourcePixels{
                glyph.uv.x * fontData.atlasTexture.width,
                glyph.uv.y * fontData.atlasTexture.height,
                glyph.uv.width * fontData.atlasTexture.width,
                glyph.uv.height * fontData.atlasTexture.height
            };

            m_commands.DrawTextureQuad(
                fontData.atlasTexture,
                sourcePixels,
                {glyphX, glyphY, glyphWidth, glyphHeight},
                {0.0f, 0.0f},
                0.0f,
                tint,
                m_width,
                m_height);
        }

        cursorX += glyph.advanceX * scale;
    }
}

Vec2 QuarkD3D11Renderer::MeasureTextWithFontData(const FontData& fontData,
                                                 const char* text,
                                                 float fontSize,
                                                 float spacing) const
{
    if (!text || fontData.baseSize <= 0 || fontSize <= 0.0f) {
        return {};
    }

    const float scale = fontSize / fontData.baseSize;
    const float lineHeight = fontData.lineHeight * scale;
    float width = 0.0f;
    float maximumWidth = 0.0f;
    bool firstGlyph = true;
    int lineCount = 1;

    for (const char* character = text; *character != '\0'; ++character) {
        if (*character == '\n') {
            maximumWidth = std::max(maximumWidth, width);
            width = 0.0f;
            firstGlyph = true;
            ++lineCount;
            continue;
        }

        const unsigned char code = static_cast<unsigned char>(*character);
        if (code < 32 || code >= 127) {
            continue;
        }

        if (!firstGlyph) {
            width += spacing;
        }
        firstGlyph = false;
        width += fontData.glyphs[code - 32].advanceX * scale;
    }

    return {std::max(maximumWidth, width), lineHeight * lineCount};
}

IFont QuarkD3D11Renderer::LoadFont(const char* filePath, int fontSize)
{
    if (!filePath) {
        return IFont{EnsureDefaultFont()};
    }

    FontData fontData{};
    if (!LoadFontData(filePath, fontSize, fontData)) {
        TraceLog(LogLevel::Error, "FONT", TextFormat("[D3D11] Failed to load font: %s", filePath));
        return {};
    }

    const uint32_t id = m_nextFontId++;
    m_fonts.emplace(id, std::move(fontData));
    TraceLog(LogLevel::Info,
             "FONT",
             TextFormat("[D3D11] Font loaded successfully: %s (Font ID: %u)", filePath, id));
    return IFont{id};
}

void QuarkD3D11Renderer::UnloadFont(IFont& font)
{
    const auto iterator = m_fonts.find(font.id);
    if (iterator != m_fonts.end()) {
        m_resources.DestroyTexture(iterator->second.atlasTexture.id);
        TraceLog(LogLevel::Info,
                 "FONT",
                 TextFormat("[D3D11] Font unloaded (Font ID: %u, Atlas ID: %u)",
                            font.id,
                            iterator->second.atlasTexture.id));
        m_fonts.erase(iterator);
    }

    if (font.id == m_defaultFontId) {
        m_defaultFontId = 0;
    }
    font = {};
}

void QuarkD3D11Renderer::DrawText(const char* text, int x, int y, int fontSize, Color color)
{
    const uint32_t fontId = EnsureDefaultFont();
    const FontData* fontData = GetFontData(IFont{fontId});
    if (fontData) {
        DrawTextWithFontData(*fontData, text, {static_cast<float>(x), static_cast<float>(y)},
                             static_cast<float>(fontSize), 0.0f, color);
    }
}

void QuarkD3D11Renderer::DrawTextEx(IFont font,
                                    const char* text,
                                    Vec2 position,
                                    float fontSize,
                                    float spacing,
                                    Color tint)
{
    const FontData* fontData = GetFontData(font);
    if (fontData) {
        DrawTextWithFontData(*fontData, text, position, fontSize, spacing, tint);
    }
}

Vec2 QuarkD3D11Renderer::MeasureTextEx(IFont font,
                                       const char* text,
                                       float fontSize,
                                       float spacing)
{
    const FontData* fontData = GetFontData(font);
    return fontData ? MeasureTextWithFontData(*fontData, text, fontSize, spacing) : Vec2{};
}

int QuarkD3D11Renderer::MeasureText(const char* text, int fontSize)
{
    const uint32_t fontId = EnsureDefaultFont();
    const FontData* fontData = GetFontData(IFont{fontId});
    return fontData
               ? static_cast<int>(std::round(
                     MeasureTextWithFontData(*fontData, text, static_cast<float>(fontSize), 0.0f).x))
               : 0;
}

ITexture QuarkD3D11Renderer::LoadTexture(const char *filePath)
{
    TraceLog(LogLevel::Trace, "TEXTURE",
             TextFormat("[D3D11] Loading texture from: %s", filePath ? filePath : "<null>"));

    if (!filePath) {
        return ITexture{};
    }

    std::error_code pathError;
    std::string cacheKey = std::filesystem::weakly_canonical(filePath, pathError).string();
    if (pathError || cacheKey.empty()) {
        cacheKey = std::filesystem::path(filePath).lexically_normal().string();
    }

    const auto cached = m_textureCache.find(cacheKey);
    if (cached != m_textureCache.end()) {
        cached->second.references++;
        TraceLog(LogLevel::Trace, "TEXTURE",
                 TextFormat("[D3D11] Reusing cached texture: %s (ID: %u, References: %d)",
                            filePath, cached->second.texture.id, cached->second.references));
        return cached->second.texture;
    }

    ImageFileData image;
    ITexture texture{};

    if (!LoadImageFile(filePath, image, 4)) {
        TraceLog(LogLevel::Error, "TEXTURE",
                 TextFormat("[D3D11] Failed to load texture: %s", filePath ? filePath : "<null>"));
        return texture;
    }

    texture = m_resources.CreateTexture(
        m_device.Get(), image.pixels.data(), image.width, image.height);

    if (texture.IsValid()) {
        m_textureCache.emplace(cacheKey, CachedTexture{texture, 1});
        m_textureCacheKeys.emplace(texture.id, cacheKey);
        TraceLog(LogLevel::Info, "TEXTURE",
                 TextFormat("[D3D11] Texture loaded successfully: %s (%dx%d, %zu bytes, ID: %u)",
                            filePath ? filePath : "<null>",
                            texture.width,
                            texture.height,
                            image.pixels.size(),
                            texture.id));
    } else {
        TraceLog(LogLevel::Error, "TEXTURE",
                 TextFormat("[D3D11] Failed to upload texture: %s",
                            filePath ? filePath : "<null>"));
    }

    return texture;
}

ITexture QuarkD3D11Renderer::GetRenderTextureTexture(IRenderTexture target)
{
    return target.texture;
}

void QuarkD3D11Renderer::UnloadTexture(ITexture &texture)
{
    if (texture.id != 0) {
        const auto cacheKey = m_textureCacheKeys.find(texture.id);
        if (cacheKey != m_textureCacheKeys.end()) {
            const auto cached = m_textureCache.find(cacheKey->second);
            if (cached != m_textureCache.end()) {
                cached->second.references--;
                if (cached->second.references > 0) {
                    TraceLog(LogLevel::Trace, "TEXTURE",
                             TextFormat("[D3D11] Released cached texture (ID: %u, References: %d)",
                                        texture.id, cached->second.references));
                    texture = {};
                    return;
                }
                m_textureCache.erase(cached);
            }
            m_textureCacheKeys.erase(cacheKey);
        }

        TraceLog(LogLevel::Info, "TEXTURE",
                 TextFormat("[D3D11] Texture unloaded (ID: %u, %dx%d)",
                            texture.id,
                            texture.width,
                            texture.height));
        m_resources.DestroyTexture(texture.id);
    }
    texture = {};
}

IRenderTexture QuarkD3D11Renderer::LoadRenderTexture(int width, int height)
{
    TraceLog(LogLevel::Trace, "RENDER_TARGET",
             TextFormat("[D3D11] Creating render texture: %dx%d", width, height));

    IRenderTexture target = m_resources.CreateRenderTexture(m_device.Get(), width, height);
    if (isRenderTextureValid(target)) {
        TraceLog(LogLevel::Info, "RENDER_TARGET",
                 TextFormat("[D3D11] Render texture created: %dx%d (Target ID: %u, Color Tex ID: %u)",
                            width,
                            height,
                            target.id,
                            target.texture.id));
    }

    return target;
}

void QuarkD3D11Renderer::UnloadRenderTexture(IRenderTexture target)
{
    if (target.id != 0) {
        TraceLog(LogLevel::Info, "RENDER_TARGET",
                 TextFormat("[D3D11] Render texture unloaded (Target ID: %u, Color Tex ID: %u)",
                            target.id,
                            target.texture.id));
        m_resources.DestroyRenderTexture(target.id);
    }
}

ITexture QuarkD3D11Renderer::GenCheckerTexture(int width,
                                               int height,
                                               int cellSize,
                                               Color colorA,
                                               Color colorB)
{
    if (width <= 0 || height <= 0 || cellSize <= 0) {
        return {};
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Color color = ((x / cellSize + y / cellSize) % 2 == 0) ? colorA : colorB;
            const size_t index = (static_cast<size_t>(y) * width + x) * 4;
            pixels[index] = color.r;
            pixels[index + 1] = color.g;
            pixels[index + 2] = color.b;
            pixels[index + 3] = color.a;
        }
    }

    ITexture texture = m_resources.CreateTexture(
        m_device.Get(), pixels.data(), width, height);

    if (texture.IsValid()) {
        TraceLog(LogLevel::Info, "TEXTURE",
                 TextFormat("[D3D11] Generated checker texture: %dx%d (Cell: %dpx, ID: %u)",
                            width,
                            height,
                            cellSize,
                            texture.id));
    }

    return texture;
}

bool QuarkD3D11Renderer::isTextureValid(ITexture &texture)
{
    return texture.IsValid() && m_resources.ShaderResource(texture.id) != nullptr;
}

bool QuarkD3D11Renderer::isRenderTextureValid(IRenderTexture &target)
{
    return target.id != 0 && target.texture.IsValid() &&
           m_resources.RenderTarget(target.id) != nullptr;
}

Image QuarkD3D11Renderer::ReadTextureImage(const ITexture &texture)
{
    if (!texture.IsValid() || texture.id == 0) return Image{};

    m_commands.FlushBatch();

    const size_t bytes = static_cast<size_t>(texture.width) * texture.height * 4;
    void *buf = MemAlloc(bytes);
    if (!buf) return Image{};

    if (!m_resources.ReadPixels(m_device.Context(), texture.id, buf, texture.width, texture.height))
    {
        MemFree(buf);
        return Image{};
    }

    Image img{};
    img.data = buf;
    img.width = texture.width;
    img.height = texture.height;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    TraceLog(LogLevel::Info, "IMAGE", TextFormat("[D3D11] Read texture pixels back to CPU: %dx%d (ID: %u)",
        texture.width, texture.height, texture.id));
    return img;
}

Image QuarkD3D11Renderer::ReadScreenImage()
{
    if (m_width <= 0 || m_height <= 0) return Image{};

    m_commands.FlushBatch();

    const size_t bytes = static_cast<size_t>(m_width) * m_height * 4;
    void *buf = MemAlloc(bytes);
    if (!buf) return Image{};

    if (!m_swapChain.ReadBackBufferPixels(buf))
    {
        MemFree(buf);
        return Image{};
    }

    Image img{};
    img.data = buf;
    img.width = m_width;
    img.height = m_height;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    TraceLog(LogLevel::Info, "IMAGE", TextFormat("[D3D11] Read backbuffer pixels to CPU: %dx%d", m_width, m_height));
    return img;
}

void QuarkD3D11Renderer::DrawTexture(const ITexture &texture,
                                      float x,
                                      float y,
                                      Color tint)
{
    DrawTexturePro(texture,
                   {0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)},
                   {x, y, static_cast<float>(texture.width), static_cast<float>(texture.height)},
                   {0.0f, 0.0f},
                   0.0f,
                   tint);
}

void QuarkD3D11Renderer::DrawTextureV(const ITexture &texture, Vec2 position, Color tint)
{
    DrawTexture(texture, position.x, position.y, tint);
}

void QuarkD3D11Renderer::DrawTextureEx(const ITexture &texture,
                                       Vec2 position,
                                       float rotation,
                                       float scale,
                                       Color tint)
{
    DrawTexturePro(texture,
                   {0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height)},
                   {position.x, position.y, texture.width * scale, texture.height * scale},
                   {texture.width * scale * 0.5f, texture.height * scale * 0.5f},
                   rotation,
                   tint);
}

void QuarkD3D11Renderer::DrawTextureRec(const ITexture &texture,
                                        Rectangle source,
                                        Vec2 position,
                                        Color tint)
{
    DrawTexturePro(texture, source, {position.x, position.y, source.width, source.height},
                   {0.0f, 0.0f}, 0.0f, tint);
}

void QuarkD3D11Renderer::DrawTexturePro(ITexture texture,
                                        Rectangle source,
                                        Rectangle destination,
                                        Vec2 origin,
                                        float rotation,
                                        Color tint)
{
    if (isTextureValid(texture)) {
        m_commands.DrawTextureQuad(texture,
                                   source,
                                   destination,
                                   origin,
                                   rotation,
                                   tint,
                                   m_width,
                                   m_height);
    }
}

void QuarkD3D11Renderer::DrawTextureTiled(ITexture texture,
                                          float scale,
                                          Vec2 offset,
                                          Color tint)
{
    if (!isTextureValid(texture) || scale <= 0.0f) {
        return;
    }

    const int columns = static_cast<int>(std::ceil(m_width / (texture.width * scale))) + 1;
    const int rows = static_cast<int>(std::ceil(m_height / (texture.height * scale))) + 1;

    for (int y = -1; y < rows; ++y) {
        for (int x = -1; x < columns; ++x) {
            DrawTexture(texture,
                        offset.x + x * texture.width * scale,
                        offset.y + y * texture.height * scale,
                        tint);
        }
    }
}

void QuarkD3D11Renderer::DrawTextureNPatch(ITexture texture,
                                           NPatchInfo np,
                                           Rectangle dst,
                                           Vec2 origin,
                                           float rotation,
                                           Color tint)
{
    if (texture.id == 0) return;

    const float dLeft   = static_cast<float>(np.left);
    const float dTop    = static_cast<float>(np.top);
    const float dRight  = static_cast<float>(np.right);
    const float dBottom = static_cast<float>(np.bottom);
    const float dMiddleW = dst.width  - dLeft - dRight;
    const float dMiddleH = dst.height - dTop  - dBottom;

    const float sLeft   = np.source.x + dLeft;
    const float sTop    = np.source.y + dTop;
    const float sRight  = np.source.x + np.source.width  - dRight;
    const float sBottom = np.source.y + np.source.height - dBottom;

    auto patch = [&](float sx, float sy, float sw, float sh,
                     float dx, float dy, float dw, float dh) {
        if (sw <= 0.f || sh <= 0.f || dw <= 0.f || dh <= 0.f) return;
        Rectangle src{ sx, sy, sw, sh };
        Rectangle dpt{ dst.x + dx, dst.y + dy, dw, dh };
        DrawTexturePro(texture, src, dpt, origin, rotation, tint);
    };

    if (np.layout == NPATCH_THREE_PATCH_HORIZONTAL) {
        patch(np.source.x, np.source.y, dLeft,   np.source.height, 0.f, 0.f, dLeft, dst.height);
        patch(sLeft, np.source.y, np.source.width - dLeft - dRight, np.source.height,
              dLeft, 0.f, dMiddleW, dst.height);
        patch(sRight, np.source.y, dRight, np.source.height, dLeft + dMiddleW, 0.f, dRight, dst.height);
        return;
    }

    if (np.layout == NPATCH_THREE_PATCH_VERTICAL) {
        patch(np.source.x, np.source.y, np.source.width, dTop, 0.f, 0.f, dst.width, dTop);
        patch(np.source.x, sTop, np.source.width, np.source.height - dTop - dBottom,
              0.f, dTop, dst.width, dMiddleH);
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
    patch(sLeft, sTop, np.source.width - dLeft - dRight, np.source.height - dTop - dBottom,
          dLeft, dTop, dMiddleW, dMiddleH);
}

void QuarkD3D11Renderer::BeginMode2D(const Camera2D& camera)
{
    m_commands.BeginMode2D(camera);
}

void QuarkD3D11Renderer::EndMode2D()
{
    m_commands.EndMode2D();
}

void QuarkD3D11Renderer::BeginTextureMode(IRenderTexture target)
{
    m_commands.BeginTextureMode(target);
}

void QuarkD3D11Renderer::EndTextureMode()
{
    m_commands.EndTextureMode(m_width, m_height);
}

QuarkD3D11Renderer::ShaderProgramData *QuarkD3D11Renderer::GetShaderProgram(uint32_t shaderId)
{
    if (shaderId == 0) {
        return nullptr;
    }

    const auto iterator = m_shaderPrograms.find(shaderId);
    return iterator == m_shaderPrograms.end() ? nullptr : &iterator->second;
}

void QuarkD3D11Renderer::BuildShaderProgram(ShaderProgramData &program,
                                            const char *vsSource,
                                            const char *fsSource)
{
    ID3D11Device *device = m_device.Get();

    const auto vertexBytecode = m_shaderCompiler.Compile(vsSource, "main", "vs_5_0");
    const auto pixelBytecode = m_shaderCompiler.Compile(fsSource, "main", "ps_5_0");

    d3d11::ThrowIfFailed(device->CreateVertexShader(vertexBytecode->GetBufferPointer(),
                                                    vertexBytecode->GetBufferSize(), nullptr,
                                                    &program.vertexShader),
                         "ID3D11Device::CreateVertexShader");
    d3d11::ThrowIfFailed(device->CreatePixelShader(pixelBytecode->GetBufferPointer(),
                                                   pixelBytecode->GetBufferSize(), nullptr,
                                                   &program.pixelShader),
                         "ID3D11Device::CreatePixelShader");

    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> vertexReflector = nullptr;
    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> pixelReflector = nullptr;

    d3d11::ThrowIfFailed(D3DReflect(vertexBytecode->GetBufferPointer(),
                                    vertexBytecode->GetBufferSize(),
                                    IID_ID3D11ShaderReflection,
                                    reinterpret_cast<void **>(vertexReflector.GetAddressOf())),
                         "D3DReflect");
    d3d11::ThrowIfFailed(D3DReflect(pixelBytecode->GetBufferPointer(),
                                    pixelBytecode->GetBufferSize(),
                                    IID_ID3D11ShaderReflection,
                                    reinterpret_cast<void **>(pixelReflector.GetAddressOf())),
                         "D3DReflect");

    D3D11_SHADER_DESC vertexDescription{};
    vertexReflector->GetDesc(&vertexDescription);

    std::vector<D3D11_INPUT_ELEMENT_DESC> inputElements;
    inputElements.reserve(vertexDescription.InputParameters);

    UINT byteOffset = 0;
    for (UINT index = 0; index < vertexDescription.InputParameters; ++index) {
        D3D11_SIGNATURE_PARAMETER_DESC signature{};
        vertexReflector->GetInputParameterDesc(index, &signature);

        const UINT components = CountMaskComponents(signature.Mask);
        D3D11_INPUT_ELEMENT_DESC element{};
        element.SemanticName = signature.SemanticName;
        element.SemanticIndex = signature.SemanticIndex;
        element.Format = SignatureFormat(signature.ComponentType, components);
        element.InputSlot = 0;
        element.AlignedByteOffset = byteOffset;
        element.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        element.InstanceDataStepRate = 0;
        inputElements.push_back(element);

        program.attributes[signature.SemanticName] = static_cast<int>(index);

        if (SemanticEquals(signature.SemanticName, "POSITION")) {
            program.positionOffset = byteOffset;
            program.positionComponents = components;
            program.hasPosition = true;
        } else if (SemanticEquals(signature.SemanticName, "TEXCOORD") &&
                   program.texCoordOffset == 0xFFFFFFFFu) {
            program.texCoordOffset = byteOffset;
            program.texCoordComponents = components;
        } else if (SemanticEquals(signature.SemanticName, "COLOR")) {
            program.colorOffset = byteOffset;
            program.colorComponents = components;
        } else if (SemanticEquals(signature.SemanticName, "NORMAL")) {
            program.normalOffset = byteOffset;
            program.normalComponents = components;
        } else if (SemanticEquals(signature.SemanticName, "WORLD_POSITION")) {
            program.worldPositionOffset = byteOffset;
            program.worldPositionComponents = components;
        }

        byteOffset += components * sizeof(float);
    }

    program.strideBytes = byteOffset;

    d3d11::ThrowIfFailed(
        device->CreateInputLayout(inputElements.data(),
                                  static_cast<UINT>(inputElements.size()),
                                  vertexBytecode->GetBufferPointer(),
                                  vertexBytecode->GetBufferSize(),
                                  &program.inputLayout),
        "ID3D11Device::CreateInputLayout shader");

    std::unordered_map<std::string, UINT> constantBufferBases;
    UINT totalConstantSize = 0;

    const auto reflectStage =
        [&](ID3D11ShaderReflection *reflector) {
            if (!reflector) {
                return;
            }

            D3D11_SHADER_DESC stageDescription{};
            reflector->GetDesc(&stageDescription);

            for (UINT bufferIndex = 0; bufferIndex < stageDescription.ConstantBuffers;
                 ++bufferIndex) {
                auto *constantBuffer = reflector->GetConstantBufferByIndex(bufferIndex);
                D3D11_SHADER_BUFFER_DESC bufferDescription{};
                if (FAILED(constantBuffer->GetDesc(&bufferDescription)) ||
                    bufferDescription.Type != D3D11_CT_CBUFFER) {
                    continue;
                }

                const std::string bufferName =
                    bufferDescription.Name ? bufferDescription.Name : "";
                UINT baseOffset = 0;
                const auto baseIterator = constantBufferBases.find(bufferName);
                if (baseIterator != constantBufferBases.end()) {
                    baseOffset = baseIterator->second;
                } else {
                    baseOffset = AlignTo16(totalConstantSize);
                    totalConstantSize = baseOffset + AlignTo16(bufferDescription.Size);
                    constantBufferBases.emplace(bufferName, baseOffset);
                }

                for (UINT variableIndex = 0; variableIndex < bufferDescription.Variables;
                     ++variableIndex) {
                    auto *variable = constantBuffer->GetVariableByIndex(variableIndex);
                    D3D11_SHADER_VARIABLE_DESC variableDescription{};
                    if (FAILED(variable->GetDesc(&variableDescription))) {
                        continue;
                    }

                    const std::string uniformName =
                        variableDescription.Name ? variableDescription.Name : "";
                    if (uniformName.empty() || program.uniforms.count(uniformName)) {
                        continue;
                    }

                    ShaderUniformInfo info{};
                    info.name = uniformName;
                    info.offset = baseOffset + variableDescription.StartOffset;
                    info.size = variableDescription.Size;
                    program.uniformInfos.push_back(std::move(info));
                    program.uniforms.emplace(uniformName,
                                             static_cast<int>(program.uniformInfos.size()) - 1);
                }
            }
        };

    reflectStage(vertexReflector.Get());
    reflectStage(pixelReflector.Get());

    if (totalConstantSize > 0) {
        EnsureConstantCapacity(program, AlignTo16(totalConstantSize));
        TraceLog(LogLevel::Trace, "SHADER",
                 TextFormat("[D3D11] Reflected shader constants (%u bytes, %zu uniforms)",
                            program.constantBufferSize,
                            program.uniformInfos.size()));
    }
}

void QuarkD3D11Renderer::EnsureConstantCapacity(ShaderProgramData &program, size_t byteCount)
{
    const size_t aligned = static_cast<size_t>(AlignTo16(static_cast<UINT>(
        byteCount > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<UINT>(byteCount))));

    if (aligned > program.constantStaging.size()) {
        const size_t grown = std::max(aligned, program.constantStaging.size() * 2);
        program.constantStaging.resize(grown, 0);
    }

    if (program.constantStaging.size() > program.constantBufferSize &&
        m_device.Get() != nullptr) {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(program.constantStaging.size());
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        d3d11::ThrowIfFailed(m_device.Get()->CreateBuffer(&description, nullptr,
                                                          &program.constantBuffer),
                             "ID3D11Device::CreateBuffer shader constants");
        program.constantBufferSize = description.ByteWidth;
        program.dirty = true;
    }
}

void QuarkD3D11Renderer::UploadConstantBuffer(ShaderProgramData &program)
{
    ID3D11DeviceContext *context = m_device.Context();
    if (!context || !program.constantBuffer || !program.dirty || program.constantStaging.empty()) {
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(program.constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                            &mapped))) {
        TraceLog(LogLevel::Warn, "SHADER", "[D3D11] Failed to map shader constant buffer.");
        return;
    }

    const size_t bytes = std::min<size_t>(program.constantStaging.size(),
                                          program.constantBufferSize);
    std::memcpy(mapped.pData, program.constantStaging.data(), bytes);
    context->Unmap(program.constantBuffer.Get(), 0);
    program.dirty = false;
}

void QuarkD3D11Renderer::StoreUniformValue(ShaderProgramData &program, int locIndex,
                                           int uniformType, const void *value,
                                           size_t elementBytes, int count)
{
    if (locIndex < 0 || value == nullptr || count <= 0 || elementBytes == 0) {
        return;
    }

    if (locIndex >= static_cast<int>(program.uniformInfos.size())) {
        TraceLog(LogLevel::Warn, "SHADER",
                 TextFormat("[D3D11] Ignoring uniform write to unknown location %d.", locIndex));
        return;
    }

    const ShaderUniformInfo &info = program.uniformInfos[static_cast<size_t>(locIndex)];
    const size_t totalBytes = static_cast<size_t>(count) * elementBytes;

    EnsureConstantCapacity(program, static_cast<size_t>(info.offset) + totalBytes);
    std::memcpy(program.constantStaging.data() + info.offset, value, totalBytes);

    auto &storage = program.uniformValues[locIndex];
    storage.assign(static_cast<const uint8_t *>(value),
                   static_cast<const uint8_t *>(value) + totalBytes);
    program.uniformTypes[locIndex] = uniformType;
    program.dirty = true;
}

void QuarkD3D11Renderer::RegisterShaderTexture(ShaderProgramData &program, int locIndex,
                                               uint32_t textureId)
{
    if (locIndex < 0) {
        return;
    }

    if (textureId != 0 && program.textureSlots.find(locIndex) == program.textureSlots.end()) {
        constexpr UINT maxShaderSlots = 7;
        if (program.nextTextureSlot > maxShaderSlots) {
            TraceLog(LogLevel::Warn, "SHADER",
                     "[D3D11] Shader texture slot limit reached (slots 1..7 available).");
            program.textureSlots[locIndex] = maxShaderSlots;
        } else {
            program.textureSlots[locIndex] = program.nextTextureSlot++;
        }
    }

    program.textureIds[locIndex] = textureId;
    program.dirty = true;
}

D3D11CommandContext::ShaderOverride QuarkD3D11Renderer::BuildShaderOverride(
    uint32_t shaderId, ID3D11ShaderResourceView *slot0Fallback)
{
    D3D11CommandContext::ShaderOverride shaderOverride{};

    ShaderProgramData *program = GetShaderProgram(shaderId);
    if (!program || !program->vertexShader || !program->pixelShader ||
        !program->inputLayout || !program->hasPosition) {
        return shaderOverride;
    }

    UploadConstantBuffer(*program);

    shaderOverride.vertexShader = program->vertexShader.Get();
    shaderOverride.pixelShader = program->pixelShader.Get();
    shaderOverride.inputLayout = program->inputLayout.Get();
    shaderOverride.constantBuffer =
        program->constantBuffer ? program->constantBuffer.Get() : nullptr;
    shaderOverride.strideBytes = program->strideBytes;
    shaderOverride.positionOffset = program->positionOffset;
    shaderOverride.texCoordOffset = program->texCoordOffset;
    shaderOverride.colorOffset = program->colorOffset;

    for (const auto &[location, textureId] : program->textureIds) {
        if (textureId == 0) {
            continue;
        }

        UINT slot = 0;
        const auto slotIterator = program->textureSlots.find(location);
        if (slotIterator != program->textureSlots.end()) {
            slot = slotIterator->second;
        } else if (shaderOverride.shaderResources[0] == nullptr) {
            slot = 0;
        } else {
            continue;
        }

        if (slot < 8) {
            shaderOverride.shaderResources[slot] = m_resources.ShaderResource(textureId);
        }
    }

    if (!shaderOverride.shaderResources[0]) {
        if (slot0Fallback) {
            shaderOverride.shaderResources[0] = slot0Fallback;
        } else if (m_whiteShaderTexture.IsValid()) {
            shaderOverride.shaderResources[0] = m_resources.ShaderResource(m_whiteShaderTexture.id);
        }
    }

    return shaderOverride;
}

void QuarkD3D11Renderer::BeginShaderMode(const Shader &shader)
{
    if (shader.id == 0) {
        EndShaderMode();
        return;
    }

    if (m_shaderPrograms.find(shader.id) == m_shaderPrograms.end()) {
        TraceLog(LogLevel::Warn, "SHADER",
                 TextFormat("[D3D11] BeginShaderMode ignored unknown shader (ID: %u)", shader.id));
        return;
    }

    m_currentShaderId = shader.id;
    const D3D11CommandContext::ShaderOverride shaderOverride = BuildShaderOverride(shader.id);
    if (!shaderOverride.Active()) {
        m_currentShaderId = 0;
        m_commands.SetShaderOverride({});
        TraceLog(LogLevel::Warn, "SHADER",
                 TextFormat("[D3D11] BeginShaderMode ignored incomplete shader (ID: %u)",
                            shader.id));
        return;
    }

    m_commands.SetShaderOverride(shaderOverride);
}

void QuarkD3D11Renderer::EndShaderMode()
{
    if (m_currentShaderId == 0) {
        return;
    }

    m_currentShaderId = 0;
    m_commands.SetShaderOverride({});
}

Shader QuarkD3D11Renderer::LoadShader(const char *vsFileName, const char *fsFileName)
{
    TraceLog(LogLevel::Trace, "SHADER",
             TextFormat("[D3D11] Loading shader files: VS='%s', FS='%s'",
                        vsFileName ? vsFileName : "<default>",
                        fsFileName ? fsFileName : "<default>"));

    std::string vsSource;
    std::string fsSource;

    if (vsFileName && !ReadTextFile(vsFileName, vsSource)) {
        TraceLog(LogLevel::Error, "SHADER",
                 TextFormat("[D3D11] Failed to open vertex shader file: %s", vsFileName));
        return Shader{};
    }

    if (fsFileName && !ReadTextFile(fsFileName, fsSource)) {
        TraceLog(LogLevel::Error, "SHADER",
                 TextFormat("[D3D11] Failed to open fragment shader file: %s", fsFileName));
        return Shader{};
    }

    if (!vsFileName && !fsFileName) {
        return Shader{};
    }

    return LoadShaderFromMemory(vsFileName ? vsSource.c_str() : kDefaultHlslVertexSource,
                                fsFileName ? fsSource.c_str() : kDefaultHlslPixelSource);
}

Shader QuarkD3D11Renderer::LoadShaderFromMemory(const char *vsSource, const char *fsSource)
{
    if (!vsSource && !fsSource) {
        return Shader{};
    }

    const uint32_t shaderId = m_nextShaderId++;
    ShaderProgramData &program = m_shaderPrograms[shaderId];

    try {
        BuildShaderProgram(program,
                           vsSource ? vsSource : kDefaultHlslVertexSource,
                           fsSource ? fsSource : kDefaultHlslPixelSource);
    } catch (const std::exception &exception) {
        TraceLog(LogLevel::Error, "SHADER",
                 TextFormat("[D3D11] Shader creation failed (ID: %u): %s",
                            shaderId,
                            exception.what()));
        m_shaderPrograms.erase(shaderId);
        return Shader{};
    }

    TraceLog(LogLevel::Info, "SHADER",
             TextFormat("[D3D11] Shader created successfully (ID: %u, Attributes: %zu, "
                        "Uniforms: %zu)",
                        shaderId,
                        program.attributes.size(),
                        program.uniforms.size()));

    return Shader{shaderId};
}

void QuarkD3D11Renderer::UnloadShader(Shader &shader)
{
    if (shader.id == 0) {
        return;
    }

    if (m_shaderPrograms.erase(shader.id) > 0) {
        TraceLog(LogLevel::Info, "SHADER",
                 TextFormat("[D3D11] Shader unloaded (ID: %u)", shader.id));
    }

    if (m_currentShaderId == shader.id) {
        m_currentShaderId = 0;
        m_commands.SetShaderOverride({});
    }

    shader = Shader{};
}

bool QuarkD3D11Renderer::isShaderValid(Shader &shader)
{
    return shader.id != 0 && m_shaderPrograms.find(shader.id) != m_shaderPrograms.end();
}

int QuarkD3D11Renderer::GetShaderLocation(const Shader &shader, const char *uniformName)
{
    if (!uniformName || shader.id == 0) {
        return -1;
    }

    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return -1;
    }

    const auto locationIterator = program->uniforms.find(uniformName);
    if (locationIterator != program->uniforms.end()) {
        return locationIterator->second;
    }

    ShaderUniformInfo info{};
    info.name = uniformName;
    info.offset = AlignTo16(static_cast<UINT>(program->constantStaging.size()));
    info.size = sizeof(float) * 4;
    program->uniformInfos.push_back(std::move(info));

    const int location = static_cast<int>(program->uniformInfos.size()) - 1;
    program->uniforms.emplace(uniformName, location);

    TraceLog(LogLevel::Trace, "SHADER",
             TextFormat("[D3D11] Allocated virtual uniform '%s' (Location: %d)",
                        uniformName,
                        location));

    return location;
}

int QuarkD3D11Renderer::GetShaderLocation(const Shader &shader, ShaderLocationIndex locIndex)
{
    if (locIndex < SHADER_LOC_VERTEX_POSITION || locIndex >= SHADER_LOC_COUNT) {
        return -1;
    }

    return GetShaderLocation(shader, ShaderLocationNames[locIndex]);
}

int QuarkD3D11Renderer::GetShaderAttributeLocation(const Shader &shader, const char *attribName)
{
    if (!attribName || shader.id == 0) {
        return -1;
    }

    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return -1;
    }

    const auto attributeIterator = program->attributes.find(attribName);
    if (attributeIterator != program->attributes.end()) {
        return attributeIterator->second;
    }

    const int newLocation = static_cast<int>(program->attributes.size());
    program->attributes.emplace(attribName, newLocation);
    return newLocation;
}

void QuarkD3D11Renderer::SetShaderValue(const Shader &shader, int locIndex, float value)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_FLOAT, &value, sizeof(value), 1);
    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValue(const Shader &shader, int locIndex, int value)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_INT, &value, sizeof(value), 1);
    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValue(const Shader &shader, int locIndex, const Color &value)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    const float rgba[4] = {value.r / 255.0f, value.g / 255.0f, value.b / 255.0f,
                           value.a / 255.0f};
    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_VEC4, rgba, sizeof(float), 4);
    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValue(const Shader &shader, int locIndex, const Vec2 &value)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    const float vec[2] = {value.x, value.y};
    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_VEC2, vec, sizeof(float), 2);
    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValue(const Shader &shader, int locIndex, const Vec3 &value)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    const float vec[3] = {value.x, value.y, value.z};
    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_VEC3, vec, sizeof(float), 3);
    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValue(const Shader &shader, int locIndex, const Vec4 &value)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    const float vec[4] = {value.x, value.y, value.z, value.w};
    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_VEC4, vec, sizeof(float), 4);
    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValueMatrix(const Shader &shader, int locIndex,
                                               const float *mat)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program || mat == nullptr) {
        return;
    }

    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_FLOAT, mat, sizeof(float), 16);
    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValueMatrix(const Shader &shader, int locIndex,
                                              const Matrix &mat)
{
    SetShaderValueMatrix(shader, locIndex, mat.m);
}

void QuarkD3D11Renderer::SetShaderValueSampler(const Shader &shader, int locIndex,
                                               int textureUnit)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureUnit,
                      sizeof(int), 1);
    RegisterShaderTexture(*program, locIndex, static_cast<uint32_t>(textureUnit));
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
    UploadConstantBuffer(*program);
}

void QuarkD3D11Renderer::SetShaderValue(const Shader &shader, int locIndex, const void *value,
                                        int uniformType)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program || value == nullptr) {
        return;
    }

    switch (uniformType) {
        case SHADER_UNIFORM_FLOAT:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(float), 1);
            break;
        case SHADER_UNIFORM_VEC2:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(float), 2);
            break;
        case SHADER_UNIFORM_VEC3:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(float), 3);
            break;
        case SHADER_UNIFORM_VEC4:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(float), 4);
            break;
        case SHADER_UNIFORM_INT:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int), 1);
            break;
        case SHADER_UNIFORM_SAMPLER2D: {
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int), 1);
            int textureId = 0;
            std::memcpy(&textureId, value, sizeof(textureId));
            RegisterShaderTexture(*program, locIndex, static_cast<uint32_t>(textureId));
            break;
        }
        case SHADER_UNIFORM_IVEC2:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int), 2);
            break;
        case SHADER_UNIFORM_IVEC3:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int), 3);
            break;
        case SHADER_UNIFORM_IVEC4:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int), 4);
            break;
        default:
            break;
    }

    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValueV(const Shader &shader, int locIndex, const void *value,
                                         int uniformType, int count)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program || value == nullptr || count <= 0) {
        return;
    }

    switch (uniformType) {
        case SHADER_UNIFORM_FLOAT:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(float), count);
            break;
        case SHADER_UNIFORM_VEC2:
            StoreUniformValue(*program, locIndex, uniformType, value,
                              sizeof(float) * 2, count);
            break;
        case SHADER_UNIFORM_VEC3:
            StoreUniformValue(*program, locIndex, uniformType, value,
                              sizeof(float) * 3, count);
            break;
        case SHADER_UNIFORM_VEC4:
            StoreUniformValue(*program, locIndex, uniformType, value,
                              sizeof(float) * 4, count);
            break;
        case SHADER_UNIFORM_INT:
        case SHADER_UNIFORM_SAMPLER2D:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int), count);
            break;
        case SHADER_UNIFORM_IVEC2:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int) * 2, count);
            break;
        case SHADER_UNIFORM_IVEC3:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int) * 3, count);
            break;
        case SHADER_UNIFORM_IVEC4:
            StoreUniformValue(*program, locIndex, uniformType, value, sizeof(int) * 4, count);
            break;
        default:
            break;
    }

    UploadConstantBuffer(*program);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValueTexture(const Shader &shader, int locIndex,
                                               const ITexture &texture)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    const int textureUnit = static_cast<int>(texture.id);
    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureUnit,
                      sizeof(int), 1);
    RegisterShaderTexture(*program, locIndex, texture.id);
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

void QuarkD3D11Renderer::SetShaderValueTextureUnit(const Shader &shader, int locIndex,
                                                   const ITexture &texture, int textureUnit)
{
    ShaderProgramData *program = GetShaderProgram(shader.id);
    if (!program) {
        return;
    }

    const int textureId = texture.valid ? static_cast<int>(texture.id) : textureUnit;
    StoreUniformValue(*program, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureId,
                      sizeof(int), 1);
    RegisterShaderTexture(*program, locIndex, static_cast<uint32_t>(textureId));
    if (m_currentShaderId == shader.id) {
        m_commands.SetShaderOverride(BuildShaderOverride(shader.id));
    }
}

} // namespace qc
#endif
