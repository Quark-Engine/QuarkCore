#include "QuarkCore/QuarkCore.hpp"

namespace {

constexpr const char* kUnlitVertexShader = R"glsl(
#version 450
layout(location = 0) in vec4 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aNormal;
layout(location = 4) in vec4 aWorld;
layout(location = 0) out vec4 vColor;
layout(set = 0, binding = 0) uniform Matrices {
    mat4 model;
    mat4 view;
    mat4 projection;
} matrices;
void main() {
    gl_Position = matrices.projection * matrices.view * aWorld;
    vColor = aColor;
}
)glsl";

constexpr const char* kUnlitFragmentShader = R"glsl(
#version 450
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vColor;
}
)glsl";

}

int main() {
    InitWindow(1280, 720, "QuarkCore Meshes Example", RendererType::Vulkan);
    SetWindowMinimumSize(800, 450);
    SetTargetFPS(60);

    Camera3D camera3d{};
    camera3d.position = { 8.0f, 6.0f, 10.0f };
    camera3d.target = { 0.0f, 1.0f, 0.0f };
    camera3d.up = { 0.0f, 1.0f, 0.0f };
    camera3d.fovy = 45.0f;
    camera3d.projection = CAMERA_PERSPECTIVE;

    Mesh plane = GenMeshPlane(16.0f, 16.0f, 8, 8);
    Mesh cube = GenMeshCube(2.0f, 2.0f, 2.0f);
    Mesh sphere = GenMeshSphere(1.25f, 32, 32);

    UploadMesh(&plane, false);
    UploadMesh(&cube, false);
    UploadMesh(&sphere, false);

    GenMeshTangents(&sphere);

    BoundingBox cubeBounds = GetMeshBoundingBox(cube);
    cubeBounds.min = Vec3{-1.0f, 0.0f, -1.0f};
    cubeBounds.max = Vec3{1.0f, 2.0f, 1.0f};

    Material defaultMaterial{};
    Shader unlitShader = LoadShaderFromMemory(kUnlitVertexShader, kUnlitFragmentShader);
    defaultMaterial.shader = &unlitShader;

    const int instanceCount = 4;
    Matrix instanceTransforms[instanceCount] = {
        Mat4::translation(-5.0f, 1.0f,  0.0f),
        Mat4::translation(-3.0f, 1.0f, -3.0f),
        Mat4::translation(-3.0f, 1.0f,  3.0f),
        Mat4::translation(-1.0f, 1.0f,  0.0f)
    };

    bool exportReady = false;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_SPACE)) {
            if (!exportReady) {
                ExportMesh(sphere, "sphere.obj");
                ExportMeshAsCode(cube, "cube_export.cpp");
                exportReady = true;
                TraceLog(LogLevel::Info, "MESHES", "Exported sphere.obj and cube_export.cpp");
            }
        }

        BeginDrawing();
        ClearBackground(Color{18, 22, 30, 255});

        BeginMode3D(camera3d);
            DrawMesh(plane, defaultMaterial, Mat4::translation(0.0f, 0.0f, 0.0f));
            DrawMesh(cube, defaultMaterial, Mat4::translation(3.0f, 1.0f, 0.0f));
            DrawMesh(sphere, defaultMaterial, Mat4::translation(-3.0f, 1.25f, 0.0f));
            DrawMeshInstanced(cube, defaultMaterial, instanceTransforms, instanceCount);
            DrawBoundingBox(cubeBounds, YELLOW);
        EndMode3D();

        DrawText("Mesh demo: plane, cube, sphere, instanced cube", 20, 20, 20, WHITE);
        DrawText("Press Space to export sphere.obj and cube_export.cpp", 20, 50, 20, LIGHTGRAY);

        EndDrawing();
    }

    UnloadMesh(plane);
    UnloadMesh(cube);
    UnloadMesh(sphere);

    UnloadShader(unlitShader);
    CloseWindow();
    return 0;
}
