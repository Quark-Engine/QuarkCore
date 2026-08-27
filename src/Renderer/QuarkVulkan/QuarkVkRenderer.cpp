#include "QuarkVkRenderer.hpp"
#include "../../QuarkInternal.hpp"

#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_vulkan.h>
#include <shaderc/shaderc.hpp>
#include <cstddef>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace qc {

static const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

static float NormalizeColorComponent(std::uint8_t value) {
    return static_cast<float>(value) / 255.0f;
}

static Mat4 MultiplyColumnMajor(const Mat4& left, const Mat4& right) {
    Mat4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result.m[column * 4 + row] = 0.0f;
            for (int index = 0; index < 4; ++index) {
                result.m[column * 4 + row] +=
                    left.m[index * 4 + row] * right.m[column * 4 + index];
            }
        }
    }
    return result;
}

static const char* kRuntime2DVertexShader = R"glsl(
#version 450
layout(push_constant) uniform ScreenData {
    vec2 screenSize;
} screen;
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec4 vColor;
void main() {
    vec2 ndc = (aPosition / screen.screenSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, ndc.y, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
}
)glsl";

static const char* kRuntime2DFragmentShader = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D texture0;
layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = texture(texture0, vTexCoord) * vColor;
}
)glsl";

static const char* kRuntime3DVertexShader = R"glsl(
#version 450
layout(location = 0) in vec4 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aNormal;
layout(location = 4) in vec4 aWorldPosition;
layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec3 vNormal;
layout(location = 3) out vec3 vWorldPosition;
void main() {
    gl_Position = aPosition;
    vTexCoord = aTexCoord;
    vColor = aColor;
    vNormal = normalize(aNormal.xyz);
    vWorldPosition = aWorldPosition.xyz;
}
)glsl";

static const char* kRuntime3DFragmentShader = R"glsl(
#version 450
layout(set = 0, binding = 1) uniform sampler2D albedoMap;
layout(set = 0, binding = 2) uniform sampler2D shadowMaps[4];
layout(set = 0, binding = 3) uniform ShadowData { mat4 lightViewProjection; } shadowData;
layout(set = 0, binding = 5) uniform sampler2D metalnessMap;
layout(set = 0, binding = 6) uniform sampler2D normalMap;
layout(set = 0, binding = 7) uniform sampler2D roughnessMap;
layout(set = 0, binding = 8) uniform sampler2D occlusionMap;
layout(set = 0, binding = 9) uniform sampler2D emissionMap;
layout(push_constant) uniform Lighting {
    vec4 lightPositions[4];
    vec4 lightColors[4];
    vec4 timeData;
    vec4 lightEnabled;
} lighting;
layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec3 vNormal;
layout(location = 3) in vec3 vWorldPosition;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float ShadowFactor(vec3 worldPosition, vec3 normal, vec3 lightDirection) {
    vec4 shadowPosition = shadowData.lightViewProjection * vec4(worldPosition, 1.0);
    shadowPosition.xyz /= max(shadowPosition.w, 0.0001);
    vec2 shadowUv = shadowPosition.xy * 0.5 + 0.5;
    if (shadowPosition.z <= 0.0 || shadowPosition.z >= 1.0 ||
        any(lessThan(shadowUv, vec2(0.0))) || any(greaterThan(shadowUv, vec2(1.0)))) {
        return 1.0;
    }
    float bias = max(0.0015 * (1.0 - dot(normal, lightDirection)), 0.0005);
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMaps[0], 0));
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float depth = texture(shadowMaps[0], shadowUv + vec2(x, y) * texelSize).r;
            lit += shadowPosition.z - bias <= depth ? 1.0 : 0.0;
        }
    }
    return lit / 9.0;
}

float DistributionGGX(vec3 normal, vec3 halfway, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float normalDotHalfway = max(dot(normal, halfway), 0.0);
    float denominator = normalDotHalfway * normalDotHalfway * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(PI * denominator * denominator, 0.0001);
}

float GeometrySchlickGGX(float normalDotDirection, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return normalDotDirection / max(normalDotDirection * (1.0 - k) + k, 0.0001);
}

float GeometrySmith(vec3 normal, vec3 viewDirection, vec3 lightDirection, float roughness) {
    return GeometrySchlickGGX(max(dot(normal, viewDirection), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(normal, lightDirection), 0.0), roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 baseReflectivity) {
    return baseReflectivity + (1.0 - baseReflectivity) * pow(1.0 - cosTheta, 5.0);
}

float LightEnabled(int index) {
    if (index == 0) return lighting.lightEnabled.x;
    if (index == 1) return lighting.lightEnabled.y;
    if (index == 2) return lighting.lightEnabled.z;
    return lighting.lightEnabled.w;
}

void main() {
    vec4 albedoSample = texture(albedoMap, vTexCoord) * vColor;
    vec3 albedo = pow(max(albedoSample.rgb, vec3(0.0)), vec3(2.2));
    float metalness = clamp(texture(metalnessMap, vTexCoord).r, 0.0, 1.0);
    float roughness = clamp(texture(roughnessMap, vTexCoord).r, 0.045, 1.0);
    float occlusion = clamp(texture(occlusionMap, vTexCoord).r, 0.0, 1.0);
    vec3 emission = pow(max(texture(emissionMap, vTexCoord).rgb, vec3(0.0)), vec3(2.2));

    vec3 normal = normalize(vNormal);
    vec3 tangent = normalize(dFdx(vWorldPosition) * dFdy(vTexCoord).y -
                             dFdy(vWorldPosition) * dFdx(vTexCoord).y);
    tangent = normalize(tangent - normal * dot(normal, tangent));
    vec3 bitangent = normalize(cross(normal, tangent));
    vec3 tangentNormal = texture(normalMap, vTexCoord).xyz * 2.0 - 1.0;
    normal = normalize(mat3(tangent, bitangent, normal) * tangentNormal);

    vec3 viewDirection = normalize(vec3(0.0, 5.0, 10.0) - vWorldPosition);
    vec3 baseReflectivity = mix(vec3(0.04), albedo, metalness);
    vec3 lighting = vec3(0.03) * albedo * occlusion;
    const vec3 lightPositions[4] = vec3[4](
        vec3(-3.0, 4.0, 3.0), vec3(3.0, 3.0, 3.0),
        vec3(-3.0, 3.0, -3.0), vec3(3.0, 3.0, -3.0));
    const vec3 lightColors[4] = vec3[4](
        vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0),
        vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0));

    for (int i = 0; i < 4; ++i) {
        if (LightEnabled(i) <= 0.0) continue;
        vec3 toLight = lightPositions[i] - vWorldPosition;
        float distanceToLight = length(toLight);
        vec3 lightDirection = toLight / max(distanceToLight, 0.0001);
        vec3 halfway = normalize(viewDirection + lightDirection);
        float normalDotLight = max(dot(normal, lightDirection), 0.0);
        float normalDotView = max(dot(normal, viewDirection), 0.0);
        if (normalDotLight <= 0.0 || normalDotView <= 0.0) continue;
        float distribution = DistributionGGX(normal, halfway, roughness);
        float geometry = GeometrySmith(normal, viewDirection, lightDirection, roughness);
        vec3 fresnel = FresnelSchlick(max(dot(halfway, viewDirection), 0.0), baseReflectivity);
        vec3 specular = (distribution * geometry * fresnel) /
                        max(4.0 * normalDotView * normalDotLight, 0.0001);
        vec3 diffuse = (1.0 - fresnel) * (1.0 - metalness) * albedo / PI;
        float attenuation = 1.0 / (1.0 + 0.08 * distanceToLight * distanceToLight);
        float shadow = i == 0 ? ShadowFactor(vWorldPosition, normal, lightDirection) : 1.0;
        lighting += (diffuse + specular) * lightColors[i] * normalDotLight * attenuation * shadow;
    }

    vec3 color = lighting + emission;
    color = color / (color + vec3(1.0));
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(color, albedoSample.a);
}
)glsl";

static std::vector<uint32_t> CompileRuntimeShader(const char* source,
                                                  shaderc_shader_kind kind,
                                                  const char* stageName) {
    static std::unordered_map<std::string, std::vector<uint32_t>> cache;
    const std::string sourceText = source ? source : "";
    const std::string cacheKey = std::string(stageName) + "\n" + sourceText;
    const auto cached = cache.find(cacheKey);
    if (cached != cache.end()) {
        return cached->second;
    }

    const auto hashText = [](const std::string& text) {
        uint64_t hash = 14695981039346656037ull;
        for (unsigned char character : text) {
            hash ^= character;
            hash *= 1099511628211ull;
        }
        return hash;
    };
    const auto cacheFile = [&]() -> std::filesystem::path {
        std::error_code error;
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path(error) / "QuarkCore" / "shader-cache";
        if (error) {
            return {};
        }
        return directory / (std::to_string(hashText(cacheKey)) + ".spv");
    };
    const std::filesystem::path path = cacheFile();
    if (!path.empty()) {
        std::ifstream file(path, std::ios::binary);
        uint32_t wordCount = 0;
        if (file && file.read(reinterpret_cast<char*>(&wordCount), sizeof(wordCount)) && wordCount > 0) {
            std::vector<uint32_t> spirv(wordCount);
            if (file.read(reinterpret_cast<char*>(spirv.data()), static_cast<std::streamsize>(spirv.size() * sizeof(uint32_t)))) {
                cache.emplace(cacheKey, spirv);
                return spirv;
            }
        }
    }

    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    const auto result = compiler.CompileGlslToSpv(source, kind, stageName, options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        throw std::runtime_error(std::string("Failed to compile runtime Vulkan ") + stageName + ": " + result.GetErrorMessage());
    }
    std::vector<uint32_t> spirv(result.cbegin(), result.cend());
    if (!path.empty()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        const uint32_t wordCount = static_cast<uint32_t>(spirv.size());
        if (file && file.write(reinterpret_cast<const char*>(&wordCount), sizeof(wordCount))) {
            file.write(reinterpret_cast<const char*>(spirv.data()),
                       static_cast<std::streamsize>(spirv.size() * sizeof(uint32_t)));
        }
    }
    cache.emplace(cacheKey, spirv);
    return spirv;
}

static const std::array<uint32_t, 367> kQuadVertSpv = {
    0x07230203,
    0x00010500,
    0x0008000B,
    0x00000031,
    0x00000000,
    0x00020011,
    0x00000001,
    0x0006000B,
    0x00000001,
    0x4C534C47,
    0x6474732E,
    0x3035342E,
    0x00000000,
    0x0003000E,
    0x00000000,
    0x00000001,
    0x000C000F,
    0x00000000,
    0x00000004,
    0x6E69616D,
    0x00000000,
    0x0000000B,
    0x0000000F,
    0x00000021,
    0x0000002A,
    0x0000002B,
    0x0000002D,
    0x0000002F,
    0x00030003,
    0x00000002,
    0x000001C2,
    0x00040005,
    0x00000004,
    0x6E69616D,
    0x00000000,
    0x00030005,
    0x00000009,
    0x0063646E,
    0x00040005,
    0x0000000B,
    0x736F5061,
    0x00000000,
    0x00060005,
    0x0000000D,
    0x68737550,
    0x736E6F43,
    0x746E6174,
    0x00000073,
    0x00060006,
    0x0000000D,
    0x00000000,
    0x65726373,
    0x69536E65,
    0x0000657A,
    0x00030005,
    0x0000000F,
    0x00006370,
    0x00060005,
    0x0000001F,
    0x505F6C67,
    0x65567265,
    0x78657472,
    0x00000000,
    0x00060006,
    0x0000001F,
    0x00000000,
    0x505F6C67,
    0x7469736F,
    0x006E6F69,
    0x00070006,
    0x0000001F,
    0x00000001,
    0x505F6C67,
    0x746E696F,
    0x657A6953,
    0x00000000,
    0x00070006,
    0x0000001F,
    0x00000002,
    0x435F6C67,
    0x4470696C,
    0x61747369,
    0x0065636E,
    0x00070006,
    0x0000001F,
    0x00000003,
    0x435F6C67,
    0x446C6C75,
    0x61747369,
    0x0065636E,
    0x00030005,
    0x00000021,
    0x00000000,
    0x00030005,
    0x0000002A,
    0x00565576,
    0x00030005,
    0x0000002B,
    0x00565561,
    0x00040005,
    0x0000002D,
    0x6C6F4376,
    0x0000726F,
    0x00040005,
    0x0000002F,
    0x6C6F4361,
    0x0000726F,
    0x00040047,
    0x0000000B,
    0x0000001E,
    0x00000000,
    0x00030047,
    0x0000000D,
    0x00000002,
    0x00050048,
    0x0000000D,
    0x00000000,
    0x00000023,
    0x00000000,
    0x00030047,
    0x0000001F,
    0x00000002,
    0x00050048,
    0x0000001F,
    0x00000000,
    0x0000000B,
    0x00000000,
    0x00050048,
    0x0000001F,
    0x00000001,
    0x0000000B,
    0x00000001,
    0x00050048,
    0x0000001F,
    0x00000002,
    0x0000000B,
    0x00000003,
    0x00050048,
    0x0000001F,
    0x00000003,
    0x0000000B,
    0x00000004,
    0x00040047,
    0x0000002A,
    0x0000001E,
    0x00000000,
    0x00040047,
    0x0000002B,
    0x0000001E,
    0x00000001,
    0x00040047,
    0x0000002D,
    0x0000001E,
    0x00000001,
    0x00040047,
    0x0000002F,
    0x0000001E,
    0x00000002,
    0x00020013,
    0x00000002,
    0x00030021,
    0x00000003,
    0x00000002,
    0x00030016,
    0x00000006,
    0x00000020,
    0x00040017,
    0x00000007,
    0x00000006,
    0x00000002,
    0x00040020,
    0x00000008,
    0x00000007,
    0x00000007,
    0x00040020,
    0x0000000A,
    0x00000001,
    0x00000007,
    0x0004003B,
    0x0000000A,
    0x0000000B,
    0x00000001,
    0x0003001E,
    0x0000000D,
    0x00000007,
    0x00040020,
    0x0000000E,
    0x00000009,
    0x0000000D,
    0x0004003B,
    0x0000000E,
    0x0000000F,
    0x00000009,
    0x00040015,
    0x00000010,
    0x00000020,
    0x00000001,
    0x0004002B,
    0x00000010,
    0x00000011,
    0x00000000,
    0x00040020,
    0x00000012,
    0x00000009,
    0x00000007,
    0x0004002B,
    0x00000006,
    0x00000016,
    0x40000000,
    0x0004002B,
    0x00000006,
    0x00000018,
    0x3F800000,
    0x00040017,
    0x0000001B,
    0x00000006,
    0x00000004,
    0x00040015,
    0x0000001C,
    0x00000020,
    0x00000000,
    0x0004002B,
    0x0000001C,
    0x0000001D,
    0x00000001,
    0x0004001C,
    0x0000001E,
    0x00000006,
    0x0000001D,
    0x0006001E,
    0x0000001F,
    0x0000001B,
    0x00000006,
    0x0000001E,
    0x0000001E,
    0x00040020,
    0x00000020,
    0x00000003,
    0x0000001F,
    0x0004003B,
    0x00000020,
    0x00000021,
    0x00000003,
    0x0004002B,
    0x00000006,
    0x00000023,
    0x00000000,
    0x00040020,
    0x00000027,
    0x00000003,
    0x0000001B,
    0x00040020,
    0x00000029,
    0x00000003,
    0x00000007,
    0x0004003B,
    0x00000029,
    0x0000002A,
    0x00000003,
    0x0004003B,
    0x0000000A,
    0x0000002B,
    0x00000001,
    0x0004003B,
    0x00000027,
    0x0000002D,
    0x00000003,
    0x00040020,
    0x0000002E,
    0x00000001,
    0x0000001B,
    0x0004003B,
    0x0000002E,
    0x0000002F,
    0x00000001,
    0x00050036,
    0x00000002,
    0x00000004,
    0x00000000,
    0x00000003,
    0x000200F8,
    0x00000005,
    0x0004003B,
    0x00000008,
    0x00000009,
    0x00000007,
    0x0004003D,
    0x00000007,
    0x0000000C,
    0x0000000B,
    0x00050041,
    0x00000012,
    0x00000013,
    0x0000000F,
    0x00000011,
    0x0004003D,
    0x00000007,
    0x00000014,
    0x00000013,
    0x00050088,
    0x00000007,
    0x00000015,
    0x0000000C,
    0x00000014,
    0x0005008E,
    0x00000007,
    0x00000017,
    0x00000015,
    0x00000016,
    0x00050050,
    0x00000007,
    0x00000019,
    0x00000018,
    0x00000018,
    0x00050083,
    0x00000007,
    0x0000001A,
    0x00000017,
    0x00000019,
    0x0003003E,
    0x00000009,
    0x0000001A,
    0x0004003D,
    0x00000007,
    0x00000022,
    0x00000009,
    0x00050051,
    0x00000006,
    0x00000024,
    0x00000022,
    0x00000000,
    0x00050051,
    0x00000006,
    0x00000025,
    0x00000022,
    0x00000001,
    0x00070050,
    0x0000001B,
    0x00000026,
    0x00000024,
    0x00000025,
    0x00000023,
    0x00000018,
    0x00050041,
    0x00000027,
    0x00000028,
    0x00000021,
    0x00000011,
    0x0003003E,
    0x00000028,
    0x00000026,
    0x0004003D,
    0x00000007,
    0x0000002C,
    0x0000002B,
    0x0003003E,
    0x0000002A,
    0x0000002C,
    0x0004003D,
    0x0000001B,
    0x00000030,
    0x0000002F,
    0x0003003E,
    0x0000002D,
    0x00000030,
    0x000100FD,
    0x00010038
};

