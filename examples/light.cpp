#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/QuarkLights.hpp"

int main()
{
    using namespace qc;

    InitWindow(800, 480, "QuarkCore - Lights Example", RendererType::OpenGL);
    SetTargetFPS(60);

    const char* fsSource = R"(
    #version 330 core
    struct Light {
        int enabled;
        int type;
        vec3 position;
        vec3 target;
        vec4 color;
        float attenuation;
    };
    
    uniform Light lights[4];
    out vec4 fragColor;
    void main() {
        if (lights[0].enabled == 1) {
            fragColor = lights[0].color;
        } else {
            fragColor = vec4(1.0, 1.0, 1.0, 1.0);
        }
    }
)";

    Shader shader = LoadShaderFromMemory(nullptr, fsSource);

    Light light = CreateLight(LIGHT_POINT, Vec3{0.0f, 1.5f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f}, GetColor(0xFFAA00), shader);

    while (!WindowShouldClose())
    {
        float t = (float)GetTime();
        light.position.x = sinf(t) * 2.0f;
        light.position.z = cosf(t) * 2.0f;
        UpdateLightValues(shader, light);

        BeginDrawing();
        ClearBackground(GetColor(0x1E1E1E));
        EndDrawing();

        Event ev;
        while (PollEvent(ev)) { }
    }

    UnloadShader(shader);
    CloseWindow();

    return 0;
}
