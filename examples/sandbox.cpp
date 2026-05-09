#include "QuarkCore/QuarkCore.hpp"

int main() {
    qc::InitWindow(1280, 720, "QuarkCore Sandbox");
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

    qc::Model model = qc::LoadModel("lantern/lantern.obj");
    
    float modelRotationY = 0.0f;
    float modelScale = 1.0f;
    float speed = 260.0f;
    int resizeCount = 0;

    qc::Vec2 player{220.0f, 340.0f};

    while (!qc::WindowShouldClose()) {
        const float dt = qc::GetDeltaTime();

        modelRotationY += dt;

        if (qc::IsKeyDown(qc::KeyboardKey::A) || qc::IsKeyDown(qc::KeyboardKey::Left)) {
            player.x -= speed * dt;
        }
        if (qc::IsKeyDown(qc::KeyboardKey::D) || qc::IsKeyDown(qc::KeyboardKey::Right)) {
            player.x += speed * dt;
        }
        if (qc::IsKeyDown(qc::KeyboardKey::W) || qc::IsKeyDown(qc::KeyboardKey::Up)) {
            player.y -= speed * dt;
        }
        if (qc::IsKeyDown(qc::KeyboardKey::S) || qc::IsKeyDown(qc::KeyboardKey::Down)) {
            player.y += speed * dt;
        }

        if (qc::IsKeyPressed(qc::KeyboardKey::Space)) {
            qc::SetWindowTitle(qc::TextFormat("QuarkCore Sandbox | FPS %d | Resizes %d", qc::GetFPS(), resizeCount));
            qc::TraceLog(
                qc::LogLevel::Info,
                "INPUT",
                qc::TextFormat("Space pressed | fps=%d dt=%.4f", qc::GetFPS(), qc::GetDeltaTime())
            );
        }
        if (qc::IsKeyPressed(qc::KeyboardKey::Escape)) {
            qc::ToggleFullscreen();
        }

        const qc::Vec2 mouse = qc::GetMousePosition();
        const qc::Color orbColor = qc::IsMouseButtonDown(qc::MouseButton::Left) ? qc::RED : qc::ORANGE;

        qc::BeginDrawing();
        qc::ClearBackground(qc::Color{20, 24, 32, 255});

        qc::DrawRectangle(60.0f, 60.0f, 520.0f, 180.0f, qc::SKYBLUE);
        qc::DrawRectangle(qc::Rectangle{84.0f, 84.0f, 472.0f, 132.0f}, qc::Color{14, 33, 55, 255});
        qc::DrawTexture(checker, player.x, player.y, qc::WHITE);
        qc::DrawCircle(mouse.x, mouse.y, 26.0f, orbColor);
        qc::DrawCircle(980.0f, 180.0f, 72.0f, qc::GREEN);

        qc::Begin3D();
        qc::Mat4 view = qc::Mat4::lookAt(
            qc::Vec3{0.0f, 2.0f, 3.0f},
            qc::Vec3{0.0f, 0.0f, 0.0f},
            qc::Vec3{0.0f, 1.0f, 0.0f}
        );
        qc::Mat4 projection = qc::Mat4::perspective(
            3.14159f / 4.0f,
            static_cast<float>(qc::GetScreenWidth()) / static_cast<float>(qc::GetScreenHeight()),
            0.1f,
            100.0f
        );
        qc::Set3DView(view, projection);
        qc::DrawModel(model, qc::Vec3{0.0f, 0.0f, 0.0f}, modelScale, 0.0f, modelRotationY, 0.0f);
        qc::End3D();

        qc::EndDrawing();
        qc::SetWindowTitle(qc::TextFormat("QuarkCore Sandbox | FPS %d | Resizes %d", qc::GetFPS(), resizeCount));
    }

    qc::StopTextInput();
    qc::UnloadTexture(checker);
    qc::UnloadModel(model);
    qc::CloseWindow();
    return 0;
}