static const std::array<uint32_t, 166> kQuadFragSpv = {
    0x07230203, 0x00010500, 0x0008000B, 0x00000018, 0x00000000, 0x00020011, 0x00000001, 0x0006000B,
    0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001,
    0x0009000F, 0x00000004, 0x00000004, 0x6E69616D, 0x00000000, 0x00000009, 0x0000000D, 0x00000011,
    0x00000015, 0x00030010, 0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001C2, 0x00040005,
    0x00000004, 0x6E69616D, 0x00000000, 0x00050005, 0x00000009, 0x4374756F, 0x726F6C6F, 0x00000000,
    0x00050005, 0x0000000D, 0x78655475, 0x65727574, 0x00000000, 0x00030005, 0x00000011, 0x00565576,
    0x00040005, 0x00000015, 0x6C6F4376, 0x0000726F, 0x00040047, 0x00000009, 0x0000001E, 0x00000000,
    0x00040047, 0x0000000D, 0x00000021, 0x00000000, 0x00040047, 0x0000000D, 0x00000022, 0x00000000,
    0x00040047, 0x00000011, 0x0000001E, 0x00000000, 0x00040047, 0x00000015, 0x0000001E, 0x00000001,
    0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020,
    0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007,
    0x0004003B, 0x00000008, 0x00000009, 0x00000003, 0x00090019, 0x0000000A, 0x00000006, 0x00000001,
    0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x0003001B, 0x0000000B, 0x0000000A,
    0x00040020, 0x0000000C, 0x00000000, 0x0000000B, 0x0004003B, 0x0000000C, 0x0000000D, 0x00000000,
    0x00040017, 0x0000000F, 0x00000006, 0x00000002, 0x00040020, 0x00000010, 0x00000001, 0x0000000F,
    0x0004003B, 0x00000010, 0x00000011, 0x00000001, 0x00040020, 0x00000014, 0x00000001, 0x00000007,
    0x0004003B, 0x00000014, 0x00000015, 0x00000001, 0x00050036, 0x00000002, 0x00000004, 0x00000000,
    0x00000003, 0x000200F8, 0x00000005, 0x0004003D, 0x0000000B, 0x0000000E, 0x0000000D, 0x0004003D,
    0x0000000F, 0x00000012, 0x00000011, 0x00050057, 0x00000007, 0x00000013, 0x0000000E, 0x00000012,
    0x0004003D, 0x00000007, 0x00000016, 0x00000015, 0x00050085, 0x00000007, 0x00000017, 0x00000013,
    0x00000016, 0x0003003E, 0x00000009, 0x00000017, 0x000100FD, 0x00010038
};

namespace {

static const std::array<uint32_t, 257> kVk3DVertSpv = {
    0x07230203, 0x00010000, 0x0008000B, 0x0000001E, 0x00000000, 0x00020011, 0x00000001, 0x0006000B,
    0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001,
    0x000B000F, 0x00000000, 0x00000004, 0x6E69616D, 0x00000000, 0x0000000D, 0x00000011, 0x00000017,
    0x00000019, 0x0000001B, 0x0000001C, 0x00030003, 0x00000002, 0x000001C2, 0x00040005, 0x00000004,
    0x6E69616D, 0x00000000, 0x00060005, 0x0000000B, 0x505F6C67, 0x65567265, 0x78657472, 0x00000000,
    0x00060006, 0x0000000B, 0x00000000, 0x505F6C67, 0x7469736F, 0x006E6F69, 0x00070006, 0x0000000B,
    0x00000001, 0x505F6C67, 0x746E696F, 0x657A6953, 0x00000000, 0x00070006, 0x0000000B, 0x00000002,
    0x435F6C67, 0x4470696C, 0x61747369, 0x0065636E, 0x00070006, 0x0000000B, 0x00000003, 0x435F6C67,
    0x446C6C75, 0x61747369, 0x0065636E, 0x00030005, 0x0000000D, 0x00000000, 0x00050005, 0x00000011,
    0x736F5061, 0x6F697469, 0x0000006E, 0x00050005, 0x00000017, 0x78655476, 0x726F6F43, 0x00000064,
    0x00050005, 0x00000019, 0x78655461, 0x726F6F43, 0x00000064, 0x00040005, 0x0000001B, 0x6C6F4376,
    0x0000726F, 0x00040005, 0x0000001C, 0x6C6F4361, 0x0000726F, 0x00030047, 0x0000000B, 0x00000002,
    0x00050048, 0x0000000B, 0x00000000, 0x0000000B, 0x00000000, 0x00050048, 0x0000000B, 0x00000001,
    0x0000000B, 0x00000001, 0x00050048, 0x0000000B, 0x00000002, 0x0000000B, 0x00000003, 0x00050048,
    0x0000000B, 0x00000003, 0x0000000B, 0x00000004, 0x00040047, 0x00000011, 0x0000001E, 0x00000000,
    0x00040047, 0x00000017, 0x0000001E, 0x00000000, 0x00040047, 0x00000019, 0x0000001E, 0x00000001,
    0x00040047, 0x0000001B, 0x0000001E, 0x00000001, 0x00040047, 0x0000001C, 0x0000001E, 0x00000002,
    0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020,
    0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040015, 0x00000008, 0x00000020, 0x00000000,
    0x0004002B, 0x00000008, 0x00000009, 0x00000001, 0x0004001C, 0x0000000A, 0x00000006, 0x00000009,
    0x0006001E, 0x0000000B, 0x00000007, 0x00000006, 0x0000000A, 0x0000000A, 0x00040020, 0x0000000C,
    0x00000003, 0x0000000B, 0x0004003B, 0x0000000C, 0x0000000D, 0x00000003, 0x00040015, 0x0000000E,
    0x00000020, 0x00000001, 0x0004002B, 0x0000000E, 0x0000000F, 0x00000000, 0x00040020, 0x00000010,
    0x00000001, 0x00000007, 0x0004003B, 0x00000010, 0x00000011, 0x00000001, 0x00040020, 0x00000013,
    0x00000003, 0x00000007, 0x00040017, 0x00000015, 0x00000006, 0x00000002, 0x00040020, 0x00000016,
    0x00000003, 0x00000015, 0x0004003B, 0x00000016, 0x00000017, 0x00000003, 0x00040020, 0x00000018,
    0x00000001, 0x00000015, 0x0004003B, 0x00000018, 0x00000019, 0x00000001, 0x0004003B, 0x00000013,
    0x0000001B, 0x00000003, 0x0004003B, 0x00000010, 0x0000001C, 0x00000001, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200F8, 0x00000005, 0x0004003D, 0x00000007, 0x00000012,
    0x00000011, 0x00050041, 0x00000013, 0x00000014, 0x0000000D, 0x0000000F, 0x0003003E, 0x00000014,
    0x00000012, 0x0004003D, 0x00000015, 0x0000001A, 0x00000019, 0x0003003E, 0x00000017, 0x0000001A,
    0x0004003D, 0x00000007, 0x0000001D, 0x0000001C, 0x0003003E, 0x0000001B, 0x0000001D, 0x000100FD,
    0x00010038
};

static const std::array<uint32_t, 167> kVk3DFragSpv = {
    0x07230203, 0x00010000, 0x0008000B, 0x00000018, 0x00000000, 0x00020011, 0x00000001, 0x0006000B,
    0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001,
    0x0008000F, 0x00000004, 0x00000004, 0x6E69616D, 0x00000000, 0x00000009, 0x00000011, 0x00000015,
    0x00030010, 0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001C2, 0x00040005, 0x00000004,
    0x6E69616D, 0x00000000, 0x00050005, 0x00000009, 0x67617246, 0x6F6C6F43, 0x00000072, 0x00050005,
    0x0000000D, 0x78655475, 0x65727574, 0x00000000, 0x00050005, 0x00000011, 0x78655476, 0x726F6F43,
    0x00000064, 0x00040005, 0x00000015, 0x6C6F4376, 0x0000726F, 0x00040047, 0x00000009, 0x0000001E,
    0x00000000, 0x00040047, 0x0000000D, 0x00000021, 0x00000000, 0x00040047, 0x0000000D, 0x00000022,
    0x00000000, 0x00040047, 0x00000011, 0x0000001E, 0x00000000, 0x00040047, 0x00000015, 0x0000001E,
    0x00000001, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006,
    0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008, 0x00000003,
    0x00000007, 0x0004003B, 0x00000008, 0x00000009, 0x00000003, 0x00090019, 0x0000000A, 0x00000006,
    0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x0003001B, 0x0000000B,
    0x0000000A, 0x00040020, 0x0000000C, 0x00000000, 0x0000000B, 0x0004003B, 0x0000000C, 0x0000000D,
    0x00000000, 0x00040017, 0x0000000F, 0x00000006, 0x00000002, 0x00040020, 0x00000010, 0x00000001,
    0x0000000F, 0x0004003B, 0x00000010, 0x00000011, 0x00000001, 0x00040020, 0x00000014, 0x00000001,
    0x00000007, 0x0004003B, 0x00000014, 0x00000015, 0x00000001, 0x00050036, 0x00000002, 0x00000004,
    0x00000000, 0x00000003, 0x000200F8, 0x00000005, 0x0004003D, 0x0000000B, 0x0000000E, 0x0000000D,
    0x0004003D, 0x0000000F, 0x00000012, 0x00000011, 0x00050057, 0x00000007, 0x00000013, 0x0000000E,
    0x00000012, 0x0004003D, 0x00000007, 0x00000016, 0x00000015, 0x00050085, 0x00000007, 0x00000017,
    0x00000013, 0x00000016, 0x0003003E, 0x00000009, 0x00000017, 0x000100FD, 0x00010038
};

bool HasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

const char* GetVulkanVendorName(uint32_t vendorID) {
    switch (vendorID) {
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        case 0x13B5: return "ARM";
        case 0x5143: return "Qualcomm";
        case 0x1010: return "ImgTec";
        case 0x106B: return "Apple";
        case 0x1414: return "Microsoft";
        case 0x10005: return "Mesa";
        default:     return "Unknown";
    }
}

const char* GetVulkanDeviceTypeString(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "Discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "Virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU (Software)";
        default:                                     return "Other";
    }
}

std::string FormatVulkanDriverVersion(uint32_t vendorID, uint32_t driverVersion) {
    if (vendorID == 0x10DE) { // NVIDIA
        return TextFormat("%u.%u.%u.%u",
            (driverVersion >> 22) & 0x3FF,
            (driverVersion >> 14) & 0x0FF,
            (driverVersion >> 6)  & 0x0FF,
            (driverVersion)       & 0x03F);
    }
#if defined(_WIN32)
    if (vendorID == 0x8086) { // Intel
        return TextFormat("%u.%u", (driverVersion >> 14), (driverVersion & 0x3FFF));
    }
#endif
    return TextFormat("%u.%u.%u",
        VK_VERSION_MAJOR(driverVersion),
        VK_VERSION_MINOR(driverVersion),
        VK_VERSION_PATCH(driverVersion));
}

const char* GetPresentModeString(VkPresentModeKHR mode) {
    switch (mode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:    return "IMMEDIATE (VSync OFF, uncapped)";
        case VK_PRESENT_MODE_MAILBOX_KHR:      return "MAILBOX (Triple Buffering)";
        case VK_PRESENT_MODE_FIFO_KHR:         return "FIFO (VSync ON)";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED (Adaptive VSync)";
        default:                               return "Unknown";
    }
}

const char* GetVkFormatString(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case VK_FORMAT_B8G8R8A8_SRGB:  return "B8G8R8A8_SRGB";
        case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_SRGB:  return "R8G8B8A8_SRGB";
        case VK_FORMAT_D32_SFLOAT:     return "D32_SFLOAT";
        case VK_FORMAT_D32_SFLOAT_S8_UINT: return "D32_SFLOAT_S8_UINT";
        case VK_FORMAT_D24_UNORM_S8_UINT:  return "D24_UNORM_S8_UINT";
        default:                       return "VkFormat(Other)";
    }
}

} // namespace

QuarkVkRenderer::~QuarkVkRenderer() {
    Shutdown();
}

void QuarkVkRenderer::Init(SDL_Window* window, int width, int height) {
    uint32_t version = 0;
    vkEnumerateInstanceVersion(&version);

    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Initializing Vulkan renderer... (Supported Instance API: %d.%d.%d, Window: %dx%d)",
        VK_VERSION_MAJOR(version),
        VK_VERSION_MINOR(version),
        VK_VERSION_PATCH(version),
        width, height));

#ifdef __APPLE__
    TraceLog(LogLevel::Info, "VULKAN", "Apple platform detected, using MoltenVK...");
#endif

    m_window   = window;
    m_width    = width;
    m_height   = height;
    m_framebufferResized = false;

    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateInstance");
    CreateInstance();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateSurface");
    CreateSurface();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: PickPhysicalDevice");
    PickPhysicalDevice();
    m_msaaSamples = GetSampleCountForSamples(m_requestedMsaaSamples);
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateLogicalDevice");
    CreateLogicalDevice();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateSwapChain");
    CreateSwapChain();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateImageViews");
    CreateImageViews();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateRenderPass");
    CreateRenderPass();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateOffscreenRenderPass");
    CreateOffscreenRenderPass();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateDescriptorSetLayout");
    CreateDescriptorSetLayout();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateShadowResources");
    CreateShadowResources();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateShadowPipeline");
    CreateShadowPipeline();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreatePipeline2D");
    CreatePipeline2D();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateOffscreenPipeline2D");
    CreateOffscreenPipeline2D();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateShaderPipelines");
    CreateShaderPipelines();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreatePipeline3D");
    CreatePipeline3D();
    CreateFramebuffers();
    CreateCommandPool();
    CreateCommandBuffers();
    CreateFrameVertexIndexBuffers();
    m_batchVertices.reserve(kVkMaxVerticesPerFrame);
    m_batchIndices.reserve(kVkMaxIndicesPerFrame);
    m_batchDrawItems.reserve(256);
    m_frameVertices.reserve(kVkMaxVerticesPerFrame);
    m_frameIndices.reserve(kVkMaxIndicesPerFrame);
    m_frameDrawItems.reserve(256);
    m_main3DBatch.triangleVertices.reserve(kVkMaxVerticesPerFrame / 2);
    m_main3DBatch.lineVertices.reserve(kVkMaxVerticesPerFrame / 2);
    m_frameTriangleVertices3D.reserve(kVkMaxVerticesPerFrame);
    m_frameLineVertices3D.reserve(kVkMaxVerticesPerFrame);
    CreateSyncObjects();
    CreateWhiteTexture();

    TraceLog(LogLevel::Info, "VULKAN", "Vulkan renderer initialized successfully.");
}

