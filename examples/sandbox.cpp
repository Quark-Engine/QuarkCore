#include "QuarkCore/QuarkCore.hpp"

int main() {
    qc::InitWindow(1280, 720, "QuarkCore Sandbox", qc::RendererType::OpenGL);
    qc::SetWindowMinimumSize(800, 450);
    qc::StartTextInput();
    qc::SetLogLevel(qc::LogLevel::Trace);
    qc::SetTargetFPS(0);
    qc::TraceLog(qc::LogLevel::Info, "CORE", "Sandbox booted");
    qc::TraceLog(
        qc::LogLevel::Info,
        "CORE",
        qc::TextFormat("Current monitor refresh rate: %.2f Hz", qc::GetCurrentMonitorRefreshRate())
    );

    qc::Texture2D checker = qc::GenCheckerTexture(
        160,
        160,
        20,
        qc::Color{245, 245, 245, 255},
        qc::Color{40, 120, 210, 255}
    );

    qc::Camera2D camera2d = { 0 };
    camera2d.target = { 220.0f, 340.0f };
    camera2d.offset = { 1280 / 2.0f, 720 / 2.0f };
    camera2d.zoom = 1.0f;

    qc::Camera3D camera3d;
    camera3d.position = { 0.0f, 10.0f, 10.0f };
    camera3d.target = { 0.0f, 0.0f, 0.0f };
    camera3d.up = { 0.0f, 1.0f, 0.0f };
    camera3d.fovy = 45.0f;
    camera3d.projection = qc::CAMERA_PERSPECTIVE;

    qc::Model model = qc::LoadModel("lantern/lantern.obj");

    qc::RenderTexture2D target = qc::LoadRenderTexture(320, 240);

    qc::Font defaultFont = qc::GetDefaultFont();

    while (!qc::WindowShouldClose()) {
        if (qc::IsKeyPressed(qc::KeyboardKey::Space)) {
            qc::TraceLog(
                qc::LogLevel::Info,
                "INPUT",
                qc::TextFormat("Space pressed | fps=%d dt=%.4f", qc::GetFPS(), qc::GetDeltaTime())
            );
        }

        qc::BeginDrawing();
        qc::ClearBackground(qc::Color{20, 24, 32, 255});

        qc::BeginTextureMode(target);
            qc::ClearBackground(qc::WHITE);
            qc::DrawRectangle(10, 10, 100, 100, qc::RED);
            qc::DrawCircle(160, 120, 40, qc::BLUE);
        qc::EndTextureMode();

        qc::BeginMode2D(camera2d);
            qc::DrawGrid(100, 64);
            qc::DrawRectangleV(camera2d.target, { 50, 50 }, qc::RED);
            qc::DrawCircle(600, 400, 40, qc::BLUE);
            qc::DrawRectangle(800, 200, 120, 80, qc::GREEN);
        qc::EndMode2D();

        qc::BeginMode3D(camera3d);
            qc::DrawPlane({ 0, 0, 0 }, { 32, 32 }, qc::LIGHTGRAY);
            qc::DrawCube({ 0, 1, 0 }, 2, 2, 2, qc::RED);
            qc::DrawGrid(20, 1.0f);
            qc::DrawModelEx(model, qc::Mat4::rotationZ(qc::GetTime()));
            qc::DrawModel(model, { 3, 0, 0 }, 1.0f, qc::GREEN);
        qc::EndMode3D();

        
        qc::DrawTexturePro(
            target.texture,
            qc::Rectangle{ 0, (float)target.texture.height, (float)target.texture.width, -(float)target.texture.height },
            qc::Rectangle{ 20, 20, 320, 240 },
            qc::Vec2{ 0, 0 },
            0.0f,
            qc::WHITE
        );

        qc::DrawText("Hello, QuarkCore!", 360, 40, 32, qc::WHITE);

        qc::Vec2 textSize = qc::MeasureTextEx(defaultFont, "DrawTextEx example", 24.0f, 2.0f);
        qc::DrawRectangle(360.0f, 80.0f, textSize.x + 16.0f, textSize.y + 12.0f, qc::Color{20, 20, 40, 180});
        qc::DrawTextEx(defaultFont, "DrawTextEx example", qc::Vec2{372.0f, 88.0f}, 24.0f, 2.0f, qc::YELLOW);

        int measuredWidth = qc::MeasureText("Measured text", 20);
        qc::DrawText(qc::TextFormat("Measured width: %d", measuredWidth), 360, 140, 20, qc::LIGHTGRAY);

        qc::EndDrawing();
    }

    qc::StopTextInput();
    qc::UnloadTexture(checker);
    qc::UnloadRenderTexture(target);
    qc::CloseWindow();
    return 0;
}
