#include "QuarkCore/QuarkCore.hpp"
#include <algorithm>
#include <cmath>

using namespace qc;

constexpr const char* D3D11_VS = R"(
struct VSInput {
    float2 position : POSITION;
    float2 texCoord : TEXCOORD0;
    float4 color    : COLOR;
};
struct VSOutput {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    float4 color    : COLOR;
};
VSOutput main(VSInput input) {
    VSOutput o;
    o.position = float4(input.position, 0.0, 1.0);
    o.texCoord = input.texCoord;
    o.color    = input.color;
    return o;
}
)";

constexpr const char* CHROMATIC_PS = R"(
cbuffer Params : register(b0) {
    float uStrength;
};

Texture2D textureMap : register(t0);
SamplerState textureSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    float4 color    : COLOR;
};

float4 main(PSInput input) : SV_TARGET {
    float s = saturate(abs(uStrength)) * 0.05;
    float2 offset = float2(s, 0.0);
    float2 uv = saturate(input.texCoord);
    float r = textureMap.Sample(textureSampler, saturate(uv + offset)).r;
    float g = textureMap.Sample(textureSampler, uv).g;
    float b = textureMap.Sample(textureSampler, saturate(uv - offset)).b;
    float a = textureMap.Sample(textureSampler, uv).a;
    return float4(r, g, b, a) * input.color;
}
)";

constexpr const char* PIXELATE_PS = R"(
cbuffer Params : register(b0) {
    float2 uScreenSize;
    float uPixelSize;
};

Texture2D textureMap : register(t0);
SamplerState textureSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    float4 color    : COLOR;
};

float4 main(PSInput input) : SV_TARGET {
    float ps = max(uPixelSize, 1.0);
    float2 safeScreen = max(uScreenSize, float2(1.0, 1.0));
    float2 pixelUV = floor(saturate(input.texCoord) * safeScreen / ps) * ps / safeScreen;
    return textureMap.Sample(textureSampler, saturate(pixelUV)) * input.color;
}
)";

constexpr const char* VIGNETTE_PS = R"(
cbuffer Params : register(b0) {
    float uRadius;
    float uSoftness;
};

Texture2D textureMap : register(t0);
SamplerState textureSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    float4 color    : COLOR;
};

float4 main(PSInput input) : SV_TARGET {
    float4 tex = textureMap.Sample(textureSampler, saturate(input.texCoord)) * input.color;
    float2 uv = input.texCoord - 0.5;
    float dist = length(uv);
    float radius = saturate(uRadius);
    float soft = saturate(uSoftness) * 0.5;
    float inner = max(radius - soft, 0.0);
    float vignette = 1.0 - smoothstep(inner, radius, dist);
    return float4(tex.rgb * vignette, tex.a);
}
)";

constexpr const char* SCANLINES_PS = R"(
cbuffer Params : register(b0) {
    float2 uScreenSize;
    float uIntensity;
};

Texture2D textureMap : register(t0);
SamplerState textureSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    float4 color    : COLOR;
};

float4 main(PSInput input) : SV_TARGET {
    float4 tex = textureMap.Sample(textureSampler, saturate(input.texCoord)) * input.color;
    float screenH = max(uScreenSize.y, 1.0);
    float pixelY = floor(saturate(input.texCoord.y) * screenH + 0.0001);
    float scanLine = fmod(pixelY, 2.0);
    float dark = 1.0 - saturate(scanLine) * saturate(uIntensity);
    return float4(tex.rgb * dark, tex.a);
}
)";

constexpr const char* TINT_PS = R"(
cbuffer Params : register(b0) {
    float uTime;
};

Texture2D textureMap : register(t0);
SamplerState textureSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    float4 color    : COLOR;
};

float4 main(PSInput input) : SV_TARGET {
    float4 tex = textureMap.Sample(textureSampler, saturate(input.texCoord));
    float pulse = 0.5 + 0.5 * sin(uTime * 3.0);
    float3 tint = float3(0.6, 0.85, 1.0) * pulse;
    return float4(tex.rgb * tint, tex.a) * input.color;
}
)";