void QuarkVkRenderer::Shutdown() {
    if (m_instance == VK_NULL_HANDLE) {
        return;
    }

    TraceLog(LogLevel::Info, "VULKAN", "Shutting down Vulkan renderer...");

    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }

    DestroyShadowResources();

    {
        std::vector<uint32_t> ids;
        ids.reserve(m_renderTargets.size());
        for (const auto& [id, _] : m_renderTargets) { ids.push_back(id); }
        for (uint32_t id : ids) { DestroyRenderTargetInternal(id); }
    }

    {
        std::vector<uint32_t> ids;
        ids.reserve(m_textures.size());
        for (const auto& [id, _] : m_textures) { ids.push_back(id); }
        for (uint32_t id : ids) { DestroyTexture(id); }
    }

    CleanupSwapChain();

    for (auto& frame : m_frames) {
        if (frame.imageAvailable != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, frame.imageAvailable, nullptr);
            frame.imageAvailable = VK_NULL_HANDLE;
        }
        if (frame.renderFinished != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, frame.renderFinished, nullptr);
            frame.renderFinished = VK_NULL_HANDLE;
        }
        if (frame.inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, frame.inFlightFence, nullptr);
            frame.inFlightFence = VK_NULL_HANDLE;
        }

        if (frame.vertexMapped && frame.vertexMemory != VK_NULL_HANDLE) {
            vkUnmapMemory(m_device, frame.vertexMemory);
            frame.vertexMapped = nullptr;
        }
        if (frame.vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, frame.vertexBuffer, nullptr);
            frame.vertexBuffer = VK_NULL_HANDLE;
        }
        if (frame.vertexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, frame.vertexMemory, nullptr);
            frame.vertexMemory = VK_NULL_HANDLE;
        }
        frame.vertexCapacity = 0;
        if (frame.indexMapped && frame.indexMemory != VK_NULL_HANDLE) {
            vkUnmapMemory(m_device, frame.indexMemory);
            frame.indexMapped = nullptr;
        }
        if (frame.indexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, frame.indexBuffer, nullptr);
            frame.indexBuffer = VK_NULL_HANDLE;
        }
        if (frame.indexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, frame.indexMemory, nullptr);
            frame.indexMemory = VK_NULL_HANDLE;
        }
        frame.indexCapacity = 0;
        if (frame.vertexMapped3D && frame.vertexMemory3D != VK_NULL_HANDLE) {
            vkUnmapMemory(m_device, frame.vertexMemory3D);
            frame.vertexMapped3D = nullptr;
        }
        if (frame.vertexBuffer3D != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, frame.vertexBuffer3D, nullptr);
            frame.vertexBuffer3D = VK_NULL_HANDLE;
        }
        if (frame.vertexMemory3D != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, frame.vertexMemory3D, nullptr);
            frame.vertexMemory3D = VK_NULL_HANDLE;
        }
        frame.vertexCapacity3D = 0;
    }

    for (VkDescriptorPool pool : m_descriptorPools) {
        if (pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, pool, nullptr);
        }
    }
    m_descriptorPools.clear();

    if (m_3DDummyBuffer != VK_NULL_HANDLE) {
        if (m_3DDummyMapped != nullptr) {
            vkUnmapMemory(m_device, m_3DDummyMemory);
            m_3DDummyMapped = nullptr;
        }
        vkDestroyBuffer(m_device, m_3DDummyBuffer, nullptr);
        m_3DDummyBuffer = VK_NULL_HANDLE;
    }
    if (m_3DDummyMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_3DDummyMemory, nullptr);
        m_3DDummyMemory = VK_NULL_HANDLE;
    }

    if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
    }

    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_descriptorSetLayout3D != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout3D, nullptr);
        m_descriptorSetLayout3D = VK_NULL_HANDLE;
    }

    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }

    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }

    m_window             = nullptr;
    m_width              = 0;
    m_height             = 0;
    m_drawing            = false;
    m_currentFrame       = 0;
    m_imageIndex         = 0;
    m_whiteTextureId     = 0;
    m_nextTextureId      = 1;
    m_nextRenderTargetId = 1;
    m_activeRenderTargetId = 0;
    m_graphicsQueueFamily = UINT32_MAX;
    m_swapChainMinImageCount = 0;
    DestroyMSAAColorResources();
    m_msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    m_requestedMsaaSamples = 1;
    m_main3DBatch.triangleVertices.clear();
    m_main3DBatch.lineVertices.clear();
    m_frameTriangleVertices3D.clear();
    m_frameLineVertices3D.clear();
    m_frame3DDrawItems.clear();
    TraceLog(LogLevel::Info, "VULKAN", "Vulkan renderer shut down successfully.");
}

void QuarkVkRenderer::BeginDrawing() {
    if (!m_device || !m_swapChain || m_drawing) {
        return;
    }

    m_activeRenderTargetId = 0;
    if (m_lastFrameCounter == 0) {
        m_lastFrameCounter = SDL_GetPerformanceCounter();
    }

    VkFrameData& frame = m_frames[m_currentFrame];
    vkWaitForFences(m_device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        m_device, m_swapChain, UINT64_MAX,
        frame.imageAvailable, VK_NULL_HANDLE, &m_imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapChain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire next Vulkan swapchain image.");
    }

    m_drawing = true;
}

void QuarkVkRenderer::EndDrawing() {
    if (!m_drawing || !m_device || !m_swapChain) return;

    VkFrameData& frame = m_frames[m_currentFrame];
    VkCommandBuffer cmd = frame.commandBuffer;

    vkResetFences(m_device, 1, &frame.inFlightFence);
    vkResetCommandBuffer(cmd, 0);

    BuildCombinedFrameGeometry();

    m_batchVertices.clear();
    m_batchIndices.clear();
    m_batchDrawItems.clear();
    for (auto& [id, rt] : m_renderTargets) {
        (void)id;
        rt.vertices.clear();
        rt.indices.clear();
        rt.drawItems.clear();
        rt.triangleVertices3D.clear();
        rt.lineVertices3D.clear();
        rt.drawItems3D.clear();
    }
    m_main3DBatch.triangleVertices.clear();
    m_main3DBatch.lineVertices.clear();
    m_main3DBatch.drawItems.clear();

    if (!UploadFrameGeometry(m_currentFrame)) {
        m_drawing = false;
        return;
    }
    if (!RecordCommandBuffer(cmd, m_imageIndex)) {
        m_drawing = false;
        return;
    }

    VkSemaphore waitSemaphores[]   = { frame.imageAvailable };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { frame.renderFinished };

    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = waitSemaphores;
    submitInfo.pWaitDstStageMask    = waitStages;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = signalSemaphores;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, frame.inFlightFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit Vulkan command buffer.");
    }

    const std::uint64_t freq = SDL_GetPerformanceFrequency();
    if (m_targetFps > 0) {
        const std::uint64_t targetTicks = freq / static_cast<std::uint64_t>(m_targetFps);
        while (true) {
            const std::uint64_t now = SDL_GetPerformanceCounter();
            const std::uint64_t elapsed = now - m_lastFrameCounter;
            if (elapsed >= targetTicks) {
                break;
            }
            const std::uint64_t remaining = targetTicks - elapsed;
            if (remaining > freq / 500) {
                SDL_Delay(1);
            }
        }
    }

    const std::uint64_t frameEnd = SDL_GetPerformanceCounter();
    m_frameTime = static_cast<float>(frameEnd - m_lastFrameCounter) / static_cast<float>(freq);
    m_lastFrameCounter = frameEnd;

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = signalSemaphores;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &m_swapChain;
    presentInfo.pImageIndices      = &m_imageIndex;

    VkResult presentResult = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (presentResult == VK_SUCCESS) {
        vkQueueWaitIdle(m_presentQueue);
    }
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR ||
        m_framebufferResized)
    {
        m_framebufferResized = false;
        RecreateSwapChain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to present Vulkan swapchain image.");
    }

    m_currentFrame = (m_currentFrame + 1) % kVkMaxFramesInFlight;
    m_drawing = false;
    m_frameGeometryPending = false;
}

void QuarkVkRenderer::ClearBackground(Color color) {
    if (m_activeRenderTargetId != 0) {
        auto itRt = m_renderTargets.find(m_activeRenderTargetId);
        if (itRt != m_renderTargets.end()) {
            itRt->second.clearColor = color;
            return;
        }
    }
    m_clearColor = color;
}

void QuarkVkRenderer::PushMatrix() {
    m_matrixStack.push_back(m_currentMatrix);
}

void QuarkVkRenderer::PopMatrix() {
    if (!m_matrixStack.empty()) {
        m_currentMatrix = m_matrixStack.back();
        m_matrixStack.pop_back();
    }
}

void QuarkVkRenderer::Translate(const Vec3& t) {
    m_currentMatrix = m_currentMatrix * Mat4::translation(t.x, t.y, t.z);
}

void QuarkVkRenderer::Rotate(float angle, const Vec3& axis) {
    if (axis.x == 1.f && axis.y == 0.f && axis.z == 0.f)
        m_currentMatrix = m_currentMatrix * Mat4::rotationX(angle);
    else if (axis.y == 1.f && axis.x == 0.f && axis.z == 0.f)
        m_currentMatrix = m_currentMatrix * Mat4::rotationY(angle);
    else if (axis.z == 1.f && axis.x == 0.f && axis.y == 0.f)
        m_currentMatrix = m_currentMatrix * Mat4::rotationZ(angle);
}

void QuarkVkRenderer::Scale(const Vec3& s) {
    m_currentMatrix = m_currentMatrix * Mat4::scale(s.x, s.y, s.z);
}

void QuarkVkRenderer::MultMatrix(const Mat4& matrix) {
    m_currentMatrix = m_currentMatrix * matrix;
}

const float* QuarkVkRenderer::GetMatrixModelview()  {
    return m_currentMatrix.m;
}

const float* QuarkVkRenderer::GetMatrixProjection() {
    return m_projectionMatrix.m;
}

void QuarkVkRenderer::EnableBackfaceCulling() {
    if (m_backfaceCullingEnabled || m_device == VK_NULL_HANDLE) {
        m_backfaceCullingEnabled = true;
        return;
    }

    m_backfaceCullingEnabled = true;
    vkDeviceWaitIdle(m_device);
    CreatePipeline3D();
    CreateShaderPipelines();
}

void QuarkVkRenderer::DisableBackfaceCulling() {
    if (!m_backfaceCullingEnabled || m_device == VK_NULL_HANDLE) {
        m_backfaceCullingEnabled = false;
        return;
    }

    m_backfaceCullingEnabled = false;
    vkDeviceWaitIdle(m_device);
    CreatePipeline3D();
    CreateShaderPipelines();
}

void QuarkVkRenderer::RefreshViewport() {
    m_framebufferResized = true;
}

VkDescriptorSet QuarkVkRenderer::GetTextureDescriptorSet(uint32_t textureId) const {
    const auto it = m_textures.find(textureId);
    if (it == m_textures.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.descriptorSet;
}

void QuarkVkRenderer::CreateInstance() {
    if (m_instance != VK_NULL_HANDLE) return;

    unsigned int extensionCount = 0;
    const char* const* extensionsData = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (!extensionsData) {
        throw std::runtime_error("SDL_Vulkan_GetInstanceExtensions failed.");
    }
    std::vector<const char*> extensions(extensionsData, extensionsData + extensionCount);

#ifdef __APPLE__
    extensions.push_back(
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    );

    extensions.push_back(
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
    );
#endif


#ifdef _DEBUG
    const char* validationLayer =
        "VK_LAYER_KHRONOS_validation";
#endif

    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "QuarkCore Vulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "QuarkCore";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo        = &appInfo;
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

#ifdef _DEBUG
    createInfo.enabledLayerCount = 1;
    createInfo.ppEnabledLayerNames = &validationLayer;
#else
    createInfo.enabledLayerCount = 0;
#endif

#ifdef __APPLE__
    createInfo.flags |=
        VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance.");
    }
    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Vulkan %d.%d.%d instance created.", VK_VERSION_MAJOR(appInfo.apiVersion), VK_VERSION_MINOR(appInfo.apiVersion), VK_VERSION_PATCH(appInfo.apiVersion)));

#ifdef __APPLE__
    TraceLog(
        LogLevel::Info,
        "VULKAN",
        "MoltenVK compatibility enabled."
    );
#endif
}

void QuarkVkRenderer::CreateSurface() {
    if (m_surface != VK_NULL_HANDLE) return;
    if (!SDL_Vulkan_CreateSurface(m_window, m_instance, nullptr, &m_surface)) {
        throw std::runtime_error("Failed to create Vulkan surface from SDL window.");
    }
    TraceLog(LogLevel::Info, "VULKAN", "Vulkan surface created.");
}

void QuarkVkRenderer::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        TraceLog(LogLevel::Error, "VULKAN", "Failed to find GPUs with Vulkan support.");
        throw std::runtime_error("Failed to find GPUs with Vulkan support.");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Found %u physical device(s) with Vulkan support:", deviceCount));

    for (uint32_t i = 0; i < deviceCount; ++i) {
        VkPhysicalDevice device = devices[i];
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(device, &memProps);
        uint64_t vramBytes = 0;
        uint64_t hostBytes = 0;
        for (uint32_t h = 0; h < memProps.memoryHeapCount; ++h) {
            if (memProps.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                vramBytes += memProps.memoryHeaps[h].size;
            } else {
                hostBytes += memProps.memoryHeaps[h].size;
            }
        }
        double vramGb = static_cast<double>(vramBytes) / (1024.0 * 1024.0 * 1024.0);
        double hostGb = static_cast<double>(hostBytes) / (1024.0 * 1024.0 * 1024.0);

        const char* devType = GetVulkanDeviceTypeString(props.deviceType);
        const char* vendorName = GetVulkanVendorName(props.vendorID);
        std::string driverVer = FormatVulkanDriverVersion(props.vendorID, props.driverVersion);
        bool suitable = IsDeviceSuitable(device);

        TraceLog(LogLevel::Info, "VULKAN", TextFormat("  [%u] GPU: %s (%s)", i, props.deviceName, devType));
        TraceLog(LogLevel::Info, "VULKAN", TextFormat("      Vendor: %s (0x%04X), Device ID: 0x%04X, Driver: %s",
            vendorName, props.vendorID, props.deviceID, driverVer.c_str()));
        TraceLog(LogLevel::Info, "VULKAN", TextFormat("      Vulkan API: %d.%d.%d, VRAM: %.2f GB, Shared RAM: %.2f GB (Suitable: %s)",
            VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion),
            vramGb, hostGb, suitable ? "Yes" : "No"));

        if (m_physicalDevice == VK_NULL_HANDLE && suitable) {
            m_physicalDevice = device;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        TraceLog(LogLevel::Error, "VULKAN", "Failed to find a suitable Vulkan physical device.");
        throw std::runtime_error("Failed to find a suitable Vulkan physical device.");
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Selected physical device: %s (%s, Vendor: %s)",
        props.deviceName, GetVulkanDeviceTypeString(props.deviceType), GetVulkanVendorName(props.vendorID)));

    const VkPhysicalDeviceLimits& lim = props.limits;
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Limits: Max 2D Image: %ux%u, Max 3D Image: %u, Max Cube: %u, Max Layers: %u",
        lim.maxImageDimension2D, lim.maxImageDimension3D, lim.maxImageDimensionCube, lim.maxImageArrayLayers));
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Limits: Max UBO Range: %u bytes, Max SSBO Range: %u bytes, Max Push Constants: %u bytes",
        lim.maxUniformBufferRange, lim.maxStorageBufferRange, lim.maxPushConstantsSize));
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Limits: Max Sampler Anisotropy: %.1fx, Max Color Attachments: %u, Max Bound Sets: %u",
        lim.maxSamplerAnisotropy, lim.maxColorAttachments, lim.maxBoundDescriptorSets));
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Limits: Max Vertex Input Attribs: %u, Max Vertex Bindings: %u, Max Memory Allocations: %u",
        lim.maxVertexInputAttributes, lim.maxVertexInputBindings, lim.maxMemoryAllocationCount));
}

void QuarkVkRenderer::CreateLogicalDevice() {
    VkQueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    m_graphicsQueueFamily = indices.graphicsFamily.value();

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(), indices.presentFamily.value()
    };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount       = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount    = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos       = queueCreateInfos.data();
    createInfo.pEnabledFeatures        = &deviceFeatures;
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(kDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();
    createInfo.enabledLayerCount       = 0;

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        TraceLog(LogLevel::Error, "VULKAN", "Failed to create Vulkan logical device.");
        throw std::runtime_error("Failed to create Vulkan logical device.");
    }
    vkGetDeviceQueue(m_device, indices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily.value(),  0, &m_presentQueue);
    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Logical device created successfully (Graphics Queue: #%u, Present Queue: #%u, Extensions: %zu).",
        indices.graphicsFamily.value(), indices.presentFamily.value(), kDeviceExtensions.size()));
}

