#include "QuarkCore/QuarkCore.hpp"
#include <cmath>

constexpr const char* TINT_FRAGMENT_SHADER = R"(
#version 330 core
in vec2 vTexCoord;
in vec4 vColor;
out vec4 fragColor;
uniform sampler2D uTexture;
uniform float uTime;
void main() {
    vec4 tex = texture(uTexture, vTexCoord);
    float pulse = 0.5 + 0.5 * sin(uTime * 3.0);
    vec3 tint = vec3(0.6, 0.85, 1.0) * pulse;
    fragColor = vec4(tex.rgb * tint, tex.a) * vColor;
}
)";

int main() {
    qc::InitWindow(1280, 720, "QuarkCore Textures and Shaders Example");
    qc::SetTargetFPS(60);
    qc::StartTextInput();

    qc::Texture2D checker = qc::GenCheckerTexture(
        256, 256, 32,
        qc::Color{215, 225, 235, 255},
        qc::Color{70, 100, 180, 255}
    );

    qc::RenderTexture2D renderTarget = qc::LoadRenderTexture(320, 240);
    qc::Shader tintShader = qc::LoadShaderFromMemory(qc::kVertexShaderSource, TINT_FRAGMENT_SHADER);

    int locTime = qc::GetShaderLocation(tintShader, "uTime");

    bool useShader = true;
    float elapsed = 0.0f;
    qc::Camera2D camera = qc::CreateCamera2D();
    camera.target = { 160.0f, 120.0f };
    camera.offset = { 640.0f, 360.0f };
    camera.zoom = 1.0f;

    qc::Event event{};
    qc::Font defaultFont = qc::GetDefaultFont();

    while (!qc::WindowShouldClose()) {
        while (qc::PollEvent(event)) {
            switch (event.type) {
                case qc::EventType::KeyDown:
                    if (event.key == SDLK_SPACE) {
                        useShader = !useShader;
                    }
                    break;
                case qc::EventType::MouseWheel:
                    if (event.dy > 0.0f) camera.zoom += 0.1f;
                    if (event.dy < 0.0f) camera.zoom = std::max(0.2f, camera.zoom - 0.1f);
                    break;
                default:
                    break;
            }
        }

        if (qc::IsKeyDown(qc::KeyboardKey::Left)) {
            camera.target.x -= 400.0f * qc::GetDeltaTime();
        }
        if (qc::IsKeyDown(qc::KeyboardKey::Right)) {
            camera.target.x += 400.0f * qc::GetDeltaTime();
        }
        if (qc::IsKeyDown(qc::KeyboardKey::Up)) {
            camera.target.y -= 400.0f * qc::GetDeltaTime();
        }
        if (qc::IsKeyDown(qc::KeyboardKey::Down)) {
            camera.target.y += 400.0f * qc::GetDeltaTime();
        }

        elapsed += qc::GetDeltaTime();

        qc::BeginTextureMode(renderTarget);
            qc::ClearBackground(qc::Color{10, 10, 20, 255});
            qc::DrawRectangle(16, 16, 80, 80, qc::Color{200, 100, 60, 255});
            qc::DrawCircle(220, 120, 50, qc::Color{90, 180, 140, 255});
            qc::DrawText("Render target content", 16, 120, 18, qc::WHITE);
        qc::EndTextureMode();

        qc::BeginDrawing();
            qc::ClearBackground(qc::Color{24, 28, 48, 255});

            qc::BeginMode2D(camera);
                qc::DrawTexture(checker, 0, 0, qc::WHITE);
                qc::DrawTexturePro(
                    renderTarget.texture,
                    qc::Rectangle{ 0, (float)renderTarget.texture.height, (float)renderTarget.texture.width, -(float)renderTarget.texture.height },
                    qc::Rectangle{ 300, 80, 320, 240 },
                    qc::Vec2{ 0, 0 },
                    0.0f,
                    qc::Color{255, 255, 255, 220}
                );
                qc::Vec2 mousePos = qc::GetMousePosition();
                qc::DrawCircle(mousePos.x, mousePos.y, 16.0f, qc::Color{255, 180, 60, 200});
            qc::EndMode2D();

            if (useShader) {
                qc::BeginShaderMode(tintShader);
                qc::SetShaderValue(tintShader, locTime, elapsed);
            }

            qc::DrawRectangle(32, 520, 360, 140, qc::Color{10, 10, 30, 230});
            qc::DrawText("Textures and Shaders with QuarkCore", 42, 540, 24, qc::Color{240, 240, 240, 255});
            qc::DrawText(qc::TextFormat("Press SPACE to toggle shader: %s", useShader ? "ON" : "OFF"), 42, 576, 18, qc::Color{200, 220, 255, 255});
            qc::DrawText(qc::TextFormat("Mouse: %.0f, %.0f", qc::GetMousePosition().x, qc::GetMousePosition().y), 42, 604, 18, qc::Color{200, 220, 255, 255});
            qc::DrawText(qc::TextFormat("Zoom: %.2f", camera.zoom), 42, 632, 18, qc::Color{200, 220, 255, 255});

            if (useShader) {
                qc::EndShaderMode();
            }

            qc::DrawTextEx(defaultFont, "Event example: PollEvent + MouseWheel + Keyboard", qc::Vec2{520.0f, 560.0f}, 20.0f, 2.0f, qc::LIGHTGRAY);
            qc::DrawText(qc::TextFormat("FPS: %d", qc::GetFPS()), 520, 610, 20, qc::YELLOW);
        qc::EndDrawing();
    }

    qc::StopTextInput();
    qc::UnloadTexture(checker);
    qc::UnloadRenderTexture(renderTarget);
    qc::UnloadShader(tintShader);
    qc::CloseWindow();
    return 0;
}