int main() {
    InitWindow(1280, 720, "QuarkCore D3D11 Shaders Example", RendererType::D3D11);
    SetTargetFPS(60);

    Texture2D checker = GenCheckerTexture(
        512, 512, 32,
        Color{220, 80, 80, 255},
        Color{80, 80, 220, 255}
    );

    Shader shaderChromatic = LoadShaderFromMemory(D3D11_VS, CHROMATIC_PS);
    Shader shaderPixelate  = LoadShaderFromMemory(D3D11_VS, PIXELATE_PS);
    Shader shaderVignette  = LoadShaderFromMemory(D3D11_VS, VIGNETTE_PS);
    Shader shaderScanlines = LoadShaderFromMemory(D3D11_VS, SCANLINES_PS);
    Shader shaderTint      = LoadShaderFromMemory(D3D11_VS, TINT_PS);

    if (!IsShaderValid(shaderChromatic)) TraceLog(LogLevel::Warn, "SHADER", "chromatic invalid");
    if (!IsShaderValid(shaderPixelate))  TraceLog(LogLevel::Warn, "SHADER", "pixelate invalid");
    if (!IsShaderValid(shaderScanlines)) TraceLog(LogLevel::Warn, "SHADER", "scanlines invalid");

    int locStrength    = GetShaderLocation(shaderChromatic, "uStrength");
    int locPixelSize   = GetShaderLocation(shaderPixelate,  "uPixelSize");
    int locPixelScreen = GetShaderLocation(shaderPixelate,  "uScreenSize");
    int locVigRadius   = GetShaderLocation(shaderVignette,  "uRadius");
    int locVigSoft     = GetShaderLocation(shaderVignette,  "uSoftness");
    int locScanScreen  = GetShaderLocation(shaderScanlines, "uScreenSize");
    int locScanIntens  = GetShaderLocation(shaderScanlines, "uIntensity");
    int locTime        = GetShaderLocation(shaderTint,      "uTime");

    int locMapAlbedo = GetShaderLocation(shaderTint, SHADER_LOC_MAP_ALBEDO);
    (void)locMapAlbedo;

    int attrPos = GetShaderAttributeLocation(shaderChromatic, "POSITION");
    (void)attrPos;

    float chromaStrength = 0.005f;
    float pixelSize      = 8.0f;
    float vigRadius      = 0.6f;
    float scanIntensity  = 0.3f;
    float elapsed        = 0.0f;

    int activeShader = 0;
    const char* names[] = { "none", "chromatic aberration", "pixelate", "vignette", "scanlines", "tint (time)" };

    while (!WindowShouldClose()) {
        Event ev;
        while (PollEvent(ev)) { }

        if (IsKeyDown(KeyboardKey::Num1)) activeShader = 0;
        else if (IsKeyDown(KeyboardKey::Num2)) activeShader = 1;
        else if (IsKeyDown(KeyboardKey::Num3)) activeShader = 2;
        else if (IsKeyDown(KeyboardKey::Num4)) activeShader = 3;
        else if (IsKeyDown(KeyboardKey::Num5)) activeShader = 4;
        else if (IsKeyDown(KeyboardKey::Num6)) activeShader = 5;

        if (IsKeyDown(KeyboardKey::Up)) {
            if (activeShader == 1) chromaStrength += 0.001f;
            if (activeShader == 2) pixelSize      += 0.5f;
            if (activeShader == 3) vigRadius      += 0.01f;
            if (activeShader == 4) scanIntensity  += 0.02f;
        }
        if (IsKeyDown(KeyboardKey::Down)) {
            if (activeShader == 1) chromaStrength -= 0.001f;
            if (activeShader == 2) pixelSize      -= 0.5f;
            if (activeShader == 3) vigRadius      -= 0.01f;
            if (activeShader == 4) scanIntensity  -= 0.02f;
        }

        chromaStrength = std::clamp(chromaStrength, 0.0f, 0.05f);
        pixelSize      = std::clamp(pixelSize, 1.0f, 64.0f);
        vigRadius      = std::clamp(vigRadius, 0.1f, 1.0f);
        scanIntensity  = std::clamp(scanIntensity, 0.0f, 1.0f);

        elapsed += GetDeltaTime();
        float sw = static_cast<float>(GetScreenWidth());
        float sh = static_cast<float>(GetScreenHeight());

        BeginDrawing();
        ClearBackground(Color{20, 20, 30, 255});

        bool hasShader = false;
        switch (activeShader) {
            case 1:
                if (IsShaderValid(shaderChromatic)) {
                    BeginShaderMode(shaderChromatic);
                    SetShaderValue(shaderChromatic, locStrength, chromaStrength);
                    hasShader = true;
                }
                break;
            case 2:
                if (IsShaderValid(shaderPixelate)) {
                    BeginShaderMode(shaderPixelate);
                    SetShaderValue(shaderPixelate, locPixelScreen, Vec2{sw, sh});
                    SetShaderValue(shaderPixelate, locPixelSize,   pixelSize);
                    hasShader = true;
                }
                break;
            case 3:
                if (IsShaderValid(shaderVignette)) {
                    BeginShaderMode(shaderVignette);
                    SetShaderValue(shaderVignette, locVigRadius, vigRadius);
                    SetShaderValue(shaderVignette, locVigSoft,   0.3f);
                    hasShader = true;
                }
                break;
            case 4:
                if (IsShaderValid(shaderScanlines)) {
                    BeginShaderMode(shaderScanlines);
                    SetShaderValue(shaderScanlines, locScanScreen, Vec2{sw, sh});
                    SetShaderValue(shaderScanlines, locScanIntens, scanIntensity);
                    hasShader = true;
                }
                break;
            case 5:
                if (IsShaderValid(shaderTint)) {
                    BeginShaderMode(shaderTint);
                    SetShaderValue(shaderTint, locTime, elapsed);
                    hasShader = true;
                }
        }

        DrawTexture(checker, 384, 104, WHITE);
        DrawRectangle(100, 150, 250, 400, Color{200, 160, 60, 255});
        DrawCircle(950, 360, 150, Color{60, 180, 160, 255});

        if (hasShader) EndShaderMode();

        DrawRectangle(10, 10, 520, 170, Color{0, 0, 0, 180});
        DrawText(TextFormat("Shader : %s", names[activeShader]), 20, 20, 20, WHITE);
        DrawText("Keys   : 1=none  2=chroma  3=pixel  4=vignette  5=scan  6=tint", 20, 45, 18, LIGHTGRAY);
        DrawText("Up/Down: adjust parameter   FPS:", 20, 65, 18, LIGHTGRAY);
        DrawText(TextFormat("%d", GetFPS()), 310, 65, 18, YELLOW);

        float param = 0.0f;
        const char* paramName = "";
        if (activeShader == 1) { param = chromaStrength; paramName = "strength"; }
        if (activeShader == 2) { param = pixelSize;      paramName = "pixel size"; }
        if (activeShader == 3) { param = vigRadius;      paramName = "radius"; }
        if (activeShader == 4) { param = scanIntensity;  paramName = "intensity"; }
        if (activeShader == 5) { param = elapsed;        paramName = "uTime"; }
        if (activeShader != 0) {
            DrawText(TextFormat("%s: %.3f", paramName, param), 20, 90, 18, SKYBLUE);
            DrawText(TextFormat("loc: %d", 
                activeShader==1?locStrength: activeShader==2?locPixelSize: activeShader==3?locVigRadius: activeShader==4?locScanIntens: locTime),
                20, 115, 18, GRAY);
        } else {
            DrawText("No shader — solid primitives visible", 20, 90, 18, GRAY);
        }

        EndDrawing();
    }

    UnloadTexture(checker);
    UnloadShader(shaderChromatic);
    UnloadShader(shaderPixelate);
    UnloadShader(shaderVignette);
    UnloadShader(shaderScanlines);
    UnloadShader(shaderTint);
    CloseWindow();
    return 0;
}