void QuarkVkRenderer::CreateSwapChain() {
    VkSwapChainSupportDetails details = QuerySwapChainSupport(m_physicalDevice);
    m_swapChainMinImageCount = details.capabilities.minImageCount;

    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(details.formats);
    VkPresentModeKHR   presentMode   = ChooseSwapPresentMode(details.presentModes);
    VkExtent2D         extent        = ChooseSwapExtent(details.capabilities);

    uint32_t imageCount = details.capabilities.minImageCount + 1;
    if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount) {
        imageCount = details.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface          = m_surface;
    createInfo.minImageCount    = imageCount;
    createInfo.imageFormat      = surfaceFormat.format;
    createInfo.imageColorSpace  = surfaceFormat.colorSpace;
    createInfo.imageExtent      = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkQueueFamilyIndices indices         = FindQueueFamilies(m_physicalDevice);
    uint32_t             queueFamilyIndices[] = {
        indices.graphicsFamily.value(), indices.presentFamily.value()
    };

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices   = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices   = nullptr;
    }

    createInfo.preTransform   = details.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode    = presentMode;
    createInfo.clipped        = VK_TRUE;
    createInfo.oldSwapchain   = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain) != VK_SUCCESS) {
        TraceLog(LogLevel::Error, "VULKAN", "Failed to create Vulkan swap chain.");
        throw std::runtime_error("Failed to create Vulkan swap chain.");
    }

    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
    m_swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, m_swapChainImages.data());

    m_swapChainImageFormat = surfaceFormat.format;
    m_swapChainExtent      = extent;

    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Swapchain created: %ux%u, Images: %u, Format: %s (%d), PresentMode: %s, MSAA: %dx",
        m_swapChainExtent.width, m_swapChainExtent.height, imageCount,
        GetVkFormatString(m_swapChainImageFormat), m_swapChainImageFormat,
        GetPresentModeString(presentMode), m_requestedMsaaSamples));
}

void QuarkVkRenderer::CreateImageViews() {
    m_swapChainImageViews.resize(m_swapChainImages.size());

    for (size_t i = 0; i < m_swapChainImages.size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image                           = m_swapChainImages[i];
        createInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format                          = m_swapChainImageFormat;
        createInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel   = 0;
        createInfo.subresourceRange.levelCount     = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(m_device, &createInfo, nullptr, &m_swapChainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan image views.");
        }
    }
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Created %zu image views.", m_swapChainImageViews.size()));
}

void QuarkVkRenderer::CreateRenderPass() {
    m_depthFormat = FindDepthFormat();

    if (m_msaaSamples > VK_SAMPLE_COUNT_1_BIT) {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = m_swapChainImageFormat;
        colorAttachment.samples        = m_msaaSamples;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format         = m_depthFormat;
        depthAttachment.samples        = m_msaaSamples;
        depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription colorAttachmentResolve{};
        colorAttachmentResolve.format         = m_swapChainImageFormat;
        colorAttachmentResolve.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachmentResolve.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachmentResolve.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachmentResolve.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachmentResolve.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachmentResolve.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentResolveRef{};
        colorAttachmentResolveRef.attachment = 2;
        colorAttachmentResolveRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;
        subpass.pResolveAttachments     = &colorAttachmentResolveRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 3> attachments = {
            colorAttachment, depthAttachment, colorAttachmentResolve
        };

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments    = attachments.data();
        renderPassInfo.subpassCount    = 1;
        renderPassInfo.pSubpasses      = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies   = &dependency;

        if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan render pass.");
        }
    } else {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = m_swapChainImageFormat;
        colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format         = m_depthFormat;
        depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments    = attachments.data();
        renderPassInfo.subpassCount    = 1;
        renderPassInfo.pSubpasses      = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies   = &dependency;

        if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan render pass.");
        }
    }
    TraceLog(LogLevel::Trace, "VULKAN", "Swapchain render pass created.");
}

void QuarkVkRenderer::CreateOffscreenRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = m_swapChainImageFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format         = m_depthFormat;
    depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments    = attachments.data();
    renderPassInfo.subpassCount    = 1;
    renderPassInfo.pSubpasses      = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies   = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_offscreenRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan offscreen render pass.");
    }
    TraceLog(LogLevel::Trace, "VULKAN", "Offscreen render pass created.");
}

void QuarkVkRenderer::CreateDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding            = 0;
    samplerBinding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount    = 1;
    samplerBinding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 7> material2DBindings{};
    material2DBindings[0] = samplerBinding;
    for (uint32_t binding = 5; binding <= 10; ++binding) {
        material2DBindings[binding - 4] = { binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                            VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(material2DBindings.size());
    layoutInfo.pBindings    = material2DBindings.data();

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan descriptor set layout.");
    }

    std::array<VkDescriptorSetLayoutBinding, 11> bindings3D{};
    bindings3D[0] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr };
    bindings3D[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    bindings3D[2] = { 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    bindings3D[3] = { 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    bindings3D[4] = { 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    for (uint32_t binding = 5; binding <= 10; ++binding) {
        bindings3D[binding] = { binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo3D{};
    layoutInfo3D.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo3D.bindingCount = static_cast<uint32_t>(bindings3D.size());
    layoutInfo3D.pBindings = bindings3D.data();
    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo3D, nullptr, &m_descriptorSetLayout3D) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan 3D descriptor set layout.");
    }

    if (m_imguiDescriptorPool == VK_NULL_HANDLE) {
        std::array<VkDescriptorPoolSize, 11> poolSizes = {{
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        }};

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets       = 1000 * static_cast<uint32_t>(poolSizes.size());
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes    = poolSizes.data();

        if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_imguiDescriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan ImGui descriptor pool.");
        }
    }

    VkDescriptorPool firstSlab = VK_NULL_HANDLE;
    if (!CreateDescriptorPoolSlab(kVkDescriptorPoolSlabSize, firstSlab)) {
        throw std::runtime_error("Failed to create initial Vulkan descriptor pool.");
    }
    m_descriptorPools.push_back(firstSlab);
    if (!CreateBuffer(4096,
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      m_3DDummyBuffer, m_3DDummyMemory)) {
        throw std::runtime_error("Failed to create Vulkan 3D uniform buffer.");
    }
    if (vkMapMemory(m_device, m_3DDummyMemory, 0, 4096, 0, &m_3DDummyMapped) != VK_SUCCESS) {
        throw std::runtime_error("Failed to map Vulkan 3D uniform buffer.");
    }
    std::memset(m_3DDummyMapped, 0, 4096);
    const Mat4 identity = Mat4::identity();
    std::memcpy(static_cast<char*>(m_3DDummyMapped) + 0, identity.m, sizeof(identity.m));
    std::memcpy(static_cast<char*>(m_3DDummyMapped) + 64, identity.m, sizeof(identity.m));
    std::memcpy(static_cast<char*>(m_3DDummyMapped) + 128, identity.m, sizeof(identity.m));
    TraceLog(LogLevel::Trace, "VULKAN", "Descriptor set layout and initial pool created.");
}

bool QuarkVkRenderer::CreateDescriptorPoolSlab(uint32_t maxSets, VkDescriptorPool& outPool) {
    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 11 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets * 3 }
    }};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets       = maxSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes    = poolSizes.data();

    outPool = VK_NULL_HANDLE;
    return vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &outPool) == VK_SUCCESS;
}

bool QuarkVkRenderer::Allocate3DDescriptorSet(VkDescriptorSet& outSet) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout3D;
    for (auto it = m_descriptorPools.rbegin(); it != m_descriptorPools.rend(); ++it) {
        allocInfo.descriptorPool = *it;
        VkResult result = vkAllocateDescriptorSets(m_device, &allocInfo, &outSet);
        if (result == VK_SUCCESS) return true;
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) return false;
    }
    VkDescriptorPool newSlab = VK_NULL_HANDLE;
    if (!CreateDescriptorPoolSlab(kVkDescriptorPoolSlabSize, newSlab)) return false;
    m_descriptorPools.push_back(newSlab);
    allocInfo.descriptorPool = newSlab;
    return vkAllocateDescriptorSets(m_device, &allocInfo, &outSet) == VK_SUCCESS;
}

VkDescriptorSet QuarkVkRenderer::CreateMaterialDescriptorSet(const Material& material) {
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    if (!Allocate3DDescriptorSet(descriptorSet)) return VK_NULL_HANDLE;

    const auto whiteIt = m_textures.find(m_whiteTextureId);
    if (whiteIt == m_textures.end()) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorImageInfo whiteImage{};
    whiteImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    whiteImage.imageView = whiteIt->second.view;
    whiteImage.sampler = whiteIt->second.sampler;

    std::array<VkDescriptorImageInfo, 7> images{};
    images.fill(whiteImage);
    const auto setFallbackImage = [&](size_t index, uint32_t textureId) {
        const auto textureIt = m_textures.find(textureId);
        if (textureIt != m_textures.end()) {
            images[index].imageView = textureIt->second.view;
            images[index].sampler = textureIt->second.sampler;
        }
    };
    setFallbackImage(1, m_blackTextureId);
    setFallbackImage(2, m_flatNormalTextureId);
    setFallbackImage(5, m_blackTextureId);
    const std::array<int, 6> mapIndices = {
        MATERIAL_MAP_ALBEDO, MATERIAL_MAP_METALNESS, MATERIAL_MAP_NORMAL,
        MATERIAL_MAP_ROUGHNESS, MATERIAL_MAP_OCCLUSION, MATERIAL_MAP_EMISSION
    };
    for (size_t i = 0; i < mapIndices.size(); ++i) {
        if (!material.maps) continue;
        const MaterialMap& map = material.maps[mapIndices[i]];
        const auto textureIt = m_textures.find(map.texture.id);
        if (map.texture.valid && textureIt != m_textures.end()) {
            images[i].imageView = textureIt->second.view;
            images[i].sampler = textureIt->second.sampler;
        }
    }

    VkDescriptorBufferInfo matrices{};
    matrices.buffer = m_3DDummyBuffer;
    matrices.offset = 0;
    matrices.range = 192;
    VkDescriptorBufferInfo shadowBuffer = matrices;
    shadowBuffer.offset = 512;
    shadowBuffer.range = 512;
    VkDescriptorBufferInfo lightBuffer = matrices;
    lightBuffer.offset = 1024;
    lightBuffer.range = 320;
    std::array<VkDescriptorImageInfo, 4> shadowImages{};
    shadowImages.fill(whiteImage);
    if (m_shadowImageView != VK_NULL_HANDLE) {
        VkDescriptorImageInfo shadowImage{};
        shadowImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shadowImage.imageView = m_shadowImageView;
        shadowImage.sampler = m_shadowSampler;
        shadowImages.fill(shadowImage);
    }

    std::array<VkWriteDescriptorSet, 11> writes{};
    writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1,
                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &matrices, nullptr };
    writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1,
                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &images[0], nullptr, nullptr };
    writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 2, 0, 4,
                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, shadowImages.data(), nullptr, nullptr };
    writes[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 3, 0, 1,
                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &shadowBuffer, nullptr };
    writes[4] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 4, 0, 1,
                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &lightBuffer, nullptr };
    for (uint32_t i = 0; i < 6; ++i) {
        writes[5 + i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 5 + i, 0, 1,
                          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &images[i + 1], nullptr, nullptr };
    }
    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return descriptorSet;
}

bool QuarkVkRenderer::AllocateTextureDescriptorSet(VkDescriptorSet& outSet) {
    if (m_descriptorSetLayout == VK_NULL_HANDLE) return false;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &m_descriptorSetLayout;

    for (auto it = m_descriptorPools.rbegin(); it != m_descriptorPools.rend(); ++it) {
        allocInfo.descriptorPool = *it;
        VkResult result = vkAllocateDescriptorSets(m_device, &allocInfo, &outSet);
        if (result == VK_SUCCESS) return true;
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
            return false;
        }
    }

    VkDescriptorPool newSlab = VK_NULL_HANDLE;
    if (!CreateDescriptorPoolSlab(kVkDescriptorPoolSlabSize, newSlab)) return false;
    m_descriptorPools.push_back(newSlab);

    allocInfo.descriptorPool = newSlab;
    return vkAllocateDescriptorSets(m_device, &allocInfo, &outSet) == VK_SUCCESS;
}

void QuarkVkRenderer::CreateFramebuffers() {
    CreateMSAAColorResources();

    m_swapChainDepthImages.resize(m_swapChainImageViews.size(), VK_NULL_HANDLE);
    m_swapChainDepthMemories.resize(m_swapChainImageViews.size(), VK_NULL_HANDLE);
    m_swapChainDepthImageViews.resize(m_swapChainImageViews.size(), VK_NULL_HANDLE);

    for (size_t i = 0; i < m_swapChainImageViews.size(); ++i) {
        if (!CreateDepthResources(m_swapChainExtent.width, m_swapChainExtent.height,
                                  m_swapChainDepthImages[i],
                                  m_swapChainDepthMemories[i],
                                  m_swapChainDepthImageViews[i],
                                  m_msaaSamples)) {
            throw std::runtime_error("Failed to create Vulkan depth resources.");
        }
    }

    m_swapChainFramebuffers.resize(m_swapChainImageViews.size());

    for (size_t i = 0; i < m_swapChainImageViews.size(); ++i) {
        std::vector<VkImageView> attachments;
        if (m_msaaSamples > VK_SAMPLE_COUNT_1_BIT) {
            attachments = {
                m_msaaColorImageView,
                m_swapChainDepthImageViews[i],
                m_swapChainImageViews[i]
            };
        } else {
            attachments = {
                m_swapChainImageViews[i],
                m_swapChainDepthImageViews[i]
            };
        }

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass      = m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments    = attachments.data();
        framebufferInfo.width           = m_swapChainExtent.width;
        framebufferInfo.height          = m_swapChainExtent.height;
        framebufferInfo.layers          = 1;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan framebuffer.");
        }
    }
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Created %zu framebuffers.", m_swapChainFramebuffers.size()));
}

bool QuarkVkRenderer::RecreateRenderTargetFramebuffers() {
    for (auto& [id, rt] : m_renderTargets) {
        (void)id;
        auto itTex = m_textures.find(rt.textureId);
        if (itTex == m_textures.end()) continue;

        if (rt.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, rt.framebuffer, nullptr);
            rt.framebuffer = VK_NULL_HANDLE;
        }

        if (rt.depthView == VK_NULL_HANDLE) {
            if (!CreateDepthResources(rt.width, rt.height, rt.depthImage, rt.depthMemory, rt.depthView)) {
                return false;
            }
        }

        std::array<VkImageView, 2> attachments = { itTex->second.view, rt.depthView };
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = m_offscreenRenderPass;
        fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbInfo.pAttachments    = attachments.data();
        fbInfo.width           = rt.width;
        fbInfo.height          = rt.height;
        fbInfo.layers          = 1;
        if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &rt.framebuffer) != VK_SUCCESS) {
            DestroyDepthResources(rt.depthImage, rt.depthMemory, rt.depthView);
            return false;
        }
    }
    return true;
}

void QuarkVkRenderer::CreateCommandPool() {
    VkQueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan command pool.");
    }
    TraceLog(LogLevel::Trace, "VULKAN", "Command pool created.");
}

void QuarkVkRenderer::CreateCommandBuffers() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_frames.size());

    std::vector<VkCommandBuffer> commandBuffers(m_frames.size());
    if (vkAllocateCommandBuffers(m_device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan command buffers.");
    }
    for (size_t i = 0; i < m_frames.size(); ++i) {
        m_frames[i].commandBuffer = commandBuffers[i];
    }
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Allocated %zu command buffers.", m_frames.size()));
}

