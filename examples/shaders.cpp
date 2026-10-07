#include "QuarkCore/QuarkCore.hpp"
#include <cmath>

// Chromatic aberration
constexpr const char* CHROMATIC_FRAG = R"(
#version 330 core
in vec2 vTexCoord;
in vec4 vColor;
out vec4 fragColor;

uniform sampler2D uTexture;
uniform float uStrength;

void main() {
    vec2 offset = vec2(uStrength, 0.0);
    float r = texture(uTexture, vTexCoord + offset).r;
    float g = texture(uTexture, vTexCoord).g;
    float b = texture(uTexture, vTexCoord - offset).b;
    float a = texture(uTexture, vTexCoord).a;
    fragColor = vec4(r, g, b, a) * vColor;
}
)";

// Pixelate
constexpr const char* PIXELATE_FRAG = R"(
#version 330 core
in vec2 vTexCoord;
in vec4 vColor;
out vec4 fragColor;

uniform sampler2D uTexture;
uniform vec2 uScreenSize;
uniform float uPixelSize;

void main() {
    vec2 pixelUV = floor(vTexCoord * uScreenSize / uPixelSize)
                   * uPixelSize / uScreenSize;
    fragColor = texture(uTexture, pixelUV) * vColor;
}
)";

// Vignette
constexpr const char* VIGNETTE_FRAG = R"(
#version 330 core
in vec2 vTexCoord;
in vec4 vColor;
out vec4 fragColor;

uniform sampler2D uTexture;
uniform float uRadius;
uniform float uSoftness;

void main() {
    vec4 tex = texture(uTexture, vTexCoord) * vColor;
    vec2 uv = vTexCoord - 0.5;
    float dist = length(uv);
    float vignette = smoothstep(uRadius, uRadius - uSoftness, dist);
    fragColor = vec4(tex.rgb * vignette, tex.a);
}
)";

// Scanlines
constexpr const char* SCANLINES_FRAG = R"(
#version 330 core
in vec2 vTexCoord;
in vec4 vColor;
out vec4 fragColor;

uniform sampler2D uTexture;
uniform vec2 uScreenSize;
uniform float uIntensity;

void main() {
    vec4 tex = texture(uTexture, vTexCoord) * vColor;
    float line = mod(floor(vTexCoord.y * uScreenSize.y), 2.0);
    float dark = 1.0 - line * uIntensity;
    fragColor = vec4(tex.rgb * dark, tex.a);
}
)";

