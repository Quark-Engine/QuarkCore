#include "QuarkVkRenderer.hpp"

#include <SDL3/SDL_vulkan.h>
#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_glsl.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace qc {
namespace {

static const char* shaderLocationNames[SHADER_LOC_COUNT] = {
    "aPosition",
    "aTexCoord0",
    "aTexCoord1",
    "aNormal",
    "aTangent",
    "aColor",
    "mvp",
    "view",
    "projection",
    "model",
    "normalMatrix",
    "viewPos",
    "colDiffuse",
    "colSpecular",
    "colAmbient",
    "albedo",
    "metalness",
    "normal",
    "roughness",
    "occlusion",
    "emission",
    "height",
    "cubemap",
    "irradiance",
    "prefilter",
    "brdf",
    "boneIds",
    "boneWeights",
    "boneTransforms",
    "instanceTransform"
};

bool ReadTextFile(const char* path, std::string& out) {
    if (!path) return false;
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) return false;
    out.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return true;
}

bool CompileGlslToSpirv(const std::string& source,
                        shaderc_shader_kind shaderKind,
                        const char* stageName,
                        std::vector<uint32_t>& outSpirv) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);

    const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
        source, shaderKind, "QuarkCore runtime shader", options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        TraceLog(LogLevel::Error, "VULKAN",
                 TextFormat("Failed to compile %s shader with Shaderc: %s",
                            stageName, result.GetErrorMessage().c_str()));
        return false;
    }

    outSpirv.assign(result.cbegin(), result.cend());
    return !outSpirv.empty();
}

void ReflectShaderResources(VkShaderProgramData& programData,
                           const std::vector<uint32_t>& spirv,
                           const char* stageName) {
    if (spirv.empty()) return;

    try {
        spirv_cross::CompilerGLSL compiler(spirv);
        const auto resources = compiler.get_shader_resources();

        for (const auto& resource : resources.stage_inputs) {
            const std::string name = compiler.get_name(resource.id);
            if (name.empty()) {
                continue;
            }
            const uint32_t location = compiler.get_decoration(resource.id, spv::DecorationLocation);
            programData.attributes[name] = static_cast<int>(location);
        }

        for (const auto& resource : resources.uniform_buffers) {
            const std::string name = compiler.get_name(resource.id);
            if (!name.empty() && programData.uniforms.find(name) == programData.uniforms.end()) {
                const int binding = static_cast<int>(compiler.get_decoration(resource.id, spv::DecorationBinding));
                programData.uniforms[name] = binding;
            }
        }

        for (const auto& resource : resources.sampled_images) {
            const std::string name = compiler.get_name(resource.id);
            if (!name.empty() && programData.uniforms.find(name) == programData.uniforms.end()) {
                const int binding = static_cast<int>(compiler.get_decoration(resource.id, spv::DecorationBinding));
                programData.uniforms[name] = binding;
            }
        }
    } catch (const std::exception& ex) {
        TraceLog(LogLevel::Warn, "VULKAN",
                 TextFormat("Failed to reflect %s shader resources: %s",
                            stageName, ex.what()));
    }
}

void StoreUniformValue(VkShaderProgramData& programData,
                      int locIndex,
                      int uniformType,
                      const void* value,
                      size_t bytesPerElement,
                      int count) {
    if (locIndex < 0 || value == nullptr || count <= 0) {
        return;
    }

    const size_t totalBytes = static_cast<size_t>(count) * bytesPerElement;
    auto& storage = programData.uniformValues[locIndex];
    storage.assign(static_cast<const uint8_t*>(value),
                   static_cast<const uint8_t*>(value) + totalBytes);
    programData.uniformTypes[locIndex] = uniformType;
}

} // namespace

void QuarkVkRenderer::BeginShaderMode(const Shader& shader) {
    if (shader.id != 0 && m_shaderPrograms.find(shader.id) != m_shaderPrograms.end()) {
        m_currentShaderProgramId = shader.id;
    }
}

void QuarkVkRenderer::EndShaderMode() {
    m_currentShaderProgramId = 0;
}

Shader QuarkVkRenderer::LoadShader(const char* vs, const char* fs) {
    std::string vsSource;
    std::string fsSource;

    if (vs && !ReadTextFile(vs, vsSource)) {
        TraceLog(LogLevel::Error, "VULKAN", TextFormat("Failed to open vertex shader file: %s", vs));
        return Shader{};
    }
    if (fs && !ReadTextFile(fs, fsSource)) {
        TraceLog(LogLevel::Error, "VULKAN", TextFormat("Failed to open fragment shader file: %s", fs));
        return Shader{};
    }

    return LoadShaderFromMemory(vsSource.empty() ? nullptr : vsSource.c_str(),
                                fsSource.empty() ? nullptr : fsSource.c_str());
}