void QuarkVkRenderer::CreateFrameVertexIndexBuffers() {
    const VkDeviceSize vertexBufSize = sizeof(VkBatchVertex) * kVkMaxVerticesPerFrame;
    const VkDeviceSize indexBufSize  = sizeof(uint32_t)      * kVkMaxIndicesPerFrame;
    const VkDeviceSize vertexBufSize3D = sizeof(Vk3DVertex)  * kVkMaxVerticesPerFrame;

    for (auto& frame : m_frames) {
        if (!CreateBuffer(vertexBufSize,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          frame.vertexBuffer, frame.vertexMemory)) {
            throw std::runtime_error("Failed to create per-frame Vulkan vertex buffer.");
        }
        vkMapMemory(m_device, frame.vertexMemory, 0, vertexBufSize, 0, &frame.vertexMapped);
        frame.vertexCapacity = vertexBufSize;

        if (!CreateBuffer(indexBufSize,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          frame.indexBuffer, frame.indexMemory)) {
            throw std::runtime_error("Failed to create per-frame Vulkan index buffer.");
        }
        vkMapMemory(m_device, frame.indexMemory, 0, indexBufSize, 0, &frame.indexMapped);
        frame.indexCapacity = indexBufSize;

        if (!CreateBuffer(vertexBufSize3D,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          frame.vertexBuffer3D, frame.vertexMemory3D)) {
            throw std::runtime_error("Failed to create per-frame Vulkan 3D vertex buffer.");
        }
        vkMapMemory(m_device, frame.vertexMemory3D, 0, vertexBufSize3D, 0, &frame.vertexMapped3D);
        frame.vertexCapacity3D = vertexBufSize3D;
    }
    TraceLog(LogLevel::Trace, "VULKAN", "Per-frame vertex/index buffers created.");
}

void QuarkVkRenderer::CreateSyncObjects() {
    for (auto& frame : m_frames) {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &frame.imageAvailable) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &frame.renderFinished) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &frame.inFlightFence) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan synchronization objects.");
        }
    }
    TraceLog(LogLevel::Trace, "VULKAN", "Synchronization objects created.");
}

void QuarkVkRenderer::CreateWhiteTexture() {
    const unsigned char white[4] = {255, 255, 255, 255};
    if (!CreateTextureFromRGBA(white, 1, 1, m_whiteTextureId)) {
        throw std::runtime_error("Failed to create Vulkan white fallback texture.");
    }
    const unsigned char black[4] = {0, 0, 0, 255};
    if (!CreateTextureFromRGBA(black, 1, 1, m_blackTextureId)) {
        throw std::runtime_error("Failed to create Vulkan black fallback texture.");
    }
    const unsigned char flatNormal[4] = {128, 128, 255, 255};
    if (!CreateTextureFromRGBA(flatNormal, 1, 1, m_flatNormalTextureId)) {
        throw std::runtime_error("Failed to create Vulkan normal fallback texture.");
    }
    Material whiteMaterial{};
    m_white3DDescriptorSet = CreateMaterialDescriptorSet(whiteMaterial);
    if (m_white3DDescriptorSet == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to create Vulkan white 3D descriptor set.");
    }
    TraceLog(LogLevel::Trace, "VULKAN", "White fallback texture created.");
}

VkPipeline QuarkVkRenderer::CreatePipelineForRenderPass(VkRenderPass renderPass,
                                                        VkShaderModule vertexModule,
                                                        VkShaderModule fragmentModule) {
    if (m_device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    if (m_pipelineLayout == VK_NULL_HANDLE) {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset     = 0;
        pushConstantRange.size       = sizeof(VkPushConstants2D);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount         = 1;
        layoutInfo.pSetLayouts            = &m_descriptorSetLayout;
        layoutInfo.pushConstantRangeCount  = 1;
        layoutInfo.pPushConstantRanges     = &pushConstantRange;

        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan pipeline layout.");
        }
    }

    bool ownsVertexModule = vertexModule == VK_NULL_HANDLE;
    bool ownsFragmentModule = fragmentModule == VK_NULL_HANDLE;
    if (ownsVertexModule) {
        const std::vector<uint32_t> vertCode = CompileRuntimeShader(
            kRuntime2DVertexShader, shaderc_vertex_shader, "built-in 2D vertex shader");
        vertexModule = CreateShaderModule(vertCode);
    }
    if (ownsFragmentModule) {
        const std::vector<uint32_t> fragCode = CompileRuntimeShader(
            kRuntime2DFragmentShader, shaderc_fragment_shader, "built-in 2D fragment shader");
        fragmentModule = CreateShaderModule(fragCode);
    }

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertexModule;
    vertStage.pName  = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragmentModule;
    fragStage.pName  = "main";

    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding   = 0;
    bindingDesc.stride    = sizeof(VkBatchVertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributes{};
    attributes[0].binding  = 0;
    attributes[0].location = 0;
    attributes[0].format   = VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset   = offsetof(VkBatchVertex, x);

    attributes[1].binding  = 0;
    attributes[1].location = 1;
    attributes[1].format   = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset   = offsetof(VkBatchVertex, u);

    attributes[2].binding  = 0;
    attributes[2].location = 2;
    attributes[2].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[2].offset   = offsetof(VkBatchVertex, r);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount  = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions    = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable  = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable        = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.cullMode                = VK_CULL_MODE_NONE;
    rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = (renderPass == m_renderPass) ? m_msaaSamples : VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable  = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable       = VK_FALSE;
    depthStencil.depthWriteEnable      = VK_FALSE;
    depthStencil.depthCompareOp        = VK_COMPARE_OP_ALWAYS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable     = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                          VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT |
                                          VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable    = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor  = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp         = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable     = VK_FALSE;
    colorBlending.attachmentCount   = 1;
    colorBlending.pAttachments      = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(sizeof(dynamicStates) / sizeof(dynamicStates[0]));
    dynamicState.pDynamicStates    = dynamicStates;

    std::array<VkPipelineShaderStageCreateInfo, 2> stages = { vertStage, fragStage };

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages             = stages.data();
    pipelineInfo.pVertexInputState   = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = m_pipelineLayout;
    pipelineInfo.renderPass          = renderPass;
    pipelineInfo.subpass             = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        if (ownsFragmentModule) vkDestroyShaderModule(m_device, fragmentModule, nullptr);
        if (ownsVertexModule) vkDestroyShaderModule(m_device, vertexModule, nullptr);
        throw std::runtime_error("Failed to create Vulkan 2D pipeline.");
    }

    if (ownsFragmentModule) vkDestroyShaderModule(m_device, fragmentModule, nullptr);
    if (ownsVertexModule) vkDestroyShaderModule(m_device, vertexModule, nullptr);
    return pipeline;
}

void QuarkVkRenderer::CreateShaderPipelines() {
    for (auto& [id, program] : m_shaderPrograms) {
        (void)id;
        if (program.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, program.pipeline, nullptr);
            program.pipeline = VK_NULL_HANDLE;
        }
        if (!program.supports3D) {
            program.pipeline = CreatePipelineForRenderPass(m_renderPass,
                                                           program.vertexModule,
                                                           program.fragmentModule);
        }
        if (program.pipeline3D != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, program.pipeline3D, nullptr);
        }
        if (program.supports3D) {
            program.pipeline3D = Create3DPipelineForRenderPass(m_renderPass,
                                                                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                                program.vertexModule,
                                                                program.fragmentModule);
        }
    }
}

void QuarkVkRenderer::CreatePipeline2D() {
    if (m_pipeline2D != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline2D, nullptr);
        m_pipeline2D = VK_NULL_HANDLE;
    }
    m_pipeline2D = CreatePipelineForRenderPass(m_renderPass);
}

void QuarkVkRenderer::CreateOffscreenPipeline2D() {
    if (m_offscreenPipeline2D != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_offscreenPipeline2D, nullptr);
        m_offscreenPipeline2D = VK_NULL_HANDLE;
    }
    m_offscreenPipeline2D = CreatePipelineForRenderPass(m_offscreenRenderPass);
}

VkPipeline QuarkVkRenderer::Create3DPipelineForRenderPass(VkRenderPass renderPass, VkPrimitiveTopology topology,
                                                           VkShaderModule vertexModule, VkShaderModule fragmentModule) {
    if (m_device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    if (m_pipelineLayout3D == VK_NULL_HANDLE) {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset     = 0;
        pushConstantRange.size       = sizeof(Vk3DPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType                 = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount        = 1;
        layoutInfo.pSetLayouts           = &m_descriptorSetLayout3D;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges   = &pushConstantRange;

        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout3D) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan 3D pipeline layout.");
        }
    }

    const bool ownsVertexModule = vertexModule == VK_NULL_HANDLE;
    const bool ownsFragmentModule = fragmentModule == VK_NULL_HANDLE;
    if (ownsVertexModule) {
        const std::vector<uint32_t> vertCode = CompileRuntimeShader(
            kRuntime3DVertexShader, shaderc_vertex_shader, "built-in 3D vertex shader");
        vertexModule = CreateShaderModule(vertCode);
    }
    if (ownsFragmentModule) {
        const std::vector<uint32_t> fragCode = CompileRuntimeShader(
            kRuntime3DFragmentShader, shaderc_fragment_shader, "built-in 3D fragment shader");
        fragmentModule = CreateShaderModule(fragCode);
    }

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertexModule;
    vertStage.pName  = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragmentModule;
    fragStage.pName  = "main";

    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding   = 0;
    bindingDesc.stride    = sizeof(Vk3DVertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 5> attributes{};
    attributes[0].binding  = 0;
    attributes[0].location = 0;
    attributes[0].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[0].offset   = offsetof(Vk3DVertex, x);

    attributes[1].binding  = 0;
    attributes[1].location = 1;
    attributes[1].format   = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset   = offsetof(Vk3DVertex, u);

    attributes[2].binding  = 0;
    attributes[2].location = 2;
    attributes[2].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[2].offset   = offsetof(Vk3DVertex, r);

    attributes[3].binding  = 0;
    attributes[3].location = 3;
    attributes[3].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[3].offset   = offsetof(Vk3DVertex, nx);

    attributes[4].binding  = 0;
    attributes[4].location = 4;
    attributes[4].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[4].offset   = offsetof(Vk3DVertex, wx);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                          = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount  = 1;
    vertexInput.pVertexBindingDescriptions     = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions   = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType                 = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology              = topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType        = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable        = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.cullMode                = m_backfaceCullingEnabled ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = (renderPass == m_renderPass) ? m_msaaSamples : VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable  = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable       = VK_TRUE;
    depthStencil.depthWriteEnable      = VK_TRUE;
    depthStencil.depthCompareOp        = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable     = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                          VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT |
                                          VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable    = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor  = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp         = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable   = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments    = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(sizeof(dynamicStates) / sizeof(dynamicStates[0]));
    dynamicState.pDynamicStates    = dynamicStates;

    std::array<VkPipelineShaderStageCreateInfo, 2> stages = { vertStage, fragStage };

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages             = stages.data();
    pipelineInfo.pVertexInputState    = &vertexInput;
    pipelineInfo.pInputAssemblyState  = &inputAssembly;
    pipelineInfo.pViewportState       = &viewportState;
    pipelineInfo.pRasterizationState  = &rasterizer;
    pipelineInfo.pMultisampleState    = &multisampling;
    pipelineInfo.pDepthStencilState   = &depthStencil;
    pipelineInfo.pColorBlendState     = &colorBlending;
    pipelineInfo.pDynamicState        = &dynamicState;
    pipelineInfo.layout               = m_pipelineLayout3D;
    pipelineInfo.renderPass           = renderPass;
    pipelineInfo.subpass              = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        if (ownsFragmentModule) vkDestroyShaderModule(m_device, fragmentModule, nullptr);
        if (ownsVertexModule) vkDestroyShaderModule(m_device, vertexModule, nullptr);
        throw std::runtime_error("Failed to create Vulkan 3D pipeline.");
    }

    if (ownsFragmentModule) vkDestroyShaderModule(m_device, fragmentModule, nullptr);
    if (ownsVertexModule) vkDestroyShaderModule(m_device, vertexModule, nullptr);
    return pipeline;
}

void QuarkVkRenderer::CreatePipeline3D() {
    if (m_pipeline3DTri != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline3DTri, nullptr);
        m_pipeline3DTri = VK_NULL_HANDLE;
    }
    if (m_pipeline3DLines != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline3DLines, nullptr);
        m_pipeline3DLines = VK_NULL_HANDLE;
    }
    if (m_offscreenPipeline3DTri != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_offscreenPipeline3DTri, nullptr);
        m_offscreenPipeline3DTri = VK_NULL_HANDLE;
    }
    if (m_offscreenPipeline3DLines != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_offscreenPipeline3DLines, nullptr);
        m_offscreenPipeline3DLines = VK_NULL_HANDLE;
    }

    m_pipeline3DTri = Create3DPipelineForRenderPass(m_renderPass, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    m_pipeline3DLines = Create3DPipelineForRenderPass(m_renderPass, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    m_offscreenPipeline3DTri = Create3DPipelineForRenderPass(m_offscreenRenderPass, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    m_offscreenPipeline3DLines = Create3DPipelineForRenderPass(m_offscreenRenderPass, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
}

void QuarkVkRenderer::CreateShadowResources() {
    constexpr uint32_t shadowSize = 2048;
    m_shadowFormat = FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM },
        VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { shadowSize, shadowSize, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_shadowFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_shadowImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow image.");
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, m_shadowImage, &requirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &allocation, nullptr, &m_shadowMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan shadow image memory.");
    }
    vkBindImageMemory(m_device, m_shadowImage, m_shadowMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_shadowImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_shadowFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_shadowImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow image view.");
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_shadowSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow sampler.");
    }

    VkAttachmentDescription attachment{};
    attachment.format = m_shadowFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthReference{ 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthReference;
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();
    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_shadowRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow render pass.");
    }

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_shadowRenderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &m_shadowImageView;
    framebufferInfo.width = shadowSize;
    framebufferInfo.height = shadowSize;
    framebufferInfo.layers = 1;
    if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_shadowFramebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shadow framebuffer.");
    }
}

void QuarkVkRenderer::CreateShadowPipeline() {
    static const char* shadowVertexShader = R"glsl(
#version 450
layout(push_constant) uniform ShadowData { mat4 lightViewProjection; } shadowData;
layout(location = 0) in vec4 aWorldPosition;
void main() { gl_Position = shadowData.lightViewProjection * aWorldPosition; }
)glsl";
    const std::vector<uint32_t> code = CompileRuntimeShader(
        shadowVertexShader, shaderc_vertex_shader, "built-in shadow vertex shader");
    VkShaderModule module = CreateShaderModule(code);

    VkPushConstantRange pushConstant{};
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstant.size = sizeof(Mat4);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstant;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_shadowPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(m_device, module, nullptr);
        throw std::runtime_error("Failed to create Vulkan shadow pipeline layout.");
    }

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage.module = module;
    stage.pName = "main";
    VkVertexInputBindingDescription binding{ 0, sizeof(Vk3DVertex), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attribute{ 0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vk3DVertex, wx) };
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.25f;
    rasterizer.depthBiasSlopeFactor = 1.75f;
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 1;
    pipelineInfo.pStages = &stage;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_shadowPipelineLayout;
    pipelineInfo.renderPass = m_shadowRenderPass;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_shadowPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(m_device, module, nullptr);
        throw std::runtime_error("Failed to create Vulkan shadow pipeline.");
    }
    vkDestroyShaderModule(m_device, module, nullptr);
}

