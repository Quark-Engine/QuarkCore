#include "QuarkCore/QuarkCore.hpp"
#include <cmath>

int main() {
    qc::InitWindow(1280, 720, "QuarkCore - 3D Primitives Demo");
    qc::SetTargetFPS(60);

    qc::Camera3D camera = qc::CreateCamera3D();
    camera.position = { 10.0f, 10.0f, 10.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = qc::CAMERA_PERSPECTIVE;

    while (!qc::WindowShouldClose()) {
        float time = (float)qc::GetTime();

        camera.position.x = 15.0f * std::sin(time * 0.5f);
        camera.position.z = 15.0f * std::cos(time * 0.5f);

        qc::BeginDrawing();
            qc::ClearBackground(qc::Color{30, 30, 35, 255});

            qc::BeginMode3D(camera);
                qc::DrawGrid(20, 1.0f);
                qc::DrawPlane({ 0, 0, 0 }, { 20, 20 }, qc::DARKGRAY);

                qc::DrawCubeV({ -4, 1, -4 }, { 2, 2, 2 }, qc::RED);
                qc::DrawCubeWiresV({ -4, 1, -4 }, { 2.1f, 2.1f, 2.1f }, qc::WHITE);

                qc::DrawSphereEx({ 4, 1.5f, -4 }, 1.5f, 32, 32, qc::BLUE);
                qc::DrawSphereWires({ 4, 1.5f, -4 }, 1.55f, 16, 16, qc::SKYBLUE);
                
                qc::DrawSphereEx({ 4, 4, -4 }, 1.0f, 6, 10, qc::ORANGE);

                qc::DrawCylinder({ -4, 2, 4 }, 1.0f, 1.0f, 4.0f, 16, qc::GREEN);
                qc::DrawCylinderWires({ -4, 2, 4 }, 1.05f, 1.05f, 4.05f, 16, qc::BLACK);

                qc::Vec3 startPos = { 4, 0, 4 };
                qc::Vec3 endPos = { 4 + std::sin(time) * 2, 5, 4 + std::cos(time) * 2 };
                qc::DrawCylinderEx(startPos, endPos, 1.0f, 0.0f, 12, qc::PURPLE);
                qc::DrawCylinderWiresEx(startPos, endPos, 1.05f, 0.05f, 12, qc::WHITE);

            qc::EndMode3D();

            qc::DrawRectangle(10, 10, 300, 80, qc::Color{0, 0, 0, 150});
            qc::DrawText("3D Primitives Example", 20, 20, 20, qc::WHITE);
            qc::DrawText("Using: CubeV, SphereEx, CylinderEx", 20, 50, 16, qc::LIGHTGRAY);

        qc::EndDrawing();
    }

    qc::CloseWindow();
    return 0;
}