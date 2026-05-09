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
    qc::InitWindow(1280, 720, "Shader Effects Demo");
    qc::SetTargetFPS(60);

    qc::Texture2D checker = qc::GenCheckerTexture(
        512, 512, 32,
        qc::Color{220, 80, 80, 255},
        qc::Color{80, 80, 220, 255}
    );

    qc::Shader shaderChromatic = qc::LoadShaderFromMemory(qc::kVertexShaderSource, CHROMATIC_FRAG);
    qc::Shader shaderPixelate  = qc::LoadShaderFromMemory(qc::kVertexShaderSource, PIXELATE_FRAG);
    qc::Shader shaderVignette  = qc::LoadShaderFromMemory(qc::kVertexShaderSource, VIGNETTE_FRAG);
    qc::Shader shaderScanlines = qc::LoadShaderFromMemory(qc::kVertexShaderSource, SCANLINES_FRAG);

    int locStrength   = qc::GetShaderLocation(shaderChromatic, "uStrength");
    int locPixelSize  = qc::GetShaderLocation(shaderPixelate,  "uPixelSize");
    int locPixelScreen= qc::GetShaderLocation(shaderPixelate,  "uScreenSize");
    int locVigRadius  = qc::GetShaderLocation(shaderVignette,  "uRadius");
    int locVigSoft    = qc::GetShaderLocation(shaderVignette,  "uSoftness");
    int locScanScreen = qc::GetShaderLocation(shaderScanlines, "uScreenSize");
    int locScanIntens = qc::GetShaderLocation(shaderScanlines, "uIntensity");

    float chromaStrength = 0.005f;
    float pixelSize      = 8.0f;
    float vigRadius      = 0.6f;
    float scanIntensity  = 0.5f;

    int activeShader = 0; // 0=none 1=chromatic 2=pixelate 3=vignette 4=scanlines

    const char* names[] = { "none", "chromatic aberration", "pixelate", "vignette", "scanlines" };

    while (!qc::WindowShouldClose()) {
        qc::Event ev;
        while (qc::PollEvent(ev)) {
            if (ev.type == qc::EventType::KeyDown) {
                switch (ev.key) {
                    case 49: activeShader = 0; break;  // '1' - no shader
                    case 50: activeShader = 1; break;  // '2' - chromatic
                    case 51: activeShader = 2; break;  // '3' - pixelate
                    case 52: activeShader = 3; break;  // '4' - vignette
                    case 53: activeShader = 4; break;  // '5' - scanlines
                }
            }
        }

        if (qc::IsKeyDown(qc::KeyboardKey::Up)) {
            if (activeShader == 1) chromaStrength += 0.001f;
            if (activeShader == 2) pixelSize      += 0.5f;
            if (activeShader == 3) vigRadius      += 0.01f;
            if (activeShader == 4) scanIntensity  += 0.02f;
        }
        if (qc::IsKeyDown(qc::KeyboardKey::Down)) {
            if (activeShader == 1) chromaStrength -= 0.001f;
            if (activeShader == 2) pixelSize      -= 0.5f;
            if (activeShader == 3) vigRadius      -= 0.01f;
            if (activeShader == 4) scanIntensity  -= 0.02f;
        }

        chromaStrength = std::max(0.0f, std::min(0.05f,  chromaStrength));
        pixelSize      = std::max(1.0f, std::min(64.0f,  pixelSize));
        vigRadius      = std::max(0.1f, std::min(1.0f,   vigRadius));
        scanIntensity  = std::max(0.0f, std::min(1.0f,   scanIntensity));

        float sw = (float)qc::GetScreenWidth();
        float sh = (float)qc::GetScreenHeight();

        switch (activeShader) {
            case 1:
                qc::BeginShaderMode(shaderChromatic);
                qc::SetShaderValue(shaderChromatic, locStrength, chromaStrength);
                break;
            case 2:
                qc::BeginShaderMode(shaderPixelate);
                qc::SetShaderValue(shaderPixelate, locPixelScreen, qc::Vec2{sw, sh});
                qc::SetShaderValue(shaderPixelate, locPixelSize,   pixelSize);
                break;
            case 3:
                qc::BeginShaderMode(shaderVignette);
                qc::SetShaderValue(shaderVignette, locVigRadius, vigRadius);
                qc::SetShaderValue(shaderVignette, locVigSoft,   0.3f);
                break;
            case 4:
                qc::BeginShaderMode(shaderScanlines);
                qc::SetShaderValue(shaderScanlines, locScanScreen, qc::Vec2{sw, sh});
                qc::SetShaderValue(shaderScanlines, locScanIntens, scanIntensity);
                break;
            default:
                break;
        }

        qc::BeginDrawing();
        qc::ClearBackground(qc::Color{20, 20, 30, 255});

        qc::DrawTexture(checker, 384, 104, qc::WHITE);
        qc::DrawRectangle(100, 150, 250, 400, qc::Color{200, 160, 60, 255});
        qc::DrawCircle(950, 360, 150, qc::Color{60, 180, 160, 255});

        if (activeShader != 0)
            qc::EndShaderMode();

        qc::DrawRectangle(10, 10, 420, 130, qc::Color{0, 0, 0, 180});

        qc::TraceLog(qc::LogLevel::Info, "HUD",
            qc::TextFormat("Shader : %s", names[activeShader]));
        qc::TraceLog(qc::LogLevel::Info, "HUD",
            qc::TextFormat("Keys   : 1=none  2=chroma  3=pixel  4=vignette  5=scan"));
        qc::TraceLog(qc::LogLevel::Info, "HUD",
            qc::TextFormat("Up/Down: adjust parameter"));

        float param = 0.0f;
        const char* paramName = "";
        if (activeShader == 1) { param = chromaStrength; paramName = "strength"; }
        if (activeShader == 2) { param = pixelSize;      paramName = "pixel size"; }
        if (activeShader == 3) { param = vigRadius;      paramName = "radius"; }
        if (activeShader == 4) { param = scanIntensity;  paramName = "intensity"; }

        if (activeShader != 0)
            qc::TraceLog(qc::LogLevel::Info, "HUD",
                qc::TextFormat("%-12s: %.3f", paramName, param));

        qc::EndDrawing();
    }

    qc::UnloadTexture(checker);
    qc::UnloadShader(shaderChromatic);
    qc::UnloadShader(shaderPixelate);
    qc::UnloadShader(shaderVignette);
    qc::UnloadShader(shaderScanlines);
    qc::CloseWindow();
    return 0;
}