void QuarkVkRenderer::DestroyShadowResources() {
    if (m_shadowPipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_device, m_shadowPipeline, nullptr);
    if (m_shadowPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_device, m_shadowPipelineLayout, nullptr);
    if (m_shadowFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(m_device, m_shadowFramebuffer, nullptr);
    if (m_shadowRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(m_device, m_shadowRenderPass, nullptr);
    if (m_shadowSampler != VK_NULL_HANDLE) vkDestroySampler(m_device, m_shadowSampler, nullptr);
    if (m_shadowImageView != VK_NULL_HANDLE) vkDestroyImageView(m_device, m_shadowImageView, nullptr);
    if (m_shadowImage != VK_NULL_HANDLE) vkDestroyImage(m_device, m_shadowImage, nullptr);
    if (m_shadowMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_shadowMemory, nullptr);
    m_shadowPipeline = VK_NULL_HANDLE;
    m_shadowPipelineLayout = VK_NULL_HANDLE;
    m_shadowFramebuffer = VK_NULL_HANDLE;
    m_shadowRenderPass = VK_NULL_HANDLE;
    m_shadowSampler = VK_NULL_HANDLE;
    m_shadowImageView = VK_NULL_HANDLE;
    m_shadowImage = VK_NULL_HANDLE;
    m_shadowMemory = VK_NULL_HANDLE;
}

void QuarkVkRenderer::RecreateSwapChain() {
    TraceLog(LogLevel::Info, "VULKAN", "Recreating swapchain...");
    vkDeviceWaitIdle(m_device);

    CleanupSwapChain();

    CreateSwapChain();
    CreateImageViews();
    CreateRenderPass();
    CreateOffscreenRenderPass();
    CreatePipeline2D();
    CreateOffscreenPipeline2D();
    CreateShaderPipelines();
    CreatePipeline3D();
    CreateFramebuffers();
    RecreateRenderTargetFramebuffers();
}

void QuarkVkRenderer::CleanupSwapChain() {
    TraceLog(LogLevel::Trace, "VULKAN", "Cleaning up swapchain resources...");

    DestroyMSAAColorResources();

    for (auto& [id, rt] : m_renderTargets) {
        (void)id;
        if (rt.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, rt.framebuffer, nullptr);
            rt.framebuffer = VK_NULL_HANDLE;
        }
    }

    for (auto framebuffer : m_swapChainFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, framebuffer, nullptr);
        }
    }
    m_swapChainFramebuffers.clear();

    for (size_t i = 0; i < m_swapChainDepthImageViews.size(); ++i) {
        if (m_swapChainDepthImageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, m_swapChainDepthImageViews[i], nullptr);
        }
        if (m_swapChainDepthImages.size() > i && m_swapChainDepthImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(m_device, m_swapChainDepthImages[i], nullptr);
        }
        if (m_swapChainDepthMemories.size() > i && m_swapChainDepthMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_swapChainDepthMemories[i], nullptr);
        }
    }
    m_swapChainDepthImageViews.clear();
    m_swapChainDepthImages.clear();
    m_swapChainDepthMemories.clear();

    if (m_offscreenPipeline2D != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_offscreenPipeline2D, nullptr);
        m_offscreenPipeline2D = VK_NULL_HANDLE;
    }
    if (m_offscreenPipeline3DLines != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_offscreenPipeline3DLines, nullptr);
        m_offscreenPipeline3DLines = VK_NULL_HANDLE;
    }
    if (m_offscreenPipeline3DTri != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_offscreenPipeline3DTri, nullptr);
        m_offscreenPipeline3DTri = VK_NULL_HANDLE;
    }
    if (m_pipeline2D != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline2D, nullptr);
        m_pipeline2D = VK_NULL_HANDLE;
    }
    if (m_pipeline3DLines != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline3DLines, nullptr);
        m_pipeline3DLines = VK_NULL_HANDLE;
    }
    if (m_pipeline3DTri != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline3DTri, nullptr);
        m_pipeline3DTri = VK_NULL_HANDLE;
    }
    for (auto& [id, shaderProgram] : m_shaderPrograms) {
        (void)id;
        if (shaderProgram.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, shaderProgram.pipeline, nullptr);
            shaderProgram.pipeline = VK_NULL_HANDLE;
        }
        if (shaderProgram.pipeline3D != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, shaderProgram.pipeline3D, nullptr);
            shaderProgram.pipeline3D = VK_NULL_HANDLE;
        }
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout3D != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout3D, nullptr);
        m_pipelineLayout3D = VK_NULL_HANDLE;
    }

    if (m_offscreenRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_offscreenRenderPass, nullptr);
        m_offscreenRenderPass = VK_NULL_HANDLE;
    }
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    for (auto imageView : m_swapChainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, imageView, nullptr);
        }
    }
    m_swapChainImageViews.clear();

    if (m_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }
    m_swapChainImages.clear();
}

VkQueueFamilyIndices QuarkVkRenderer::FindQueueFamilies(VkPhysicalDevice device) const {
    VkQueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }
        if (indices.isComplete()) break;
    }
    return indices;
}

VkSwapChainSupportDetails QuarkVkRenderer::QuerySwapChainSupport(VkPhysicalDevice device) const {
    VkSwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
    }
    return details;
}

bool QuarkVkRenderer::IsDeviceSuitable(VkPhysicalDevice device) const {
    VkQueueFamilyIndices indices = FindQueueFamilies(device);
    if (!indices.isComplete()) return false;

    VkSwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device);
    return !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
}

VkSurfaceFormatKHR QuarkVkRenderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const {
    for (const auto& fmt : formats) {
        if ((fmt.format == VK_FORMAT_B8G8R8A8_SRGB || fmt.format == VK_FORMAT_B8G8R8A8_UNORM) &&
            fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return fmt;
        }
    }
    return formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
}

VkPresentModeKHR QuarkVkRenderer::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& modes) const {
    const bool vsync = m_vsyncExplicitlySet ? m_vsync : (m_targetFps != 0);
    if (!vsync) {
        for (const auto& mode : modes) {
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) return mode;
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

void QuarkVkRenderer::SetTargetFPS(int fps) {
    m_targetFps = fps;
    if (!m_vsyncExplicitlySet && m_swapChain != VK_NULL_HANDLE) {
        RecreateSwapChain();
    }
}

bool QuarkVkRenderer::SetVSync(bool enabled) {
    if (m_vsync == enabled && m_vsyncExplicitlySet) {
        return true;
    }
    m_vsync = enabled;
    m_vsyncExplicitlySet = true;
    if (m_swapChain != VK_NULL_HANDLE) {
        RecreateSwapChain();
    }
    return true;
}

VkExtent2D QuarkVkRenderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps) const {
    if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;

    VkExtent2D actual = { static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height) };
    actual.width  = std::max(caps.minImageExtent.width,  std::min(caps.maxImageExtent.width,  actual.width));
    actual.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, actual.height));
    return actual;
}

uint32_t QuarkVkRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable Vulkan memory type.");
}

VkFormat QuarkVkRenderer::FindSupportedFormat(const std::vector<VkFormat>& candidates,
                                              VkImageTiling tiling,
                                              VkFormatFeatureFlags features) const {
    for (VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR &&
            (props.linearTilingFeatures & features) == features) {
            return format;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL &&
            (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported Vulkan format.");
}

VkFormat QuarkVkRenderer::FindDepthFormat() const {
    return FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

bool QuarkVkRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                    VkMemoryPropertyFlags props,
                                    VkBuffer& outBuffer, VkDeviceMemory& outMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, outBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, props);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(m_device, outBuffer, outMemory, 0);
    return true;
}

bool QuarkVkRenderer::EnsureMappedBufferCapacity(VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped,
                                                 VkDeviceSize& capacity, VkDeviceSize required,
                                                 VkBufferUsageFlags usage) {
    if (required <= capacity) {
        return true;
    }

    VkDeviceSize newCapacity = capacity > 0 ? capacity : 4096;
    while (newCapacity < required) {
        newCapacity *= 2;
    }

    VkBuffer newBuffer = VK_NULL_HANDLE;
    VkDeviceMemory newMemory = VK_NULL_HANDLE;
    if (!CreateBuffer(newCapacity,
                      usage,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      newBuffer,
                      newMemory)) {
        return false;
    }

    void* newMapped = nullptr;
    if (vkMapMemory(m_device, newMemory, 0, newCapacity, 0, &newMapped) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, newBuffer, nullptr);
        vkFreeMemory(m_device, newMemory, nullptr);
        return false;
    }

    vkDeviceWaitIdle(m_device);
    if (mapped != nullptr && memory != VK_NULL_HANDLE) {
        vkUnmapMemory(m_device, memory);
    }
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, buffer, nullptr);
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, memory, nullptr);
    }

    buffer = newBuffer;
    memory = newMemory;
    mapped = newMapped;
    capacity = newCapacity;
    return true;
}

bool QuarkVkRenderer::CreateDepthResources(uint32_t width, uint32_t height,
                                           VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView,
                                           VkSampleCountFlagBits samples) {
    outImage = VK_NULL_HANDLE;
    outMemory = VK_NULL_HANDLE;
    outView = VK_NULL_HANDLE;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width  = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.format        = m_depthFormat;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples       = samples;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &outImage) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(m_device, outImage, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) {
        vkDestroyImage(m_device, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        return false;
    }

    vkBindImageMemory(m_device, outImage, outMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = outImage;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = m_depthFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (HasStencilComponent(m_depthFormat)) {
        viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &outView) != VK_SUCCESS) {
        vkFreeMemory(m_device, outMemory, nullptr);
        vkDestroyImage(m_device, outImage, nullptr);
        outImage = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

void QuarkVkRenderer::DestroyDepthResources(VkImage& image, VkDeviceMemory& memory, VkImageView& view) {
    if (view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, image, nullptr);
        image = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

VkSampleCountFlagBits QuarkVkRenderer::GetSampleCountForSamples(int samples) const {
    if (samples <= 1 || m_physicalDevice == VK_NULL_HANDLE) return VK_SAMPLE_COUNT_1_BIT;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;

    VkSampleCountFlagBits requested = VK_SAMPLE_COUNT_1_BIT;
    if (samples >= 8) requested = VK_SAMPLE_COUNT_8_BIT;
    else if (samples >= 4) requested = VK_SAMPLE_COUNT_4_BIT;
    else if (samples >= 2) requested = VK_SAMPLE_COUNT_2_BIT;

    if (counts & requested) {
        return requested;
    }
    if (samples >= 8 && (counts & VK_SAMPLE_COUNT_4_BIT)) return VK_SAMPLE_COUNT_4_BIT;
    if (samples >= 4 && (counts & VK_SAMPLE_COUNT_2_BIT)) return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

void QuarkVkRenderer::SetMSAASamples(int samples) {
    m_requestedMsaaSamples = (samples == 2 || samples == 4 || samples == 8) ? samples : 1;
    if (m_physicalDevice != VK_NULL_HANDLE) {
        VkSampleCountFlagBits newSamples = GetSampleCountForSamples(m_requestedMsaaSamples);
        if (newSamples != m_msaaSamples) {
            m_msaaSamples = newSamples;
            if (m_swapChain != VK_NULL_HANDLE) {
                RecreateSwapChain();
            }
        }
    }
}

void QuarkVkRenderer::CreateMSAAColorResources() {
    if (m_msaaSamples <= VK_SAMPLE_COUNT_1_BIT) {
        return;
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width  = m_swapChainExtent.width;
    imageInfo.extent.height = m_swapChainExtent.height;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.format        = m_swapChainImageFormat;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.samples       = m_msaaSamples;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &m_msaaColorImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create MSAA color image.");
    }

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(m_device, m_msaaColorImage, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_msaaColorMemory) != VK_SUCCESS) {
        vkDestroyImage(m_device, m_msaaColorImage, nullptr);
        m_msaaColorImage = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to allocate MSAA color image memory.");
    }

    vkBindImageMemory(m_device, m_msaaColorImage, m_msaaColorMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = m_msaaColorImage;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = m_swapChainImageFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_msaaColorImageView) != VK_SUCCESS) {
        vkFreeMemory(m_device, m_msaaColorMemory, nullptr);
        m_msaaColorMemory = VK_NULL_HANDLE;
        vkDestroyImage(m_device, m_msaaColorImage, nullptr);
        m_msaaColorImage = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to create MSAA color image view.");
    }
}

void QuarkVkRenderer::DestroyMSAAColorResources() {
    if (m_msaaColorImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_msaaColorImageView, nullptr);
        m_msaaColorImageView = VK_NULL_HANDLE;
    }
    if (m_msaaColorImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_msaaColorImage, nullptr);
        m_msaaColorImage = VK_NULL_HANDLE;
    }
    if (m_msaaColorMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_msaaColorMemory, nullptr);
        m_msaaColorMemory = VK_NULL_HANDLE;
    }
}

VkCommandBuffer QuarkVkRenderer::BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &cmd) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

void QuarkVkRenderer::EndSingleTimeCommands(VkCommandBuffer cmd) {
    if (cmd == VK_NULL_HANDLE) return;
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
}

bool QuarkVkRenderer::TransitionImageLayout(VkImage image, VkFormat /*format*/,
                                             VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) return false;

    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout                       = oldLayout;
    barrier.newLayout                       = newLayout;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndSingleTimeCommands(cmd);
    return true;
}

bool QuarkVkRenderer::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t w, uint32_t h) {
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) return false;

    VkBufferImageCopy region{};
    region.bufferOffset                    = 0;
    region.bufferRowLength                 = 0;
    region.bufferImageHeight               = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset                     = {0, 0, 0};
    region.imageExtent                     = {w, h, 1};

    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndSingleTimeCommands(cmd);
    return true;
}

bool QuarkVkRenderer::CreateTextureFromRGBA(const unsigned char* rgba,
                                             uint32_t width, uint32_t height,
                                             uint32_t& outId) {
    if (!rgba || width == 0 || height == 0) {
        TraceLog(LogLevel::Warn, "TEXTURE", "[Vulkan] Cannot create texture: invalid parameters (null data or zero size)");
        return false;
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4u;
    TraceLog(LogLevel::Trace, "TEXTURE", TextFormat("[Vulkan] Creating GPU texture: %ux%u (%llu bytes RGBA8)",
        width, height, static_cast<unsigned long long>(imageSize)));

    VkBuffer       stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    if (!CreateBuffer(imageSize,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      stagingBuffer, stagingMemory)) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed to allocate staging buffer for texture upload");
        return false;
    }

    void* mapped = nullptr;
    vkMapMemory(m_device, stagingMemory, 0, imageSize, 0, &mapped);
    std::memcpy(mapped, rgba, static_cast<size_t>(imageSize));
    vkUnmapMemory(m_device, stagingMemory);

    VkTextureData tex{};
    tex.width  = width;
    tex.height = height;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width  = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &tex.image) != VK_SUCCESS) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed to create VkImage");
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device, tex.image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &tex.memory) != VK_SUCCESS) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed to allocate device memory for texture");
        vkDestroyImage(m_device, tex.image, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        return false;
    }
    vkBindImageMemory(m_device, tex.image, tex.memory, 0);

    if (!TransitionImageLayout(tex.image, imageInfo.format,
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) ||
        !CopyBufferToImage(stagingBuffer, tex.image, width, height) ||
        !TransitionImageLayout(tex.image, imageInfo.format,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed image transitions or buffer copy for texture");
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        return false;
    }

    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = tex.image;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &tex.view) != VK_SUCCESS) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed to create VkImageView");
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
        return false;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter    = (gTextureFilterMode == TextureFilterMode::Nearest) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    samplerInfo.minFilter    = (gTextureFilterMode == TextureFilterMode::Nearest) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    samplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod       = 1.0f;

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &tex.sampler) != VK_SUCCESS) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed to create VkSampler");
        vkDestroyImageView(m_device, tex.view, nullptr);
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
        return false;
    }

    if (!AllocateTextureDescriptorSet(tex.descriptorSet)) {
        TraceLog(LogLevel::Error, "TEXTURE", "[Vulkan] Failed to allocate texture descriptor set");
        vkDestroySampler(m_device, tex.sampler, nullptr);
        vkDestroyImageView(m_device, tex.view, nullptr);
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
        return false;
    }

    VkDescriptorImageInfo imageDescriptor{};
    imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageDescriptor.imageView   = tex.view;
    imageDescriptor.sampler     = tex.sampler;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = tex.descriptorSet;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imageDescriptor;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

    outId = m_nextTextureId++;
    m_textures[outId] = tex;

    TraceLog(LogLevel::Trace, "TEXTURE", TextFormat("[Vulkan] Texture uploaded to GPU (ID: %u, %ux%u, Mem: %llu bytes, DS: %p)",
        outId, width, height, static_cast<unsigned long long>(memReq.size), (void*)tex.descriptorSet));
    return true;
}

void QuarkVkRenderer::DestroyTexture(uint32_t textureId) {
    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) return;

    VkTextureData& tex = it->second;
    TraceLog(LogLevel::Info, "TEXTURE", TextFormat("[Vulkan] Texture destroyed (ID: %u, %ux%u)", textureId, tex.width, tex.height));

    if (tex.sampler  != VK_NULL_HANDLE) vkDestroySampler   (m_device, tex.sampler,  nullptr);
    if (tex.view     != VK_NULL_HANDLE) vkDestroyImageView (m_device, tex.view,     nullptr);
    if (tex.image    != VK_NULL_HANDLE) vkDestroyImage     (m_device, tex.image,    nullptr);
    if (tex.memory   != VK_NULL_HANDLE) vkFreeMemory       (m_device, tex.memory,   nullptr);
    m_textures.erase(it);
}

