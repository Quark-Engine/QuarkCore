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
    InitWindow(1280, 720, "QuarkCore Textures and Shaders Example", RendererType::OpenGL);
    SetTargetFPS(60);
    StartTextInput();

    Texture2D checker = GenCheckerTexture(
        256, 256, 32,
        Color{215, 225, 235, 255},
        Color{70, 100, 180, 255}
    );

    RenderTexture2D renderTarget = LoadRenderTexture(320, 240);
    Shader tintShader = LoadShaderFromMemory(kVertexShaderSource, TINT_FRAGMENT_SHADER);

    int locTime = GetShaderLocation(tintShader, "uTime");

    bool useShader = true;
    float elapsed = 0.0f;
    Camera2D camera = CreateCamera2D();
    camera.target = { 160.0f, 120.0f };
    camera.offset = { 640.0f, 360.0f };
    camera.zoom = 1.0f;

    Font defaultFont = GetDefaultFont();

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_SPACE)) {
            useShader = !useShader;
        }
        const float wheelMove = GetMouseWheelMove();
        if (wheelMove > 0.0f) camera.zoom += 0.1f;
        if (wheelMove < 0.0f) camera.zoom = std::max(0.2f, camera.zoom - 0.1f);

        if (IsKeyDown(KEY_LEFT)) {
            camera.target.x += 400.0f * GetDeltaTime();
        }
        if (IsKeyDown(KEY_RIGHT)) {
            camera.target.x -= 400.0f * GetDeltaTime();
        }
        if (IsKeyDown(KEY_UP)) {
            camera.target.y += 400.0f * GetDeltaTime();
        }
        if (IsKeyDown(KEY_DOWN)) {
            camera.target.y -= 400.0f * GetDeltaTime();
        }

        elapsed += GetDeltaTime();

        BeginTextureMode(renderTarget);
            ClearBackground(Color{10, 10, 20, 255});
            DrawRectangle(16, 16, 80, 80, Color{200, 100, 60, 255});
            DrawCircle(220, 120, 50, Color{90, 180, 140, 255});
            DrawText("Render target content", 16, 120, 18, WHITE);
        EndTextureMode();

        BeginDrawing();
            ClearBackground(Color{24, 28, 48, 255});

            BeginMode2D(camera);
                DrawTexture(checker, 0, 0, WHITE);
                DrawTexturePro(
                    renderTarget.texture,
                    Rectangle{ 0, (float)renderTarget.texture.height, (float)renderTarget.texture.width, -(float)renderTarget.texture.height },
                    Rectangle{ 300, 80, 320, 240 },
                    Vec2{ 0, 0 },
                    0.0f,
                    Color{255, 255, 255, 220}
                );
                Vec2 mousePosScreen = GetMousePosition();
                Vec2 mousePosWorld = GetScreenToWorld2D(mousePosScreen, camera);
                DrawCircle(mousePosWorld.x, mousePosWorld.y, 16.0f, Color{255, 180, 60, 200});
            EndMode2D();

            if (useShader) {
                BeginShaderMode(tintShader);
                SetShaderValue(tintShader, locTime, elapsed);
            }

            DrawRectangle(32, 520, 360, 140, Color{10, 10, 30, 230});
            DrawText("Textures and Shaders with QuarkCore", 42, 540, 24, Color{240, 240, 240, 255});
            DrawText(TextFormat("Press SPACE to toggle shader: %s", useShader ? "ON" : "OFF"), 42, 576, 18, Color{200, 220, 255, 255});
            DrawText(TextFormat("Mouse: %.0f, %.0f", GetMousePosition().x, GetMousePosition().y), 42, 604, 18, Color{200, 220, 255, 255});
            DrawText(TextFormat("Zoom: %.2f", camera.zoom), 42, 632, 18, Color{200, 220, 255, 255});

            if (useShader) {
                EndShaderMode();
            }

            DrawTextEx(defaultFont, "Input example: keyboard + mouse wheel", Vec2{520.0f, 560.0f}, 20.0f, 2.0f, LIGHTGRAY);
            DrawText(TextFormat("FPS: %d", GetFPS()), 520, 610, 20, YELLOW);
        EndDrawing();
    }

    StopTextInput();
    UnloadTexture(checker);
    UnloadRenderTexture(renderTarget);
    UnloadShader(tintShader);
    CloseWindow();
    return 0;
}
