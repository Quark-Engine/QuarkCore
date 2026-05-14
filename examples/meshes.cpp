#include "QuarkCore/QuarkCore.hpp"

int main() {
    qc::InitWindow(1280, 720, "QuarkCore Meshes Example", qc::RendererType::OpenGL);
    qc::SetWindowMinimumSize(800, 450);
    qc::SetTargetFPS(60);

    qc::Camera3D camera3d{};
    camera3d.position = { 8.0f, 6.0f, 10.0f };
    camera3d.target = { 0.0f, 1.0f, 0.0f };
    camera3d.up = { 0.0f, 1.0f, 0.0f };
    camera3d.fovy = 45.0f;
    camera3d.projection = qc::CAMERA_PERSPECTIVE;

    qc::Mesh plane = qc::GenMeshPlane(16.0f, 16.0f, 8, 8);
    qc::Mesh cube = qc::GenMeshCube(2.0f, 2.0f, 2.0f);
    qc::Mesh sphere = qc::GenMeshSphere(1.25f, 32, 32);

    qc::UploadMesh(&plane, false);
    qc::UploadMesh(&cube, false);
    qc::UploadMesh(&sphere, false);

    qc::GenMeshTangents(&sphere);

    qc::BoundingBox cubeBounds = qc::GetMeshBoundingBox(cube);
    cubeBounds.min = qc::Vec3{-1.0f, 0.0f, -1.0f};
    cubeBounds.max = qc::Vec3{1.0f, 2.0f, 1.0f};

    qc::Material defaultMaterial{};

    const int instanceCount = 4;
    qc::Matrix instanceTransforms[instanceCount] = {
        qc::Mat4::translation(-5.0f, 1.0f,  0.0f),
        qc::Mat4::translation(-3.0f, 1.0f, -3.0f),
        qc::Mat4::translation(-3.0f, 1.0f,  3.0f),
        qc::Mat4::translation(-1.0f, 1.0f,  0.0f)
    };

    bool exportReady = false;

    while (!qc::WindowShouldClose()) {
        if (qc::IsKeyPressed(qc::KeyboardKey::Space)) {
            if (!exportReady) {
                qc::ExportMesh(sphere, "sphere.obj");
                qc::ExportMeshAsCode(cube, "cube_export.cpp");
                exportReady = true;
                qc::TraceLog(qc::LogLevel::Info, "MESHES", "Exported sphere.obj and cube_export.cpp");
            }
        }

        qc::BeginDrawing();
        qc::ClearBackground(qc::Color{18, 22, 30, 255});

        qc::BeginMode3D(camera3d);
            qc::DrawMesh(plane, defaultMaterial, qc::Mat4::translation(0.0f, 0.0f, 0.0f));
            qc::DrawMesh(cube, defaultMaterial, qc::Mat4::translation(3.0f, 1.0f, 0.0f));
            qc::DrawMesh(sphere, defaultMaterial, qc::Mat4::translation(-3.0f, 1.25f, 0.0f));
            qc::DrawMeshInstanced(cube, defaultMaterial, instanceTransforms, instanceCount);
            qc::DrawBoundingBox(cubeBounds, qc::YELLOW);
        qc::EndMode3D();

        qc::DrawText("Mesh demo: plane, cube, sphere, instanced cube", 20, 20, 20, qc::WHITE);
        qc::DrawText("Press Space to export sphere.obj and cube_export.cpp", 20, 50, 20, qc::LIGHTGRAY);

        qc::EndDrawing();
    }

    qc::UnloadMesh(plane);
    qc::UnloadMesh(cube);
    qc::UnloadMesh(sphere);

    qc::CloseWindow();
    return 0;
}
