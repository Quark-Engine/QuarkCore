#include "QuarkCore/QuarkCore.hpp"

int main() {
    qc::InitWindow(1280, 720, "QuarkCore Vulkan Example", qc::RendererType::Vulkan);
    qc::SetWindowMinimumSize(800, 450);
    qc::SetTargetFPS(60);

    while (!qc::WindowShouldClose()) {
        
    }

    qc::CloseWindow();
    return 0;
}