IRenderTexture QuarkVkRenderer::CreateRenderTargetInternal(int width, int height) {
    if (width <= 0 || height <= 0 ||
        m_device == VK_NULL_HANDLE ||
        m_offscreenRenderPass == VK_NULL_HANDLE) {
        return IRenderTexture{};
    }

    VkTextureData tex{};
    tex.width  = static_cast<uint32_t>(width);
    tex.height = static_cast<uint32_t>(height);
    tex.isRenderTarget = true;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.extent        = { tex.width, tex.height, 1 };
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.format        = m_swapChainImageFormat;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &tex.image) != VK_SUCCESS) {
        return IRenderTexture{};
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device, tex.image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &tex.memory) != VK_SUCCESS) {
        vkDestroyImage(m_device, tex.image, nullptr);
        return IRenderTexture{};
    }
    vkBindImageMemory(m_device, tex.image, tex.memory, 0);

    if (!TransitionImageLayout(tex.image, imageInfo.format,
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
        return IRenderTexture{};
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = tex.image;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = imageInfo.format;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &tex.view) != VK_SUCCESS) {
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
        return IRenderTexture{};
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter    = VK_FILTER_LINEAR;
    samplerInfo.minFilter    = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod       = 1.0f;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &tex.sampler) != VK_SUCCESS) {
        vkDestroyImageView(m_device, tex.view, nullptr);
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
        return IRenderTexture{};
    }

    if (!AllocateTextureDescriptorSet(tex.descriptorSet)) {
        vkDestroySampler(m_device, tex.sampler, nullptr);
        vkDestroyImageView(m_device, tex.view, nullptr);
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
        return IRenderTexture{};
    }

    VkDescriptorImageInfo imageDescriptor{};
    imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageDescriptor.imageView   = tex.view;
    imageDescriptor.sampler     = tex.sampler;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = tex.descriptorSet;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imageDescriptor;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

    const uint32_t textureId = m_nextTextureId++;
    m_textures[textureId] = tex;

    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    if (!CreateDepthResources(tex.width, tex.height, depthImage, depthMemory, depthView)) {
        DestroyTexture(textureId);
        return IRenderTexture{};
    }

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    std::array<VkImageView, 2> attachments = { tex.view, depthView };

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass      = m_offscreenRenderPass;
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments    = attachments.data();
    fbInfo.width           = tex.width;
    fbInfo.height          = tex.height;
    fbInfo.layers          = 1;
    if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        DestroyDepthResources(depthImage, depthMemory, depthView);
        DestroyTexture(textureId);
        return IRenderTexture{};
    }

    const uint32_t rtId = m_nextRenderTargetId++;
    VkRenderTargetData rt{};
    rt.textureId   = textureId;
    rt.width       = tex.width;
    rt.height      = tex.height;
    rt.framebuffer = framebuffer;
    rt.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    rt.depthImage  = depthImage;
    rt.depthMemory = depthMemory;
    rt.depthView   = depthView;
    m_renderTargets[rtId] = rt;

    TraceLog(LogLevel::Info, "RENDER_TARGET", TextFormat("[Vulkan] Render target created: %ux%u (Target ID: %u, Color Tex ID: %u, Depth Buffer: yes)",
        tex.width, tex.height, rtId, textureId));
    return IRenderTexture{ rtId, {}, 0 };
}

void QuarkVkRenderer::DestroyRenderTargetInternal(uint32_t renderTargetId) {
    auto it = m_renderTargets.find(renderTargetId);
    if (it == m_renderTargets.end()) return;

    TraceLog(LogLevel::Info, "RENDER_TARGET", TextFormat("[Vulkan] Render target destroyed (Target ID: %u, %ux%u)",
        renderTargetId, it->second.width, it->second.height));

    const uint32_t textureId = it->second.textureId;
    if (it->second.framebuffer != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, it->second.framebuffer, nullptr);
    }
    if (m_device != VK_NULL_HANDLE) {
        DestroyDepthResources(it->second.depthImage, it->second.depthMemory, it->second.depthView);
    }
    m_renderTargets.erase(it);

    if (m_activeRenderTargetId == renderTargetId) {
        m_activeRenderTargetId = 0;
    }
    DestroyTexture(textureId);
}

VkShaderModule QuarkVkRenderer::CreateShaderModule(const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode    = spirv.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        TraceLog(LogLevel::Error, "VULKAN", "[Vulkan] Failed to create Vulkan shader module.");
        throw std::runtime_error("Failed to create Vulkan shader module.");
    }
    TraceLog(LogLevel::Trace, "SHADER", TextFormat("[Vulkan] Created VkShaderModule (%zu words / %zu bytes, Handle: %p)",
        spirv.size(), createInfo.codeSize, (void*)shaderModule));
    return shaderModule;
}

bool QuarkVkRenderer::ReadBinaryFile(const char* path, std::vector<char>& outData) const {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) return false;
    const std::streamsize fileSize = file.tellg();
    outData.resize(static_cast<size_t>(fileSize));
    file.seekg(0);
    file.read(outData.data(), fileSize);
    return true;
}

void QuarkVkRenderer::BuildCombinedFrameGeometry() {
    m_frameVertices.clear();
    m_frameIndices.clear();
    m_frameDrawItems.clear();
    m_framePasses.clear();
    m_frameTriangleVertices3D.clear();
    m_frameLineVertices3D.clear();
    m_frame3DShaderProgramId = 0;

    for (auto& [id, rt] : m_renderTargets) {
        const bool has2D = !rt.drawItems.empty();
        const bool has3D = !rt.triangleVertices3D.empty() || !rt.lineVertices3D.empty();
        if (!has2D && !has3D) continue;

        const uint32_t baseVertex = static_cast<uint32_t>(m_frameVertices.size());
        const uint32_t baseIndex  = static_cast<uint32_t>(m_frameIndices.size());
        const uint32_t firstDraw  = static_cast<uint32_t>(m_frameDrawItems.size());
        const uint32_t triFirst   = static_cast<uint32_t>(m_frameTriangleVertices3D.size());
        const uint32_t lineFirst  = static_cast<uint32_t>(m_frameLineVertices3D.size());
        const uint32_t first3DDrawItem = static_cast<uint32_t>(m_frame3DDrawItems.size());

        m_frameVertices.insert(m_frameVertices.end(), rt.vertices.begin(), rt.vertices.end());
        for (uint32_t idx : rt.indices) {
            m_frameIndices.push_back(baseVertex + idx);
        }
        for (VkDrawItem item : rt.drawItems) {
            item.firstIndex += baseIndex;
            m_frameDrawItems.push_back(item);
        }

        m_frameTriangleVertices3D.insert(m_frameTriangleVertices3D.end(),
                                         rt.triangleVertices3D.begin(), rt.triangleVertices3D.end());
        m_frameLineVertices3D.insert(m_frameLineVertices3D.end(),
                                     rt.lineVertices3D.begin(), rt.lineVertices3D.end());
        for (Vk3DDrawItem item : rt.drawItems3D) {
            item.firstVertex += triFirst;
            m_frame3DDrawItems.push_back(item);
        }

        m_framePasses.push_back(VkFramePass{
            id,
            firstDraw,
            static_cast<uint32_t>(m_frameDrawItems.size() - firstDraw),
            rt.width,
            rt.height,
            triFirst,
            static_cast<uint32_t>(rt.triangleVertices3D.size()),
            lineFirst,
            static_cast<uint32_t>(rt.lineVertices3D.size()),
            first3DDrawItem,
            static_cast<uint32_t>(m_frame3DDrawItems.size() - first3DDrawItem)
        });
    }

    {
        const uint32_t baseVertex = static_cast<uint32_t>(m_frameVertices.size());
        const uint32_t baseIndex  = static_cast<uint32_t>(m_frameIndices.size());
        const uint32_t firstDraw  = static_cast<uint32_t>(m_frameDrawItems.size());
        const uint32_t triFirst   = static_cast<uint32_t>(m_frameTriangleVertices3D.size());
        const uint32_t lineFirst  = static_cast<uint32_t>(m_frameLineVertices3D.size());
        const uint32_t first3DDrawItem = static_cast<uint32_t>(m_frame3DDrawItems.size());

        m_frameVertices.insert(m_frameVertices.end(), m_batchVertices.begin(), m_batchVertices.end());
        for (uint32_t idx : m_batchIndices) {
            m_frameIndices.push_back(baseVertex + idx);
        }
        for (VkDrawItem item : m_batchDrawItems) {
            item.firstIndex += baseIndex;
            m_frameDrawItems.push_back(item);
        }

        m_frameTriangleVertices3D.insert(m_frameTriangleVertices3D.end(),
                                         m_main3DBatch.triangleVertices.begin(),
                                         m_main3DBatch.triangleVertices.end());
        m_frameLineVertices3D.insert(m_frameLineVertices3D.end(),
                                     m_main3DBatch.lineVertices.begin(),
                                     m_main3DBatch.lineVertices.end());
        for (Vk3DDrawItem item : m_main3DBatch.drawItems) {
            item.firstVertex += triFirst;
            m_frame3DDrawItems.push_back(item);
        }
        m_frame3DShaderProgramId = m_main3DBatch.shaderProgramId;

        m_framePasses.push_back(VkFramePass{
            0,
            firstDraw,
            static_cast<uint32_t>(m_frameDrawItems.size() - firstDraw),
            static_cast<uint32_t>(m_swapChainExtent.width),
            static_cast<uint32_t>(m_swapChainExtent.height),
            triFirst,
            static_cast<uint32_t>(m_main3DBatch.triangleVertices.size()),
            lineFirst,
            static_cast<uint32_t>(m_main3DBatch.lineVertices.size()),
            first3DDrawItem,
            static_cast<uint32_t>(m_frame3DDrawItems.size() - first3DDrawItem)
        });
    }
}

