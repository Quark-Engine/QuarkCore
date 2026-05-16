#include "QuarkCore/QuarkCore.hpp"
#include "QuarkCore/QuarkLights.hpp"
#include <cmath>

int main()
{
    using namespace qc;

    InitWindow(800, 480, "QuarkCore Lights", RendererType::OpenGL);
    SetTargetFPS(60);

    const char* vsSource = R"(
        #version 330 core
        layout(location=0) in vec2 aPos;
        uniform vec2 uScreenSize;
        void main() {
            vec2 ndc  = (aPos / uScreenSize) * 2.0 - 1.0;
            ndc.y     = -ndc.y;
            gl_Position = vec4(ndc, 0.0, 1.0);
        }
    )";

    const char* fsSource = R"(
        #version 330 core

        struct Light {
            int   enabled;
            int   type;
            vec3  position;
            vec3  target;
            vec4  color;
            float attenuation;
        };

        uniform Light lights[4];
        uniform vec2  uScreenSize;

        out vec4 fragColor;

        void main()
        {
            vec2 uv     = vec2(gl_FragCoord.x, uScreenSize.y - gl_FragCoord.y);
            vec3 result = vec3(0.0);

            for (int i = 0; i < 4; i++)
            {
                if (lights[i].enabled == 0) continue;

                vec2  lightPos  = lights[i].position.xy;
                float dist      = length(uv - lightPos);
                float intensity = 1.0 / (1.0 + dist * dist * lights[i].attenuation);
                result         += lights[i].color.rgb * clamp(intensity, 0.0, 1.0);
            }

            fragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
        }
    )";

    Shader shader = LoadShaderFromMemory(vsSource, fsSource);

    printf("shader id: %d\n", shader.id);
    printf("lights[0].enabled: %d\n", GetShaderLocation(shader, "lights[0].enabled"));
    printf("lights[0].position: %d\n", GetShaderLocation(shader, "lights[0].position"));
    printf("lights[0].color: %d\n", GetShaderLocation(shader, "lights[0].color"));
    printf("lights[0].attenuation: %d\n", GetShaderLocation(shader, "lights[0].attenuation"));
    printf("uScreenSize: %d\n", GetShaderLocation(shader, "uScreenSize"));

    int locScreenSize = GetShaderLocation(shader, "uScreenSize");

    Light light0 = CreateLight(
        LIGHT_POINT,
        Vec3{ 400.0f, 240.0f, 0.0f },
        Vec3{ 0.0f,   0.0f,   0.0f },
        GetColor(0xFFAA00),
        shader
    );
    light0.attenuation = 0.0001f;

    Light light1 = CreateLight(
        LIGHT_POINT,
        Vec3{ 400.0f, 240.0f, 0.0f },
        Vec3{ 0.0f,   0.0f,   0.0f },
        GetColor(0x0088FF),
        shader
    );
    light1.attenuation = 0.0001f;

    while (!WindowShouldClose())
    {
        float t = (float)GetTime();

        light0.position.x = (sinf(t)         * 0.3f + 0.5f) * 800.0f;
        light0.position.y = (cosf(t)         * 0.3f + 0.5f) * 480.0f;

        light1.position.x = (sinf(-t + 1.5f) * 0.3f + 0.5f) * 800.0f;
        light1.position.y = (cosf(-t + 1.5f) * 0.3f + 0.5f) * 480.0f;

        BeginDrawing();
        ClearBackground(BLACK);

        BeginShaderMode(shader);
        SetShaderValue(shader, locScreenSize, Vec2{
            (float)GetScreenWidth(),
            (float)GetScreenHeight()
        });
        UpdateLightValues(shader, light0);
        UpdateLightValues(shader, light1);
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), WHITE);
        EndShaderMode();

        EndDrawing();
    }

    UnloadShader(shader);
    CloseWindow();
    return 0;
}