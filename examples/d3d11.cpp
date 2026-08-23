#include "QuarkCore/QuarkCore.hpp"

int main() {
    qc::InitWindow(1280, 720, "QuarkCore D3D11 Example", qc::RendererType::D3D11);
    qc::SetWindowMinimumSize(800, 450);
    qc::SetTargetFPS(60);

    while (!qc::WindowShouldClose()) {
        qc::BeginDrawing();

        qc::ClearBackground(qc::Color{18, 24, 38, 255});
        qc::DrawTriangle(
            qc::Vec2{640, 100},
            qc::Vec2{540, 300},
            qc::Vec2{740, 300},
            qc::RED
        );

        qc::EndDrawing();
    }

    qc::CloseWindow();
    return 0;
}