bool QuarkVkRenderer::UploadFrameGeometry(uint32_t frameIndex) {
    VkFrameData& frame = m_frames[frameIndex];
    const VkDeviceSize vertexBytes = m_frameVertices.size() * sizeof(VkBatchVertex);
    const VkDeviceSize indexBytes = m_frameIndices.size() * sizeof(uint32_t);
    const VkDeviceSize vertexBytes3D =
        (m_frameTriangleVertices3D.size() + m_frameLineVertices3D.size()) * sizeof(Vk3DVertex);

    if (!EnsureMappedBufferCapacity(frame.vertexBuffer, frame.vertexMemory, frame.vertexMapped,
                                    frame.vertexCapacity, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
        !EnsureMappedBufferCapacity(frame.indexBuffer, frame.indexMemory, frame.indexMapped,
                                    frame.indexCapacity, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT) ||
        !EnsureMappedBufferCapacity(frame.vertexBuffer3D, frame.vertexMemory3D, frame.vertexMapped3D,
                                    frame.vertexCapacity3D, vertexBytes3D, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        TraceLog(LogLevel::Error, "VULKAN", "Failed to grow per-frame geometry buffer.");
        return false;
    }

    if (!m_frameVertices.empty() && frame.vertexMapped) {
        std::memcpy(frame.vertexMapped, m_frameVertices.data(),
                    m_frameVertices.size() * sizeof(VkBatchVertex));
    }
    if (!m_frameIndices.empty() && frame.indexMapped) {
        std::memcpy(frame.indexMapped, m_frameIndices.data(),
                    m_frameIndices.size() * sizeof(uint32_t));
    }
    if (frame.vertexMapped3D) {
        const size_t triBytes = m_frameTriangleVertices3D.size() * sizeof(Vk3DVertex);
        const size_t lineBytes = m_frameLineVertices3D.size() * sizeof(Vk3DVertex);
        if (triBytes + lineBytes > 0) {
            std::memcpy(frame.vertexMapped3D, m_frameTriangleVertices3D.data(), triBytes);
            if (lineBytes > 0) {
                std::memcpy(static_cast<char*>(frame.vertexMapped3D) + triBytes,
                            m_frameLineVertices3D.data(), lineBytes);
            }
        }
    }
    return true;
}

void QuarkVkRenderer::AppendQuadToBatch(
    std::vector<VkBatchVertex>& vertices,
    std::vector<uint32_t>&      indices,
    std::vector<VkDrawItem>&    drawItems,
    VkDescriptorSet             ds,
    float x0, float y0,
    float x1, float y1,
    float x2, float y2,
    float x3, float y3,
    float r, float g, float b, float a,
    float u0, float v0,
    float u1, float v1)
{
    const Vec2 p0 = ApplyCameraTransform(Vec2{ x0, y0 });
    const Vec2 p1 = ApplyCameraTransform(Vec2{ x1, y1 });
    const Vec2 p2 = ApplyCameraTransform(Vec2{ x2, y2 });
    const Vec2 p3 = ApplyCameraTransform(Vec2{ x3, y3 });

    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.push_back({ p0.x, p0.y, u0, v0, r, g, b, a });
    vertices.push_back({ p1.x, p1.y, u1, v0, r, g, b, a });
    vertices.push_back({ p2.x, p2.y, u1, v1, r, g, b, a });
    vertices.push_back({ p3.x, p3.y, u0, v1, r, g, b, a });

    const uint32_t firstIndex = static_cast<uint32_t>(indices.size());
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
    indices.push_back(base + 0);

    if (!drawItems.empty() &&
        drawItems.back().descriptorSet == ds &&
        drawItems.back().shaderProgramId == m_currentShaderProgramId &&
        drawItems.back().firstIndex + drawItems.back().indexCount == firstIndex) {
        drawItems.back().indexCount += 6;
    } else {
        drawItems.push_back({ 0, m_currentShaderProgramId, ds, firstIndex, 6 });
    }
}

void QuarkVkRenderer::AppendQuad(
    VkDescriptorSet ds,
    float x0, float y0,
    float x1, float y1,
    float x2, float y2,
    float x3, float y3,
    float r, float g, float b, float a,
    float u0, float v0, float u1, float v1)
{
    if (m_activeRenderTargetId != 0) {
        auto itRt = m_renderTargets.find(m_activeRenderTargetId);
        if (itRt != m_renderTargets.end()) {
            AppendQuadToBatch(itRt->second.vertices, itRt->second.indices,
                              itRt->second.drawItems, ds,
                              x0, y0, x1, y1, x2, y2, x3, y3,
                              r, g, b, a, u0, v0, u1, v1);
            return;
        }
    }
    AppendQuadToBatch(m_batchVertices, m_batchIndices, m_batchDrawItems, ds,
                      x0, y0, x1, y1, x2, y2, x3, y3,
                      r, g, b, a, u0, v0, u1, v1);
}

bool QuarkVkRenderer::RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) return false;

    const VkFrameData& frame = m_frames[m_currentFrame];
    VkDeviceSize offsets[] = { 0 };
    VkDescriptorSet whiteDescriptorSet = m_white3DDescriptorSet;
    VkDescriptorSet white2DDescriptorSet = VK_NULL_HANDLE;
    const auto whiteTexIt = m_textures.find(m_whiteTextureId);
    if (whiteTexIt != m_textures.end()) {
        white2DDescriptorSet = whiteTexIt->second.descriptorSet;
    }
    
    for (const VkFramePass& pass : m_framePasses) {
        if (pass.renderTargetId == 0) continue;

        auto itRt  = m_renderTargets.find(pass.renderTargetId);
        if (itRt == m_renderTargets.end()) continue;

        Color passClearColor = m_clearColor;
        if (pass.renderTargetId != 0) {
            if (itRt != m_renderTargets.end()) {
                passClearColor = itRt->second.clearColor;
            }
        }

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{
            NormalizeColorComponent(passClearColor.r),
            NormalizeColorComponent(passClearColor.g),
            NormalizeColorComponent(passClearColor.b),
            NormalizeColorComponent(passClearColor.a)
        }};
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rtPassInfo{};
        rtPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rtPassInfo.renderPass        = m_offscreenRenderPass;
        rtPassInfo.framebuffer       = itRt->second.framebuffer;
        rtPassInfo.renderArea.extent = { pass.width, pass.height };
        rtPassInfo.clearValueCount   = static_cast<uint32_t>(clearValues.size());
        rtPassInfo.pClearValues      = clearValues.data();
        vkCmdBeginRenderPass(cmd, &rtPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = 0.0f;
        viewport.width    = static_cast<float>(pass.width);
        viewport.height   = static_cast<float>(pass.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = { pass.width, pass.height };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        if ((pass.triVertexCount + pass.lineVertexCount) > 0 &&
            whiteDescriptorSet != VK_NULL_HANDLE) {
            vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertexBuffer3D, offsets);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout3D, 0, 1, &whiteDescriptorSet, 0, nullptr);

            if (pass.drawItemCount3D > 0) {
                uint32_t cursor = pass.triFirstVertex;
                for (uint32_t i = 0; i < pass.drawItemCount3D; ++i) {
                    const Vk3DDrawItem& item = m_frame3DDrawItems[pass.first3DDrawItem + i];
                    if (item.firstVertex > cursor && m_offscreenPipeline3DTri != VK_NULL_HANDLE) {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_offscreenPipeline3DTri);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                m_pipelineLayout3D, 0, 1, &whiteDescriptorSet, 0, nullptr);
                        vkCmdDraw(cmd, item.firstVertex - cursor, 1, cursor, 0);
                    }
                    const VkDescriptorSet descriptorSet = item.descriptorSet != VK_NULL_HANDLE
                        ? item.descriptorSet : whiteDescriptorSet;
                    if (m_offscreenPipeline3DTri == VK_NULL_HANDLE || descriptorSet == VK_NULL_HANDLE) continue;
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_offscreenPipeline3DTri);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_pipelineLayout3D, 0, 1, &descriptorSet, 0, nullptr);
                    vkCmdDraw(cmd, item.vertexCount, 1, item.firstVertex, 0);
                    cursor = std::max(cursor, item.firstVertex + item.vertexCount);
                }
                const uint32_t triEnd = pass.triFirstVertex + pass.triVertexCount;
                if (cursor < triEnd && m_offscreenPipeline3DTri != VK_NULL_HANDLE) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_offscreenPipeline3DTri);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_pipelineLayout3D, 0, 1, &whiteDescriptorSet, 0, nullptr);
                    vkCmdDraw(cmd, triEnd - cursor, 1, cursor, 0);
                }
            } else if (pass.triVertexCount > 0 && m_offscreenPipeline3DTri != VK_NULL_HANDLE) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_offscreenPipeline3DTri);
                vkCmdDraw(cmd, pass.triVertexCount, 1, pass.triFirstVertex, 0);
            }
            if (pass.lineVertexCount > 0 && m_offscreenPipeline3DLines != VK_NULL_HANDLE) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_offscreenPipeline3DLines);
                vkCmdDraw(cmd, pass.lineVertexCount, 1,
                          static_cast<uint32_t>(m_frameTriangleVertices3D.size()) + pass.lineFirstVertex, 0);
            }
        }

        if (m_offscreenPipeline2D != VK_NULL_HANDLE) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_offscreenPipeline2D);
        }
        const VkPushConstants2D rtPushConstants{
            static_cast<float>(pass.width),
            static_cast<float>(pass.height)
        };
        vkCmdPushConstants(cmd, m_pipelineLayout,
                   VK_SHADER_STAGE_VERTEX_BIT,
                   0, sizeof(rtPushConstants), &rtPushConstants);
        vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertexBuffer, offsets);
        vkCmdBindIndexBuffer(cmd, frame.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        for (uint32_t i = 0; i < pass.drawItemCount; ++i) {
            const VkDrawItem& item = m_frameDrawItems[pass.firstDrawItem + i];
            if (item.descriptorSet == VK_NULL_HANDLE) continue;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout, 0, 1, &item.descriptorSet, 0, nullptr);
            vkCmdDrawIndexed(cmd, item.indexCount, 1, item.firstIndex, 0, 0);
        }

        vkCmdEndRenderPass(cmd);
    }

    const VkFramePass* mainPass = nullptr;
    for (const VkFramePass& pass : m_framePasses) {
        if (pass.renderTargetId == 0) {
            mainPass = &pass;
            break;
        }
    }

    if (mainPass && mainPass->triVertexCount > 0 &&
        m_shadowRenderPass != VK_NULL_HANDLE && m_shadowPipeline != VK_NULL_HANDLE) {
        const Mat4 lightView = Mat4::lookAt(
            Vec3{ -20.0f, 30.0f, 20.0f }, Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ 0.0f, 1.0f, 0.0f });
        Mat4 shadowProjection = Mat4::identity();
        shadowProjection.m[0] = 2.0f / 60.0f;
        shadowProjection.m[5] = 2.0f / 60.0f;
        shadowProjection.m[10] = -1.0f / 99.0f;
        shadowProjection.m[12] = 0.0f;
        shadowProjection.m[13] = 0.0f;
        shadowProjection.m[14] = -1.0f / 99.0f;
        m_shadowViewProjection = MultiplyColumnMajor(shadowProjection, lightView);
        if (m_3DDummyMapped != nullptr) {
            std::memcpy(static_cast<char*>(m_3DDummyMapped) + 512,
                        m_shadowViewProjection.m, sizeof(m_shadowViewProjection.m));
        }

        VkClearValue shadowClear{};
        shadowClear.depthStencil = { 1.0f, 0 };
        VkRenderPassBeginInfo shadowPassInfo{};
        shadowPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        shadowPassInfo.renderPass = m_shadowRenderPass;
        shadowPassInfo.framebuffer = m_shadowFramebuffer;
        shadowPassInfo.renderArea.extent = { 2048, 2048 };
        shadowPassInfo.clearValueCount = 1;
        shadowPassInfo.pClearValues = &shadowClear;
        vkCmdBeginRenderPass(cmd, &shadowPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport shadowViewport{ 0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f };
        VkRect2D shadowScissor{};
        shadowScissor.extent = { 2048, 2048 };
        vkCmdSetViewport(cmd, 0, 1, &shadowViewport);
        vkCmdSetScissor(cmd, 0, 1, &shadowScissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
        vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertexBuffer3D, offsets);
        vkCmdPushConstants(cmd, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(m_shadowViewProjection), &m_shadowViewProjection);
        vkCmdDraw(cmd, mainPass->triVertexCount, 1, mainPass->triFirstVertex, 0);
        vkCmdEndRenderPass(cmd);
    }

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{
        NormalizeColorComponent(m_clearColor.r),
        NormalizeColorComponent(m_clearColor.g),
        NormalizeColorComponent(m_clearColor.b),
        NormalizeColorComponent(m_clearColor.a)
    }};
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass        = m_renderPass;
    renderPassInfo.framebuffer       = m_swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.extent = m_swapChainExtent;
    renderPassInfo.clearValueCount   = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues      = clearValues.data();
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(m_swapChainExtent.width);
    viewport.height   = static_cast<float>(m_swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = m_swapChainExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (mainPass && (mainPass->triVertexCount + mainPass->lineVertexCount) > 0 &&
        whiteDescriptorSet != VK_NULL_HANDLE) {
        vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertexBuffer3D, offsets);
        VkPipeline custom3DPipeline = m_pipeline3DTri;
        VkPipelineLayout custom3DLayout = m_pipelineLayout3D;
        VkDescriptorSet custom3DSet = whiteDescriptorSet;
        if (m_frame3DShaderProgramId != 0) {
            const auto shaderIt = m_shaderPrograms.find(m_frame3DShaderProgramId);
            if (shaderIt != m_shaderPrograms.end() && shaderIt->second.pipeline3D != VK_NULL_HANDLE) {
                custom3DPipeline = shaderIt->second.pipeline3D;
                custom3DLayout = m_pipelineLayout3D;
                custom3DSet = shaderIt->second.descriptorSet3D;
            }
        }

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                custom3DLayout, 0, 1, &custom3DSet, 0, nullptr);

        {
            if (m_3DDummyMapped != nullptr) {
                std::memcpy(static_cast<char*>(m_3DDummyMapped) + 0,
                            m_currentMatrix.m, sizeof(m_currentMatrix.m));
                std::memcpy(static_cast<char*>(m_3DDummyMapped) + 64,
                            m_viewMatrix.m, sizeof(m_viewMatrix.m));
                std::memcpy(static_cast<char*>(m_3DDummyMapped) + 128,
                            m_projectionMatrix.m, sizeof(m_projectionMatrix.m));
            }
            Vk3DPushConstants lighting{};
            const float positions[4][3] = {
                { -3.0f, 4.0f,  3.0f }, {  3.0f, 3.0f,  3.0f },
                { -3.0f, 3.0f, -3.0f }, {  3.0f, 3.0f, -3.0f }
            };
            const float colors[4][3] = {
                { 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }
            };
            for (int lightIndex = 0; lightIndex < 4; ++lightIndex) {
                std::copy(std::begin(positions[lightIndex]), std::end(positions[lightIndex]), lighting.lightPositions[lightIndex]);
                lighting.lightPositions[lightIndex][3] = 1.0f;
                std::copy(std::begin(colors[lightIndex]), std::end(colors[lightIndex]), lighting.lightColors[lightIndex]);
                lighting.lightColors[lightIndex][3] = 1.0f;
                lighting.lightEnabled[lightIndex] = m_3DLightEnabled[static_cast<size_t>(lightIndex)] ? 1.0f : 0.0f;
            }
            lighting.timeData[0] = static_cast<float>(SDL_GetTicks()) / 1000.0f;
            if (m_3DDummyMapped != nullptr) {
                float* lightData = static_cast<float*>(static_cast<void*>(static_cast<char*>(m_3DDummyMapped) + 1024));
                std::fill(lightData, lightData + 80, 0.0f);
                lightData[0] = 0.1f;
                lightData[1] = 0.1f;
                lightData[2] = 0.1f;
                lightData[4] = 1.0f;
                lightData[5] = 1.0f;
                lightData[6] = 1.0f;
                lightData[8] = 2.0f;
                lightData[9] = 4.0f;
                lightData[10] = 7.0f;
                for (int lightIndex = 0; lightIndex < 4; ++lightIndex) {
                    const size_t base = 12 + static_cast<size_t>(lightIndex) * 16;
                    lightData[base + 0] = positions[lightIndex][0];
                    lightData[base + 1] = positions[lightIndex][1];
                    lightData[base + 2] = positions[lightIndex][2];
                    lightData[base + 3] = 1.0f;
                    lightData[base + 7] = 1.0f;
                    lightData[base + 8] = colors[lightIndex][0];
                    lightData[base + 9] = colors[lightIndex][1];
                    lightData[base + 10] = colors[lightIndex][2];
                    lightData[base + 11] = 1.0f;
                    lightData[base + 12] = 0.08f;
                    lightData[base + 13] = m_3DLightEnabled[static_cast<size_t>(lightIndex)] ? 1.0f : 0.0f;
                    lightData[base + 14] = 1.0f;
                }
            }
            vkCmdPushConstants(cmd, custom3DLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(lighting), &lighting);
        }

        if (mainPass->drawItemCount3D > 0) {
            uint32_t cursor = mainPass->triFirstVertex;
            for (uint32_t i = 0; i < mainPass->drawItemCount3D; ++i) {
                const Vk3DDrawItem& item = m_frame3DDrawItems[mainPass->first3DDrawItem + i];
                if (item.firstVertex > cursor && m_pipeline3DTri != VK_NULL_HANDLE) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline3DTri);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_pipelineLayout3D, 0, 1, &whiteDescriptorSet, 0, nullptr);
                    vkCmdDraw(cmd, item.firstVertex - cursor, 1, cursor, 0);
                }
                VkPipeline pipeline = m_pipeline3DTri;
                VkPipelineLayout layout = m_pipelineLayout3D;
                VkDescriptorSet descriptorSet = whiteDescriptorSet;
                const auto shaderIt = m_shaderPrograms.find(item.shaderProgramId);
                if (shaderIt != m_shaderPrograms.end() && shaderIt->second.pipeline3D != VK_NULL_HANDLE) {
                    pipeline = shaderIt->second.pipeline3D;
                    layout = m_pipelineLayout3D;
                    descriptorSet = item.descriptorSet;
                } else if (item.descriptorSet != VK_NULL_HANDLE) {
                    descriptorSet = item.descriptorSet;
                }
                if (pipeline == VK_NULL_HANDLE || descriptorSet == VK_NULL_HANDLE) continue;
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        layout, 0, 1, &descriptorSet, 0, nullptr);
                vkCmdDraw(cmd, item.vertexCount, 1, item.firstVertex, 0);
                cursor = std::max(cursor, item.firstVertex + item.vertexCount);
            }
            const uint32_t triEnd = mainPass->triFirstVertex + mainPass->triVertexCount;
            if (cursor < triEnd && m_pipeline3DTri != VK_NULL_HANDLE) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline3DTri);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_pipelineLayout3D, 0, 1, &whiteDescriptorSet, 0, nullptr);
                vkCmdDraw(cmd, triEnd - cursor, 1, cursor, 0);
            }
        } else {
            if (mainPass->triVertexCount > 0 && custom3DPipeline != VK_NULL_HANDLE) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, custom3DPipeline);
                vkCmdDraw(cmd, mainPass->triVertexCount, 1, mainPass->triFirstVertex, 0);
            }
            if (mainPass->lineVertexCount > 0 && m_pipeline3DLines != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_pipelineLayout3D, 0, 1, &whiteDescriptorSet, 0, nullptr);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline3DLines);
                vkCmdDraw(cmd, mainPass->lineVertexCount, 1,
                          static_cast<uint32_t>(m_frameTriangleVertices3D.size()) + mainPass->lineFirstVertex, 0);
            }
        }
    }

    if (white2DDescriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelineLayout, 0, 1, &white2DDescriptorSet, 0, nullptr);
    }

    VkPipeline pipeline2D = m_pipeline2D;
    if (m_currentShaderProgramId != 0) {
        const auto shaderIt = m_shaderPrograms.find(m_currentShaderProgramId);
        if (shaderIt != m_shaderPrograms.end() && shaderIt->second.pipeline != VK_NULL_HANDLE) {
            pipeline2D = shaderIt->second.pipeline;
        }
    }
    const VkPushConstants2D screenPushConstants{
        static_cast<float>(m_swapChainExtent.width),
        static_cast<float>(m_swapChainExtent.height)
    };
    vkCmdPushConstants(cmd, m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(screenPushConstants), &screenPushConstants);
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertexBuffer, offsets);
    vkCmdBindIndexBuffer(cmd, frame.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    VkPipeline boundPipeline = VK_NULL_HANDLE;
    for (const VkFramePass& pass : m_framePasses) {
        if (pass.renderTargetId != 0) continue;
        for (uint32_t i = 0; i < pass.drawItemCount; ++i) {
            const VkDrawItem& item = m_frameDrawItems[pass.firstDrawItem + i];
            if (item.descriptorSet == VK_NULL_HANDLE) continue;

            VkPipeline itemPipeline = pipeline2D;
            if (item.shaderProgramId != 0) {
                const auto shaderIt = m_shaderPrograms.find(item.shaderProgramId);
                if (shaderIt != m_shaderPrograms.end() && shaderIt->second.pipeline != VK_NULL_HANDLE) {
                    itemPipeline = shaderIt->second.pipeline;
                }
            }
            if (itemPipeline != boundPipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, itemPipeline);
                boundPipeline = itemPipeline;
            }
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout, 0, 1, &item.descriptorSet, 0, nullptr);
            vkCmdDrawIndexed(cmd, item.indexCount, 1, item.firstIndex, 0, 0);
        }
    }

    if (const VulkanRenderCallback callback = GetVulkanRenderCallback()) {
        callback(cmd);
    }

    vkCmdEndRenderPass(cmd);
    return vkEndCommandBuffer(cmd) == VK_SUCCESS;
}

void QuarkVkRenderer::FlushBatch() {}

void QuarkVkRenderer::PushQuad(float x, float y, float w, float h, Color color,
                                float u0, float v0, float u1, float v1) {
    if (m_textures.find(m_whiteTextureId) == m_textures.end()) return;
    VkDescriptorSet ds = m_textures.at(m_whiteTextureId).descriptorSet;

    const float r = NormalizeColorComponent(color.r);
    const float g = NormalizeColorComponent(color.g);
    const float b = NormalizeColorComponent(color.b);
    const float a = NormalizeColorComponent(color.a);

    AppendQuad(ds,
               x,     y,
               x + w, y,
               x + w, y + h,
               x,     y + h,
               r, g, b, a,
               u0, v0, u1, v1);
}

void QuarkVkRenderer::EnsureBatchTexture(VkDescriptorSet ds) {
    m_currentDescriptorSet = ds;
}

} // namespace qc
