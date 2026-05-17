#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/QuarkLights.hpp"
#include <cmath>

using namespace qc;

int main()
{

    InitWindow(800, 480, "QuarkCore - 3D Lights Example", RendererType::OpenGL);
    SetTargetFPS(60);

    Shader shader = LoadShader("resources/lighting.vs", "resources/lighting.fs");

    int locViewPos = GetShaderLocation(shader, "viewPos");
    int locAmbient = GetShaderLocation(shader, "ambient");
    int locDiffuse = GetShaderLocation(shader, "colDiffuse");
    int locModel   = GetShaderLocation(shader, "uModel");
    int locView    = GetShaderLocation(shader, "uView");
    int locProj    = GetShaderLocation(shader, "uProjection");

    Vec4 ambient = {0.1f, 0.1f, 0.1f, 1.0f};
    Vec4 diffuse = {1.0f, 1.0f, 1.0f, 1.0f};

    SetShaderValue(shader, locAmbient, ambient);
    SetShaderValue(shader, locDiffuse, diffuse);

    Light lights[4];
    lights[0] = CreateLight(LIGHT_POINT, Vec3{-2,1,-2}, Vec3{0,0,0}, YELLOW, shader);
    lights[1] = CreateLight(LIGHT_POINT, Vec3{ 2,1, 2}, Vec3{0,0,0}, RED,    shader);
    lights[2] = CreateLight(LIGHT_POINT, Vec3{-2,1, 2}, Vec3{0,0,0}, GREEN,  shader);
    lights[3] = CreateLight(LIGHT_POINT, Vec3{ 2,1,-2}, Vec3{0,0,0}, BLUE,   shader);

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
    mat.maps = new MaterialMap[12]{};
    mat.maps[MATERIAL_MAP_ALBEDO].color   = WHITE;
    mat.maps[MATERIAL_MAP_ALBEDO].texture = tex;

    Camera3D camera = CreateCamera3D();
    camera.position = Vec3{2.0f, 4.0f, 6.0f};
    camera.target   = Vec3{0.0f, 0.5f, 0.0f};
    camera.up       = Vec3{0.0f, 1.0f, 0.0f};
    camera.fovy     = 45.0f;

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KeyboardKey::Y)) lights[0].enabled = !lights[0].enabled;
        if (IsKeyPressed(KeyboardKey::R)) lights[1].enabled = !lights[1].enabled;
        if (IsKeyPressed(KeyboardKey::G)) lights[2].enabled = !lights[2].enabled;
        if (IsKeyPressed(KeyboardKey::B)) lights[3].enabled = !lights[3].enabled;

        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode3D(camera);

        BeginShaderMode(shader);

        SetShaderValueMatrix(shader, locView,  GetMatrixModelview());
        SetShaderValueMatrix(shader, locProj,  GetMatrixProjection());
        SetShaderValue(shader, locViewPos, camera.position);

        for (int i = 0; i < 4; i++)
            UpdateLightValues(shader, lights[i]);

        Mat4 planeT = Mat4::identity();
        SetShaderValueMatrix(shader, locModel, planeT.m);
        DrawMesh(planeMesh, mat, planeT);

        Mat4 cubeT = Mat4::identity();
        SetShaderValueMatrix(shader, locModel, cubeT.m);
        DrawMesh(cubeMesh, mat, cubeT);

        EndShaderMode();

        for (int i = 0; i < 4; i++)
        {
            if (lights[i].enabled)
                DrawSphereEx(lights[i].position, 0.2f, 8, 8, lights[i].color);
            else
                DrawSphereWires(lights[i].position, 0.2f, 8, 8,
                    Fade(lights[i].color, 0.3f));
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
    delete[] mat.maps;
    UnloadShader(shader);
    CloseWindow();
    return 0;
}