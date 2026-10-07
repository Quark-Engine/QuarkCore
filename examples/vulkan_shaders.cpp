#include "QuarkCore/QuarkCore.hpp"
#include <cmath>

namespace {

constexpr const char* kVkSimpleVert = R"(
#version 450 core
layout(location = 0) in vec2 aPosition;
layout(location = 0) out vec2 vPos;

layout(push_constant) uniform ScreenSize {
    vec2 size;
};

void main() {
    vPos = vec2(aPosition.x / size.x, 1.0 - aPosition.y / size.y);
    vec2 clipPosition = vec2(
        (aPosition.x / size.x) * 2.0 - 1.0,
        (aPosition.y / size.y) * 2.0 - 1.0);
    gl_Position = vec4(clipPosition, 0.0, 1.0);
}
)";

constexpr const char* kVkSimpleFrag = R"(
#version 450 core
layout(location = 0) in vec2 vPos;
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 warm = vec3(0.95, 0.25, 0.55);
    vec3 cool = vec3(0.25, 0.65, 1.0);
    vec3 accent = vec3(0.95, 0.85, 0.35);

    vec3 color = mix(warm, cool, vPos.x);
    color = mix(color, accent, vPos.y);
    color += 0.08 * vec3(sin(vPos.x * 12.0), cos(vPos.y * 10.0), sin((vPos.x + vPos.y) * 8.0));

    fragColor = vec4(color, 1.0);
}
)";

} // namespace

int main() {
    InitWindow(1280, 720, "QuarkCore Vulkan Shader Example", RendererType::Vulkan);
    SetTargetFPS(60);

    Shader shader = LoadShaderFromMemory(kVkSimpleVert, kVkSimpleFrag);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(Color{20, 20, 30, 255});

        BeginShaderMode(shader);
        DrawRectangle(180, 110, 260, 260, Color{180, 120, 220, 255});
        DrawCircle(940, 390, 120, Color{80, 200, 160, 255});
        EndShaderMode();
        EndDrawing();
    }

    UnloadShader(shader);
    CloseWindow();
    return 0;
}