Shader QuarkVkRenderer::LoadShaderFromMemory(const char* vs, const char* fs) {
    if (!vs && !fs) {
        return Shader{};
    }

    const uint32_t shaderId = m_nextShaderProgramId++;
    VkShaderProgramData programData{};

    try {
        if (vs) {
            std::vector<uint32_t> spirv;
            if (!CompileGlslToSpirv(vs, shaderc_glsl_vertex_shader, "vertex", spirv)) {
                return Shader{};
            }
            ReflectShaderResources(programData, spirv, "vertex");
            programData.vertexModule = CreateShaderModule(spirv);
        }

        if (fs) {
            std::vector<uint32_t> spirv;
            if (!CompileGlslToSpirv(fs, shaderc_glsl_fragment_shader, "fragment", spirv)) {
                if (programData.vertexModule != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(m_device, programData.vertexModule, nullptr);
                }
                return Shader{};
            }
            ReflectShaderResources(programData, spirv, "fragment");
            programData.fragmentModule = CreateShaderModule(spirv);
        }
    } catch (const std::exception& ex) {
        TraceLog(LogLevel::Error, "VULKAN", TextFormat("Shader compilation failed: %s", ex.what()));
        if (programData.vertexModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, programData.vertexModule, nullptr);
        }
        if (programData.fragmentModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, programData.fragmentModule, nullptr);
        }
        return Shader{};
    }

    programData.pipeline = CreatePipelineForRenderPass(m_renderPass,
                                                       programData.vertexModule,
                                                       programData.fragmentModule);
    m_shaderPrograms[shaderId] = std::move(programData);
    return Shader{shaderId};
}

void QuarkVkRenderer::UnloadShader(Shader& shader) {
    if (shader.id == 0) {
        return;
    }

    const auto it = m_shaderPrograms.find(shader.id);
    if (it != m_shaderPrograms.end()) {
        if (it->second.pipeline != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, it->second.pipeline, nullptr);
        }
        if (it->second.vertexModule != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, it->second.vertexModule, nullptr);
        }
        if (it->second.fragmentModule != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, it->second.fragmentModule, nullptr);
        }
        m_shaderPrograms.erase(it);
    }

    if (m_currentShaderProgramId == shader.id) {
        m_currentShaderProgramId = 0;
    }
    shader = Shader{};
}

bool QuarkVkRenderer::isShaderValid(Shader& shader) {
    return shader.id != 0 && m_shaderPrograms.find(shader.id) != m_shaderPrograms.end();
}

int QuarkVkRenderer::GetShaderLocation(const Shader& shader, const char* uniformName) {
    if (!uniformName || shader.id == 0) {
        return -1;
    }

    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return -1;
    }

    auto& locationMap = it->second.uniforms;
    const auto locIt = locationMap.find(uniformName);
    if (locIt != locationMap.end()) {
        return locIt->second;
    }

    const int newLocation = static_cast<int>(locationMap.size());
    locationMap.emplace(uniformName, newLocation);
    return newLocation;
}

int QuarkVkRenderer::GetShaderLocation(const Shader& shader, ShaderLocationIndex locIndex) {
    if (locIndex < SHADER_LOC_VERTEX_POSITION || locIndex >= SHADER_LOC_COUNT) {
        return -1;
    }
    return GetShaderLocation(shader, shaderLocationNames[locIndex]);
}

int QuarkVkRenderer::GetShaderAttributeLocation(const Shader& shader, const char* attribName) {
    if (!attribName || shader.id == 0) {
        return -1;
    }

    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return -1;
    }

    auto& locationMap = it->second.attributes;
    const auto locIt = locationMap.find(attribName);
    if (locIt != locationMap.end()) {
        return locIt->second;
    }

    const int newLocation = static_cast<int>(locationMap.size());
    locationMap.emplace(attribName, newLocation);
    return newLocation;
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, float value) {
    if (shader.id == 0 || locIndex < 0) {
        return;
    }
    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return;
    }
    StoreUniformValue(it->second, locIndex, SHADER_UNIFORM_FLOAT, &value, sizeof(float), 1);
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, int value) {
    if (shader.id == 0 || locIndex < 0) {
        return;
    }
    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return;
    }
    StoreUniformValue(it->second, locIndex, SHADER_UNIFORM_INT, &value, sizeof(int), 1);
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const Color& value) {
    if (shader.id == 0 || locIndex < 0) {
        return;
    }
    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return;
    }
    const float rgba[4] = {
        value.r / 255.0f,
        value.g / 255.0f,
        value.b / 255.0f,
        value.a / 255.0f
    };
    StoreUniformValue(it->second, locIndex, SHADER_UNIFORM_VEC4, rgba, sizeof(float), 4);
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const qc::Vec2& value) {
    if (shader.id == 0 || locIndex < 0) {
        return;
    }
    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return;
    }
    const float vec[2] = { value.x, value.y };
    StoreUniformValue(it->second, locIndex, SHADER_UNIFORM_VEC2, vec, sizeof(float), 2);
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const qc::Vec3& value) {
    if (shader.id == 0 || locIndex < 0) {
        return;
    }
    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return;
    }
    const float vec[3] = { value.x, value.y, value.z };
    StoreUniformValue(it->second, locIndex, SHADER_UNIFORM_VEC3, vec, sizeof(float), 3);
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const qc::Vec4& value) {
    if (shader.id == 0 || locIndex < 0) {
        return;
    }
    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return;
    }
    const float vec[4] = { value.x, value.y, value.z, value.w };
    StoreUniformValue(it->second, locIndex, SHADER_UNIFORM_VEC4, vec, sizeof(float), 4);
}

