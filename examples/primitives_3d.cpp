#include "QuarkCore/QuarkCore.hpp"
#include <cmath>

int main() {
    InitWindow(1280, 720, "QuarkCore - 3D Primitives Demo", RendererType::OpenGL);
    SetTargetFPS(0);

    Camera3D camera = CreateCamera3D();
    camera.position = { 10.0f, 10.0f, 10.0f };
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        float time = (float)GetTime();

        camera.position.x = 15.0f * std::sin(time * 0.5f);
        camera.position.z = 15.0f * std::cos(time * 0.5f);

        BeginDrawing();
            ClearBackground(Color{30, 30, 35, 255});

            BeginMode3D(camera);
                DrawPlane({ 0, 0, 0 }, { 20, 20 }, DARKGRAY);
                DrawGrid(20, 1.0f);

                DrawCubeV({ -4, 1, -4 }, { 2, 2, 2 }, RED);
                DrawCubeWiresV({ -4, 1, -4 }, { 2.1f, 2.1f, 2.1f }, WHITE);

                DrawSphereEx({ 4, 1.5f, -4 }, 1.5f, 32, 32, BLUE);
                DrawSphereWires({ 4, 1.5f, -4 }, 1.55f, 16, 16, SKYBLUE);
                
                DrawSphereEx({ 4, 4, -4 }, 1.0f, 6, 10, ORANGE);

                DrawCylinder({ -4, 2, 4 }, 1.0f, 1.0f, 4.0f, 16, GREEN);
                DrawCylinderWires({ -4, 2, 4 }, 1.05f, 1.05f, 4.05f, 16, BLACK);

                Vec3 startPos = { 4, 0, 4 };
                Vec3 endPos = { 4 + std::sin(time) * 2, 5, 4 + std::cos(time) * 2 };
                DrawCylinderEx(startPos, endPos, 1.0f, 0.0f, 12, PURPLE);
                DrawCylinderWiresEx(startPos, endPos, 1.05f, 0.05f, 12, WHITE);

            EndMode3D();

            DrawRectangle(10, 10, 300, 80, Color{0, 0, 0, 150});
            DrawText("3D Primitives Example", 20, 20, 20, WHITE);
            DrawText("Using: CubeV, SphereEx, CylinderEx", 20, 50, 16, LIGHTGRAY);

        EndDrawing();

        SetWindowTitle(("QuarkCore - 3D Primitives Demo | FPS: " + std::to_string(GetFPS())).c_str());
    }

    CloseWindow();
    return 0;
}