#include "QuarkCore/QuarkCore.hpp"

int main() {
    InitWindow(1280, 720, "QuarkCore VK Sandbox", RendererType::Vulkan);
    SetWindowMinimumSize(800, 450);
    StartTextInput();
    SetLogLevel(LogLevel::Trace);
    SetTargetFPS(0);
    TraceLog(LogLevel::Info, "CORE", "Sandbox booted");
    TraceLog(
        LogLevel::Info,
        "CORE",
        TextFormat("Current monitor refresh rate: %.2f Hz", GetCurrentMonitorRefreshRate())
    );

    Texture2D checker = GenCheckerTexture(
        160,
        160,
        20,
        Color{245, 245, 245, 255},
        Color{40, 120, 210, 255}
    );
    Texture2D vulkan = LoadTexture("resources/vulkan.png");

    Camera2D camera2d;
    camera2d.target = { 220.0f, 340.0f };
    camera2d.offset = { 1280 / 2.0f, 720 / 2.0f };
    camera2d.zoom = 1.0f;

    Camera3D camera3d;
    camera3d.position = { 0.0f, 10.0f, 10.0f };
    camera3d.target = { 0.0f, 0.0f, 0.0f };
    camera3d.up = { 0.0f, 1.0f, 0.0f };
    camera3d.fovy = 45.0f;
    camera3d.projection = CAMERA_PERSPECTIVE;

    Model model = LoadModel("resources/lantern.obj");

    RenderTexture2D target = LoadRenderTexture(320, 240);

    Font defaultFont = GetDefaultFont();

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_SPACE)) {
            TraceLog(
                LogLevel::Info,
                "INPUT",
                TextFormat("Space pressed | fps=%d dt=%.4f", GetFPS(), GetDeltaTime())
            );
        }

        BeginDrawing();
        ClearBackground(Color{20, 24, 32, 255});

        BeginTextureMode(target);
            ClearBackground(WHITE);
            DrawRectangle(10, 10, 100, 100, RED);
            DrawCircle(160, 120, 40, BLUE);
        EndTextureMode();

        BeginMode2D(camera2d);
            DrawGrid(100, 64);
            DrawRectangleV(camera2d.target, { 50, 50 }, RED);
            DrawCircle(600, 400, 40, BLUE);
            DrawRectangle(800, 200, 120, 80, GREEN);
        EndMode2D();

        BeginMode3D(camera3d);
            DrawPlane({ 0, 0, 0 }, { 32, 32 }, LIGHTGRAY);
            DrawCube({ 0, 1, 0 }, 2, 2, 2, RED);
            DrawGrid(20, 1.0f);
            DrawModelEx(model, Mat4::rotationZ(GetTime()));
            DrawModel(model, { 3, 0, 0 }, 1.0f, GREEN);
        EndMode3D();

        constexpr float vulkanScale = 0.25f;
        constexpr float vulkanTopMargin = 20.0f;
        DrawTextureEx(
            vulkan,
            Vec2{
                static_cast<float>(GetScreenWidth()) - vulkan.width * vulkanScale,
                vulkanTopMargin
            },
            0.0f,
            vulkanScale,
            WHITE
        );

        DrawTexturePro(
            target.texture,
            Rectangle{ 0, (float)target.texture.height, (float)target.texture.width, -(float)target.texture.height },
            Rectangle{ 20, 20, 320, 240 },
            Vec2{ 0, 0 },
            0.0f,
            WHITE
        );

        DrawText("Hello, QuarkCore!", 360, 40, 32, WHITE);

        Vec2 textSize = MeasureTextEx(defaultFont, "DrawTextEx example", 24.0f, 2.0f);
        DrawRectangle(360.0f, 80.0f, textSize.x + 16.0f, textSize.y + 12.0f, Color{20, 20, 40, 180});
        DrawTextEx(defaultFont, "DrawTextEx example", Vec2{372.0f, 88.0f}, 24.0f, 2.0f, YELLOW);

        int measuredWidth = MeasureText("Measured text", 20);
        DrawText(TextFormat("Measured width: %d", measuredWidth), 360, 140, 20, LIGHTGRAY);

        EndDrawing();
    }

    StopTextInput();
    UnloadTexture(checker);
    UnloadTexture(vulkan);
    UnloadRenderTexture(target);
    CloseWindow();
    return 0;
}
