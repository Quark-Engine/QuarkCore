#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/QuarkLights.hpp"
#include <cmath>

using namespace qc;

int main() {
    InitWindow(800, 480, "QuarkCore - Vulkan 3D Lights Example", RendererType::Vulkan);
    SetTargetFPS(60);

    Shader shader = LoadShader("resources/vulkan_lighting.vs", "resources/vulkan_lighting.fs");
    Shader shadowShader = LoadShader("resources/vulkan_shadow_depth.vs", "resources/vulkan_shadow_depth.fs");

    int locAmbient = GetShaderLocation(shader, "ambient");
    int locDiffuse = GetShaderLocation(shader, "colDiffuse");
    int locViewPos = GetShaderLocation(shader, "viewPos");
    int locLightViews[4];
    int locLightProjections[4];
    for (int i = 0; i < 4; ++i) {
        locLightViews[i] = GetShaderLocation(shader, TextFormat("lightViews[%i]", i));
        locLightProjections[i] = GetShaderLocation(shader, TextFormat("lightProjections[%i]", i));
    }

    Vec4 ambient = {0.1f, 0.1f, 0.1f, 1.0f};
    Vec4 diffuse = {1.0f, 1.0f, 1.0f, 1.0f};

    SetShaderValue(shader, locAmbient, ambient);
    SetShaderValue(shader, locDiffuse, diffuse);

    Light lights[4];
    lights[0] = CreateLight(LIGHT_POINT, Vec3{-3, 4,  3}, Vec3{0, 0, 0}, YELLOW, shader);
    lights[1] = CreateLight(LIGHT_POINT, Vec3{ 3, 3,  3}, Vec3{0, 0, 0}, RED,    shader);
    lights[2] = CreateLight(LIGHT_POINT, Vec3{-3, 3, -3}, Vec3{0, 0, 0}, GREEN,  shader);
    lights[3] = CreateLight(LIGHT_POINT, Vec3{ 3, 3, -3}, Vec3{0, 0, 0}, BLUE,   shader);

    for (int i = 0; i < 4; ++i)
        lights[i].attenuation = 0.08f;

    qc::Texture2D tex = qc::GenCheckerTexture(
        256, 256, 32,
        qc::Color{215, 225, 235, 255},
        qc::Color{70, 100, 180, 255}
    );

    Mesh planeMesh = GenMeshPlane(10.0f, 10.0f, 1, 1);
    Mesh cubeMesh  = GenMeshCube(2.0f, 4.0f, 2.0f);
    UploadMesh(&planeMesh, false);
    UploadMesh(&cubeMesh,  false);

    Material mat = {};
    mat.shader = &shader;
    mat.maps = new MaterialMap[12]{};
    mat.maps[MATERIAL_MAP_ALBEDO].color   = WHITE;
    mat.maps[MATERIAL_MAP_ALBEDO].texture = tex;

    Material shadowMat = {};
    shadowMat.shader = &shadowShader;

    RenderTexture2D shadowMaps[4];
    Camera3D shadowCameras[4];
    for (int i = 0; i < 4; ++i) {
        shadowMaps[i] = LoadRenderTexture(1024, 1024);
        shadowCameras[i] = CreateCamera3D();
        shadowCameras[i].position = lights[i].position;
        shadowCameras[i].target   = lights[i].target;
        shadowCameras[i].up       = Vec3{0.0f, 1.0f, 0.0f};
        shadowCameras[i].fovy     = 55.0f;
    }

    Camera3D camera = CreateCamera3D();
    camera.position = Vec3{2.0f, 4.0f, 6.0f};
    camera.target   = Vec3{0.0f, 0.5f, 0.0f};
    camera.up       = Vec3{0.0f, 1.0f, 0.0f};
    camera.fovy     = 45.0f;

    while (!WindowShouldClose()) {
        float orbitTime = (float)GetTime() * 0.35f;
        camera.position = Vec3{
            std::sin(orbitTime) * 9.0f,
            4.5f,
            std::cos(orbitTime) * 9.0f
        };
        camera.target = Vec3{0.0f, 0.5f, 0.0f};

        if (IsKeyPressed(KeyboardKey::Y)) lights[0].enabled = !lights[0].enabled;
        if (IsKeyPressed(KeyboardKey::R)) lights[1].enabled = !lights[1].enabled;
        if (IsKeyPressed(KeyboardKey::G)) lights[2].enabled = !lights[2].enabled;
        if (IsKeyPressed(KeyboardKey::B)) lights[3].enabled = !lights[3].enabled;

        mat.maps[MATERIAL_MAP_HEIGHT + 0].texture = shadowMaps[0].texture;
        mat.maps[MATERIAL_MAP_HEIGHT + 1].texture = shadowMaps[1].texture;
        mat.maps[MATERIAL_MAP_HEIGHT + 2].texture = shadowMaps[2].texture;
        mat.maps[MATERIAL_MAP_HEIGHT + 3].texture = shadowMaps[3].texture;

        Mat4 cubeT = Mat4::identity();
        for (int i = 0; i < 4; ++i) {
            BeginTextureMode(shadowMaps[i]);
            ClearBackground(WHITE);
            BeginMode3D(shadowCameras[i]);
            BeginShaderMode(shadowShader);
            DrawMesh(cubeMesh, shadowMat, cubeT);

            Mat4 lightView{};
            Mat4 lightProjection{};
            for (int m = 0; m < 16; ++m) {
                lightView.m[m] = GetMatrixModelview()[m];
                lightProjection.m[m] = GetMatrixProjection()[m];
            }
            SetShaderValueMatrix(shader, locLightViews[i], lightView.m);
            SetShaderValueMatrix(shader, locLightProjections[i], lightProjection.m);

            EndShaderMode();
            EndMode3D();
            EndTextureMode();
        }

        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode3D(camera);

        BeginShaderMode(shader);

        SetShaderValue(shader, locViewPos, camera.position);

        for (int i = 0; i < 4; i++)
            UpdateLightValues(shader, lights[i]);

        Mat4 planeT = Mat4::identity();
        DrawMesh(planeMesh, mat, planeT);

        cubeT = Mat4::identity();
        DrawMesh(cubeMesh, mat, cubeT);

        EndShaderMode();

        for (int i = 0; i < 4; ++i) {
            if (lights[i].enabled)
                DrawSphereEx(lights[i].position, 0.2f, 8, 8, lights[i].color);
            else
                DrawSphereWires(lights[i].position, 0.2f, 8, 8, Fade(lights[i].color, 0.3f));
        }

        DrawGrid(10, 1.0f);
        EndMode3D();

        DrawText("Use keys [Y][R][G][B] to toggle lights", 10, 10, 20, DARKGRAY);
        EndDrawing();

        Event ev;
        while (PollEvent(ev)) {}
    }

    UnloadMesh(planeMesh);
    UnloadMesh(cubeMesh);
    for (int i = 0; i < 4; ++i)
        UnloadRenderTexture(shadowMaps[i]);
    delete[] mat.maps;
    UnloadShader(shader);
    UnloadShader(shadowShader);
    CloseWindow();
    return 0;
}
