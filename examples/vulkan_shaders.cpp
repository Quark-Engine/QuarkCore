#include "QuarkCore/QuarkCore.hpp"
#include <cmath>

namespace {

constexpr const char* kVkSimpleVert = R"(
#version 450 core
layout(location = 0) in vec2 aPosition;
layout(location = 0) out vec2 vPos;

void main() {
    vPos = aPosition * 0.5 + 0.5;
    gl_Position = vec4(aPosition, 0.0, 1.0);
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
    qc::InitWindow(1280, 720, "QuarkCore Vulkan Shader Example", qc::RendererType::Vulkan);
    qc::SetTargetFPS(60);

    qc::Shader shader = qc::LoadShaderFromMemory(kVkSimpleVert, kVkSimpleFrag);

    while (!qc::WindowShouldClose()) {
        qc::Event ev;
        while (qc::PollEvent(ev)) {
            if (ev.type == qc::EventType::KeyDown && ev.key == static_cast<int>(qc::KeyboardKey::Escape)) {
                qc::CloseWindow();
                break;
            }
        }

        qc::BeginDrawing();
        qc::ClearBackground(qc::Color{20, 20, 30, 255});

        qc::BeginShaderMode(shader);
        qc::DrawRectangle(180, 110, 260, 260, qc::Color{180, 120, 220, 255});
        qc::DrawCircle(940, 390, 120, qc::Color{80, 200, 160, 255});
        qc::EndShaderMode();
        qc::EndDrawing();
    }

    qc::UnloadShader(shader);
    qc::CloseWindow();
    return 0;
}