void QuarkVkRenderer::SetShaderValueMatrix(const Shader& shader, int locIndex, const float* mat) {
    if (shader.id == 0 || locIndex < 0 || mat == nullptr) {
        return;
    }
    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return;
    }
    StoreUniformValue(it->second, locIndex, SHADER_UNIFORM_FLOAT, mat, sizeof(float), 16);
}

void QuarkVkRenderer::SetShaderValueSampler(const Shader& shader, int locIndex, int textureUnit) {
    if (shader.id == 0 || locIndex < 0) {
        return;
    }
    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return;
    }
    StoreUniformValue(it->second, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureUnit, sizeof(int), 1);
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const void* value, int uniformType) {
    if (shader.id == 0 || locIndex < 0 || value == nullptr) {
        return;
    }
    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return;
    }

    switch (uniformType) {
        case SHADER_UNIFORM_FLOAT:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(float), 1);
            break;
        case SHADER_UNIFORM_VEC2:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(float), 2);
            break;
        case SHADER_UNIFORM_VEC3:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(float), 3);
            break;
        case SHADER_UNIFORM_VEC4:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(float), 4);
            break;
        case SHADER_UNIFORM_INT:
        case SHADER_UNIFORM_SAMPLER2D:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(int), 1);
            break;
        case SHADER_UNIFORM_IVEC2:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(int), 2);
            break;
        case SHADER_UNIFORM_IVEC3:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(int), 3);
            break;
        case SHADER_UNIFORM_IVEC4:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(int), 4);
            break;
        default:
            break;
    }
}

void QuarkVkRenderer::SetShaderValueV(const Shader& shader, int locIndex, const void* value, int uniformType, int count) {
    if (shader.id == 0 || locIndex < 0 || value == nullptr || count <= 0) {
        return;
    }
    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return;
    }

    switch (uniformType) {
        case SHADER_UNIFORM_FLOAT:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(float), count);
            break;
        case SHADER_UNIFORM_VEC2:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(float) * 2, count);
            break;
        case SHADER_UNIFORM_VEC3:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(float) * 3, count);
            break;
        case SHADER_UNIFORM_VEC4:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(float) * 4, count);
            break;
        case SHADER_UNIFORM_INT:
        case SHADER_UNIFORM_SAMPLER2D:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(int), count);
            break;
        case SHADER_UNIFORM_IVEC2:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(int) * 2, count);
            break;
        case SHADER_UNIFORM_IVEC3:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(int) * 3, count);
            break;
        case SHADER_UNIFORM_IVEC4:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(int) * 4, count);
            break;
        default:
            break;
    }
}

void QuarkVkRenderer::SetShaderValueMatrix(const Shader& shader, int locIndex, const Matrix& mat) {
    if (shader.id == 0 || locIndex < 0) {
        return;
    }
    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return;
    }
    const float* data = mat.m;
    StoreUniformValue(it->second, locIndex, SHADER_UNIFORM_FLOAT, data, sizeof(float), 16);
}

void QuarkVkRenderer::SetShaderValueTexture(const Shader& shader, int locIndex, const ITexture& texture) {
    if (shader.id == 0 || locIndex < 0) {
        return;
    }
    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return;
    }
    const int textureUnit = static_cast<int>(texture.id);
    StoreUniformValue(it->second, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureUnit, sizeof(int), 1);
}

void QuarkVkRenderer::SetShaderValueTextureUnit(const Shader& shader, int locIndex, const ITexture& texture, int textureUnit) {
    (void)texture;
    if (shader.id == 0 || locIndex < 0) {
        return;
    }
    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return;
    }
    StoreUniformValue(it->second, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureUnit, sizeof(int), 1);
}

} // namespace qc
