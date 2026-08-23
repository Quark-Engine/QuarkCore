#include "QuarkCore/QuarkCore.hpp"
#include <algorithm>
#include <cmath>

using namespace qc;

namespace {

void DrawScene(const Mesh& plane, const Mesh& cube, const Material& material, bool includePlane) {
    if (includePlane) {
        DrawMesh(plane, material, Mat4::identity());
    }
    DrawMesh(cube, material, Mat4::translation(0.0f, 2.0f, 0.0f));
    DrawSphereEx(Vec3{-2.6f, 1.0f, 0.5f}, 1.0f, 16, 24, Color{220, 90, 70, 255});
    DrawSphereEx(Vec3{2.4f, 0.8f, -1.2f}, 0.8f, 16, 24, Color{90, 170, 235, 255});
}

} // namespace

int main() {
    InitWindow(1024, 640, "QuarkCore - Vulkan 3D Shadows", RendererType::Vulkan);
    SetTargetFPS(60);

    Mesh plane = GenMeshPlane(12.0f, 12.0f, 1, 1);
    Mesh cube = GenMeshCube(2.0f, 4.0f, 2.0f);
    UploadMesh(&plane, false);
    UploadMesh(&cube, false);

    Material material{};

    Camera3D camera = CreateCamera3D();
    camera.position = Vec3{9.0f, 6.0f, 9.0f};
    camera.target = Vec3{0.0f, 1.0f, 0.0f};
    camera.up = Vec3{0.0f, 1.0f, 0.0f};
    camera.fovy = 45.0f;
    bool lightEnabled[4] = {true, true, true, true};

    while (!WindowShouldClose()) {
        const float t = static_cast<float>(GetTime()) * 0.35f;
        camera.position = Vec3{std::sin(t) * 10.0f, 5.5f, std::cos(t) * 10.0f};

        if (IsKeyPressed(KeyboardKey::Y)) lightEnabled[0] = !lightEnabled[0];
        if (IsKeyPressed(KeyboardKey::R)) lightEnabled[1] = !lightEnabled[1];
        if (IsKeyPressed(KeyboardKey::G)) lightEnabled[2] = !lightEnabled[2];
        if (IsKeyPressed(KeyboardKey::B)) lightEnabled[3] = !lightEnabled[3];
        for (int i = 0; i < 4; ++i) Set3DLightEnabled(i, lightEnabled[i]);

        BeginDrawing();
        ClearBackground(Color{18, 22, 32, 255});
        BeginMode3D(camera);
        DrawScene(plane, cube, material, true);
        DrawGrid(12, 1.0f);
        EndMode3D();
        DrawText("Vulkan shadow maps  |  Y R G B: toggle lights", 16, 16, 20, WHITE);
        EndDrawing();

        Event event;
        while (PollEvent(event)) {}
    }

    UnloadMesh(plane);
    UnloadMesh(cube);
    CloseWindow();
    return 0;
}
