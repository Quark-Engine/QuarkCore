#include "QuarkCore/QuarkCore.hpp"
#include <algorithm>
#include <cmath>

using namespace qc;

namespace {

constexpr const char* kVertexShader = R"(
#version 450
layout(location = 0) in vec4 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aNormal;
layout(location = 4) in vec4 aWorld;
layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec3 vWorld;

layout(set = 0, binding = 0) uniform Matrices {
    mat4 model;
    mat4 view;
    mat4 projection;
} matrices;

void main() {
    vTexCoord = aTexCoord;
    vColor = aColor;
    vWorld = aWorld.xyz;
    gl_Position = matrices.projection * matrices.view * aWorld;
}
)";

constexpr const char* kFragmentShader = R"(
#version 450
layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec3 vWorld;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 1) uniform sampler2D uTexture;
layout(push_constant) uniform Lights {
    vec4 lightPositions[4];
    vec4 lightColors[4];
    vec4 timeData;
    vec4 lightEnabled;
} lights;

void main() {
    vec3 normal = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
    vec3 lighting = vec3(0.10);

    for (int i = 0; i < 4; ++i) {
        float enabled = lights.lightEnabled[i];
        vec3 toLight = lights.lightPositions[i].xyz - vWorld;
        float distanceToLight = length(toLight);
        float diffuse = max(dot(normal, normalize(toLight)), 0.0);
        lighting += lights.lightColors[i].rgb * diffuse * enabled * (1.0 / (1.0 + 0.08 * distanceToLight * distanceToLight));
    }

    vec4 albedo = texture(uTexture, vTexCoord) * vColor;
    fragColor = vec4(albedo.rgb * lighting, albedo.a);
}
)";

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

    Shader shader = LoadShaderFromMemory(kVertexShader, kFragmentShader);
    const int textureLocation = GetShaderLocation(shader, "uTexture");
    Texture2D albedo = GenCheckerTexture(256, 256, 32, Color{220, 230, 240, 255}, Color{65, 95, 165, 255});
    SetShaderValueTexture(shader, textureLocation, albedo);

    Mesh plane = GenMeshPlane(12.0f, 12.0f, 1, 1);
    Mesh cube = GenMeshCube(2.0f, 4.0f, 2.0f);
    UploadMesh(&plane, false);
    UploadMesh(&cube, false);

    Material material{};
    material.shader = &shader;
    material.maps = new MaterialMap[MATERIAL_MAP_BRDF + 1]{};
    material.maps[MATERIAL_MAP_ALBEDO].color = WHITE;
    material.maps[MATERIAL_MAP_ALBEDO].texture = albedo;

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
        BeginShaderMode(shader);
        DrawScene(plane, cube, material, true);
        EndShaderMode();
        const Vec3 lightPositions[4] = {
            {-3.0f, 4.0f, 3.0f}, {3.0f, 3.0f, 3.0f},
            {-3.0f, 3.0f, -3.0f}, {3.0f, 3.0f, -3.0f}
        };
        const Vec3 objectPositions[3] = {
            {0.0f, 0.0f, 0.0f}, {-2.6f, 0.0f, 0.5f}, {2.4f, 0.0f, -1.2f}
        };
        const float objectHeights[3] = {1.8f, 1.0f, 0.8f};
        const float objectRadii[3] = {0.95f, 0.60f, 0.50f};
        int activeLight = -1;
        for (int i = 0; i < 4; ++i) {
            if (lightEnabled[i]) {
                activeLight = i;
                break;
            }
        }
        if (activeLight >= 0) {
            for (int objectIndex = 0; objectIndex < 3; ++objectIndex) {
                const float height = objectHeights[objectIndex];

                Vec3 toLight = lightPositions[activeLight] - objectPositions[objectIndex];
                toLight.y = 0.0f;
                const float horizDist = std::sqrt(toLight.x * toLight.x + toLight.z * toLight.z);
                const Vec3 leanDir = (horizDist > 0.001f)
                    ? Vec3{-toLight.x / horizDist, 0.0f, -toLight.z / horizDist}
                    : Vec3{0.0f, 0.0f, 0.0f};

                const float leanAmount = std::min(height * 0.25f, objectRadii[objectIndex] * 0.6f);
                const Vec3 shadowPosition = objectPositions[objectIndex] + leanDir * leanAmount;
                const float shadowScale = objectRadii[objectIndex] * std::max(1.0f, 1.25f - 0.05f * height);

                const Vec3 shadowCenter{shadowPosition.x, 0.03f, shadowPosition.z};
                const Color shadowColor{18, 20, 28, 105};

                if (objectIndex == 0) {
                    const float side = shadowScale * 2.0f; // shadowScale ~ apothem
                    DrawCube(shadowCenter, side, 0.02f, side, shadowColor);
                } else {
                    DrawCylinder(shadowCenter, shadowScale, shadowScale, 0.025f, 32, shadowColor);
                }
            }
        }
        DrawGrid(12, 1.0f);
        EndMode3D();
        DrawText("Vulkan shadow maps  |  Y R G B: toggle lights", 16, 16, 20, WHITE);
        EndDrawing();

        Event event;
        while (PollEvent(event)) {}
    }

    UnloadMesh(plane);
    UnloadMesh(cube);
    delete[] material.maps;
    UnloadShader(shader);
    CloseWindow();
    return 0;
}