int main() {
    InitWindow(1280, 720, "Shader Effects Demo", RendererType::OpenGL);
    SetTargetFPS(60);

    Texture2D checker = GenCheckerTexture(
        512, 512, 32,
        Color{220, 80, 80, 255},
        Color{80, 80, 220, 255}
    );

    Shader shaderChromatic = LoadShaderFromMemory(kVertexShaderSource, CHROMATIC_FRAG);
    Shader shaderPixelate  = LoadShaderFromMemory(kVertexShaderSource, PIXELATE_FRAG);
    Shader shaderVignette  = LoadShaderFromMemory(kVertexShaderSource, VIGNETTE_FRAG);
    Shader shaderScanlines = LoadShaderFromMemory(kVertexShaderSource, SCANLINES_FRAG);

    int locStrength   = GetShaderLocation(shaderChromatic, "uStrength");
    int locPixelSize  = GetShaderLocation(shaderPixelate,  "uPixelSize");
    int locPixelScreen= GetShaderLocation(shaderPixelate,  "uScreenSize");
    int locVigRadius  = GetShaderLocation(shaderVignette,  "uRadius");
    int locVigSoft    = GetShaderLocation(shaderVignette,  "uSoftness");
    int locScanScreen = GetShaderLocation(shaderScanlines, "uScreenSize");
    int locScanIntens = GetShaderLocation(shaderScanlines, "uIntensity");

    float chromaStrength = 0.005f;
    float pixelSize      = 8.0f;
    float vigRadius      = 0.6f;
    float scanIntensity  = 0.5f;

    int activeShader = 0; // 0=none 1=chromatic 2=pixelate 3=vignette 4=scanlines

    const char* names[] = { "none", "chromatic aberration", "pixelate", "vignette", "scanlines" };

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_ONE)) activeShader = 0;
        else if (IsKeyPressed(KEY_TWO)) activeShader = 1;
        else if (IsKeyPressed(KEY_THREE)) activeShader = 2;
        else if (IsKeyPressed(KEY_FOUR)) activeShader = 3;
        else if (IsKeyPressed(KEY_FIVE)) activeShader = 4;

        if (IsKeyDown(KEY_UP)) {
            if (activeShader == 1) chromaStrength += 0.001f;
            if (activeShader == 2) pixelSize      += 0.5f;
            if (activeShader == 3) vigRadius      += 0.01f;
            if (activeShader == 4) scanIntensity  += 0.02f;
        }
        if (IsKeyDown(KEY_DOWN)) {
            if (activeShader == 1) chromaStrength -= 0.001f;
            if (activeShader == 2) pixelSize      -= 0.5f;
            if (activeShader == 3) vigRadius      -= 0.01f;
            if (activeShader == 4) scanIntensity  -= 0.02f;
        }

        chromaStrength = std::max(0.0f, std::min(0.05f,  chromaStrength));
        pixelSize      = std::max(1.0f, std::min(64.0f,  pixelSize));
        vigRadius      = std::max(0.1f, std::min(1.0f,   vigRadius));
        scanIntensity  = std::max(0.0f, std::min(1.0f,   scanIntensity));

        float sw = (float)GetScreenWidth();
        float sh = (float)GetScreenHeight();

        switch (activeShader) {
            case 1:
                BeginShaderMode(shaderChromatic);
                SetShaderValue(shaderChromatic, locStrength, chromaStrength);
                break;
            case 2:
                BeginShaderMode(shaderPixelate);
                SetShaderValue(shaderPixelate, locPixelScreen, Vec2{sw, sh});
                SetShaderValue(shaderPixelate, locPixelSize,   pixelSize);
                break;
            case 3:
                BeginShaderMode(shaderVignette);
                SetShaderValue(shaderVignette, locVigRadius, vigRadius);
                SetShaderValue(shaderVignette, locVigSoft,   0.3f);
                break;
            case 4:
                BeginShaderMode(shaderScanlines);
                SetShaderValue(shaderScanlines, locScanScreen, Vec2{sw, sh});
                SetShaderValue(shaderScanlines, locScanIntens, scanIntensity);
                break;
            default:
                break;
        }

        BeginDrawing();
        ClearBackground(Color{20, 20, 30, 255});

        DrawTexture(checker, 384, 104, WHITE);
        DrawRectangle(100, 150, 250, 400, Color{200, 160, 60, 255});
        DrawCircle(950, 360, 150, Color{60, 180, 160, 255});

        if (activeShader != 0)
            EndShaderMode();

        DrawRectangle(10, 10, 420, 130, Color{0, 0, 0, 180});

        TraceLog(LogLevel::Info, "HUD",
            TextFormat("Shader : %s", names[activeShader]));
        TraceLog(LogLevel::Info, "HUD",
            TextFormat("Keys   : 1=none  2=chroma  3=pixel  4=vignette  5=scan"));
        TraceLog(LogLevel::Info, "HUD",
            TextFormat("Up/Down: adjust parameter"));

        float param = 0.0f;
        const char* paramName = "";
        if (activeShader == 1) { param = chromaStrength; paramName = "strength"; }
        if (activeShader == 2) { param = pixelSize;      paramName = "pixel size"; }
        if (activeShader == 3) { param = vigRadius;      paramName = "radius"; }
        if (activeShader == 4) { param = scanIntensity;  paramName = "intensity"; }

        if (activeShader != 0)
            TraceLog(LogLevel::Info, "HUD",
                TextFormat("%-12s: %.3f", paramName, param));

        EndDrawing();
    }

    UnloadTexture(checker);
    UnloadShader(shaderChromatic);
    UnloadShader(shaderPixelate);
    UnloadShader(shaderVignette);
    UnloadShader(shaderScanlines);
    CloseWindow();
    return 0;
}