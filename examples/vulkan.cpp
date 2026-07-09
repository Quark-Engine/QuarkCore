#include "QuarkCore/QuarkCore.hpp"

int main() {
    qc::InitWindow(1280, 720, "QuarkCore Vulkan Example", qc::RendererType::Vulkan);
    qc::SetWindowMinimumSize(800, 450);
    qc::SetTargetFPS(60);

    while (!qc::WindowShouldClose()) {
        qc::BeginDrawing();

        qc::ClearBackground(qc::WHITE);
        qc::DrawText("Hello, Vulkan!", 20, 20, 32, qc::GREEN);
        qc::DrawTriangle(qc::Vector2{ 640, 100 }, qc::Vector2{ 540, 300 }, qc::Vector2{ 740, 300 }, qc::RED);

        qc::EndDrawing();
    }

    qc::CloseWindow();
    return 0;
}
