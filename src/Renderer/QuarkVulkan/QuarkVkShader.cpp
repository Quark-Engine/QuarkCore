#include "QuarkVkRenderer.hpp"

#include <SDL3/SDL_vulkan.h>
#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_glsl.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
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

int NormalizeIntValue(const void* data) {
    int intValue = 0;
    std::memcpy(&intValue, data, sizeof(int));
    float floatValue = 0.0f;
    std::memcpy(&floatValue, data, sizeof(float));
    if (floatValue >= -16384.0f && floatValue <= 16384.0f &&
        floatValue == static_cast<float>(static_cast<int>(floatValue))) {
        return static_cast<int>(floatValue);
    }
    return intValue;
}

bool CompileGlslToSpirv(const std::string& source,
                        shaderc_shader_kind shaderKind,
                        const char* stageName,
                        std::vector<uint32_t>& outSpirv) {
    TraceLog(LogLevel::Trace, "SHADER", TextFormat("[Vulkan] Compiling %s GLSL to SPIR-V (%zu bytes source)...", stageName, source.size()));

    static std::unordered_map<std::string, std::vector<uint32_t>> cache;
    const std::string cacheKey = std::string(stageName) + "\n" + source;
    const auto cached = cache.find(cacheKey);
    if (cached != cache.end()) {
        outSpirv = cached->second;
        return true;
    }

    uint64_t hash = 14695981039346656037ull;
    for (unsigned char character : cacheKey) {
        hash ^= character;
        hash *= 1099511628211ull;
    }

    std::error_code error;
    const std::filesystem::path cachePath =
        std::filesystem::temp_directory_path(error) / "QuarkCore" / "shader-cache" /
        ("user-" + std::to_string(hash) + ".spv");
    if (!error) {
        std::ifstream file(cachePath, std::ios::binary);
        uint32_t wordCount = 0;
        if (file && file.read(reinterpret_cast<char*>(&wordCount), sizeof(wordCount)) && wordCount > 0) {
            outSpirv.resize(wordCount);
            if (file.read(reinterpret_cast<char*>(outSpirv.data()),
                          static_cast<std::streamsize>(outSpirv.size() * sizeof(uint32_t)))) {
                cache.emplace(cacheKey, outSpirv);
                TraceLog(LogLevel::Trace, "SHADER", TextFormat("[Vulkan] Loaded user %s shader from cache", stageName));
                return true;
            }
            outSpirv.clear();
        }
    }

    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);

    const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
        source, shaderKind, "QuarkCore runtime shader", options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        TraceLog(LogLevel::Error, "SHADER",
                 TextFormat("[Vulkan] Failed to compile %s shader with Shaderc: %s",
                            stageName, result.GetErrorMessage().c_str()));
        return false;
    }

    outSpirv.assign(result.cbegin(), result.cend());
    if (!error) {
        std::error_code directoryError;
        std::filesystem::create_directories(cachePath.parent_path(), directoryError);
        std::ofstream file(cachePath, std::ios::binary | std::ios::trunc);
        const uint32_t wordCount = static_cast<uint32_t>(outSpirv.size());
        if (file && file.write(reinterpret_cast<const char*>(&wordCount), sizeof(wordCount))) {
            file.write(reinterpret_cast<const char*>(outSpirv.data()),
                       static_cast<std::streamsize>(outSpirv.size() * sizeof(uint32_t)));
        }
    }
    cache.emplace(cacheKey, outSpirv);
    TraceLog(LogLevel::Trace, "SHADER", TextFormat("[Vulkan] Compiled %s shader to %zu SPIR-V words (%zu bytes)",
        stageName, outSpirv.size(), outSpirv.size() * sizeof(uint32_t)));
    return !outSpirv.empty();
}

void ReflectShaderResources(VkShaderProgramData& programData,
                           const std::vector<uint32_t>& spirv,
                           const char* stageName) {
    if (spirv.empty()) return;

    try {
        spirv_cross::CompilerGLSL compiler(spirv);
        const auto resources = compiler.get_shader_resources();
        int attribCount = 0;
        int uniformCount = 0;

        for (const auto& resource : resources.stage_inputs) {
            const std::string name = compiler.get_name(resource.id);
            if (name.empty()) {
                continue;
            }
            const uint32_t location = compiler.get_decoration(resource.id, spv::DecorationLocation);
            programData.attributes[name] = static_cast<int>(location);
            attribCount++;
        }

        for (const auto& resource : resources.uniform_buffers) {
            const std::string name = compiler.get_name(resource.id);
            if (!name.empty() && programData.uniforms.find(name) == programData.uniforms.end()) {
                const int binding = static_cast<int>(compiler.get_decoration(resource.id, spv::DecorationBinding));
                programData.uniforms[name] = binding;
                uniformCount++;
            }
        }

        for (const auto& resource : resources.sampled_images) {
            const std::string name = compiler.get_name(resource.id);
            if (!name.empty() && programData.uniforms.find(name) == programData.uniforms.end()) {
                const int binding = static_cast<int>(compiler.get_decoration(resource.id, spv::DecorationBinding));
                programData.uniforms[name] = binding;
                uniformCount++;
            }
        }

        TraceLog(LogLevel::Trace, "SHADER", TextFormat("[Vulkan] Reflected %s stage: %d attributes, %d uniform/sampler bindings",
            stageName, attribCount, uniformCount));
    } catch (const std::exception& ex) {
        TraceLog(LogLevel::Warn, "SHADER",
                 TextFormat("[Vulkan] Failed to reflect %s shader resources: %s",
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
    TraceLog(LogLevel::Trace, "SHADER", TextFormat("[Vulkan] Loading shader files: VS='%s', FS='%s'",
        vs ? vs : "<none>", fs ? fs : "<none>"));
    std::string vsSource;
    std::string fsSource;

    if (vs && !ReadTextFile(vs, vsSource)) {
        TraceLog(LogLevel::Error, "SHADER", TextFormat("[Vulkan] Failed to open vertex shader file: %s", vs));
        return Shader{};
    }
    if (fs && !ReadTextFile(fs, fsSource)) {
        TraceLog(LogLevel::Error, "SHADER", TextFormat("[Vulkan] Failed to open fragment shader file: %s", fs));
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
        TraceLog(LogLevel::Error, "SHADER", TextFormat("[Vulkan] Shader compilation failed: %s", ex.what()));
        if (programData.vertexModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, programData.vertexModule, nullptr);
        }
        if (programData.fragmentModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, programData.fragmentModule, nullptr);
        }
        return Shader{};
    }

    programData.supports3D = programData.attributes.find("aNormal") != programData.attributes.end() ||
                             programData.uniforms.find("matrices") != programData.uniforms.end();

    if (programData.supports3D) {
        programData.pipeline3D = Create3DPipelineForRenderPass(m_renderPass,
                                                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                               programData.vertexModule,
                                                               programData.fragmentModule);
        if (m_offscreenRenderPass != VK_NULL_HANDLE) {
            programData.pipeline3DOffscreen = Create3DPipelineForRenderPass(m_offscreenRenderPass,
                                                                            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                                            programData.vertexModule,
                                                                            programData.fragmentModule);
        }
        if (Allocate3DDescriptorSet(programData.descriptorSet3D)) {
            const auto whiteIt = m_textures.find(m_whiteTextureId);
            if (whiteIt != m_textures.end()) {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = whiteIt->second.view;
                imageInfo.sampler = whiteIt->second.sampler;
                std::array<VkDescriptorImageInfo, 4> shadowImages{ imageInfo, imageInfo, imageInfo, imageInfo };
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = m_3DDummyBuffer;
                bufferInfo.offset = 0;
                bufferInfo.range = 192;
                VkDescriptorBufferInfo shadowBufferInfo = bufferInfo;
                shadowBufferInfo.offset = 512;
                shadowBufferInfo.range = 512;
                VkDescriptorBufferInfo lightBufferInfo = bufferInfo;
                lightBufferInfo.offset = 1024;
                lightBufferInfo.range = 320;
                std::array<VkWriteDescriptorSet, 5> writes{};
                writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, programData.descriptorSet3D, 0, 0, 1,
                              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bufferInfo, nullptr };
                writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, programData.descriptorSet3D, 1, 0, 1,
                              VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfo, nullptr, nullptr };
                writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, programData.descriptorSet3D, 2, 0, 4,
                              VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, shadowImages.data(), nullptr, nullptr };
                writes[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, programData.descriptorSet3D, 3, 0, 1,
                              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &shadowBufferInfo, nullptr };
                writes[4] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, programData.descriptorSet3D, 4, 0, 1,
                              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &lightBufferInfo, nullptr };
                vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
            }
        }
    } else {
        programData.pipeline = CreatePipelineForRenderPass(m_renderPass,
                                                           programData.vertexModule,
                                                           programData.fragmentModule);
    }
    TraceLog(LogLevel::Info, "SHADER", TextFormat("[Vulkan] Shader program created successfully (ID: %u, Attributes: %zu, Uniforms: %zu, Pipeline: %p)",
        shaderId, programData.attributes.size(), programData.uniforms.size(), (void*)programData.pipeline));
    m_shaderPrograms[shaderId] = std::move(programData);
    return Shader{shaderId};
}

void QuarkVkRenderer::UnloadShader(Shader& shader) {
    if (shader.id == 0) {
        return;
    }

    const auto it = m_shaderPrograms.find(shader.id);
    if (it != m_shaderPrograms.end()) {
        TraceLog(LogLevel::Info, "SHADER", TextFormat("[Vulkan] Shader program unloaded (ID: %u)", shader.id));
        if (it->second.pipeline != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, it->second.pipeline, nullptr);
        }
        if (it->second.pipeline3D != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, it->second.pipeline3D, nullptr);
        }
        if (it->second.pipeline3DOffscreen != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, it->second.pipeline3DOffscreen, nullptr);
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
        it->second.uniformNames[locIt->second] = uniformName;
        return locIt->second;
    }

    const int newLocation = static_cast<int>(locationMap.size());
    locationMap.emplace(uniformName, newLocation);
    it->second.uniformNames[newLocation] = uniformName;
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
    Write3DShaderUniform(shader.id, locIndex, &value, sizeof(float));
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
    Write3DShaderUniform(shader.id, locIndex, value);
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
    Write3DShaderUniform(shader.id, locIndex, rgba, sizeof(rgba));
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
    Write3DShaderUniform(shader.id, locIndex, vec, sizeof(vec));
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
    Write3DShaderUniform(shader.id, locIndex, vec, sizeof(vec));
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
    Write3DShaderUniform(shader.id, locIndex, vec, sizeof(vec));
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
    Write3DShaderUniform(shader.id, locIndex, mat, sizeof(float) * 16);
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
    Update3DDescriptorSet(it->second);
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const void* value, int uniformType) {
    if (shader.id == 0 || locIndex < 0 || value == nullptr) {
        return;
    }
    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return;
    }

    size_t writtenBytes = 0;
    switch (uniformType) {
        case SHADER_UNIFORM_FLOAT:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(float), 1);
            writtenBytes = sizeof(float);
            break;
        case SHADER_UNIFORM_VEC2:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(float), 2);
            writtenBytes = sizeof(float) * 2;
            break;
        case SHADER_UNIFORM_VEC3:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(float), 3);
            writtenBytes = sizeof(float) * 3;
            break;
        case SHADER_UNIFORM_VEC4:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(float), 4);
            writtenBytes = sizeof(float) * 4;
            break;
        case SHADER_UNIFORM_INT:
        case SHADER_UNIFORM_SAMPLER2D:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(int), 1);
            writtenBytes = sizeof(float);
            break;
        case SHADER_UNIFORM_IVEC2:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(int), 2);
            writtenBytes = sizeof(float) * 2;
            break;
        case SHADER_UNIFORM_IVEC3:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(int), 3);
            writtenBytes = sizeof(float) * 3;
            break;
        case SHADER_UNIFORM_IVEC4:
            StoreUniformValue(it->second, locIndex, uniformType, value, sizeof(int), 4);
            writtenBytes = sizeof(float) * 4;
            break;
        default:
            break;
    }
    if (writtenBytes > 0) {
        Write3DShaderUniform(shader.id, locIndex, value, writtenBytes);
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
    Write3DShaderUniform(shader.id, locIndex, data, sizeof(float) * 16);
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
    if (shader.id == 0 || locIndex < 0) {
        return;
    }
    const auto it = m_shaderPrograms.find(shader.id);
    if (it == m_shaderPrograms.end()) {
        return;
    }
    const int textureId = texture.valid ? static_cast<int>(texture.id) : textureUnit;
    StoreUniformValue(it->second, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureId, sizeof(int), 1);
    Update3DDescriptorSet(it->second);
}

void QuarkVkRenderer::Update3DDescriptorSet(VkShaderProgramData& program) {
    if (program.descriptorSet3D == VK_NULL_HANDLE) return;

    const auto whiteIt = m_textures.find(m_whiteTextureId);
    if (whiteIt == m_textures.end()) return;

    const VkTextureData& white = whiteIt->second;
    std::array<VkDescriptorImageInfo, 4> shadowImages{};
    for (auto& image : shadowImages) {
        image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image.imageView = white.view;
        image.sampler = white.sampler;
    }
    VkDescriptorImageInfo albedo = shadowImages[0];

    const auto textureFromUniform = [&](const char* name, VkDescriptorImageInfo& output) {
        const auto locationIt = program.uniforms.find(name);
        if (locationIt == program.uniforms.end()) return;
        const auto valueIt = program.uniformValues.find(locationIt->second);
        if (valueIt == program.uniformValues.end() || valueIt->second.size() < sizeof(int)) return;
        int textureId = 0;
        std::memcpy(&textureId, valueIt->second.data(), sizeof(textureId));
        const auto textureIt = m_textures.find(static_cast<uint32_t>(textureId));
        if (textureIt != m_textures.end()) {
            output.imageView = textureIt->second.view;
            output.sampler = textureIt->second.sampler;
        }
    };

    textureFromUniform("uTexture", albedo);
    std::array<VkDescriptorImageInfo, 6> materialImages{};
    materialImages.fill(albedo);
    const std::array<const char*, 6> materialNames = {
        "albedo", "metalness", "normal", "roughness", "occlusion", "emission"
    };
    for (size_t i = 0; i < materialNames.size(); ++i) {
        textureFromUniform(materialNames[i], materialImages[i]);
    }
    const auto shadowsIt = program.uniforms.find("shadowMaps");
    if (shadowsIt != program.uniforms.end()) {
        const auto valuesIt = program.uniformValues.find(shadowsIt->second);
        if (valuesIt != program.uniformValues.end()) {
            const size_t count = std::min<size_t>(4, valuesIt->second.size() / sizeof(int));
            for (size_t i = 0; i < count; ++i) {
                int textureId = 0;
                std::memcpy(&textureId, valuesIt->second.data() + i * sizeof(int), sizeof(textureId));
                const auto textureIt = m_textures.find(static_cast<uint32_t>(textureId));
                if (textureIt != m_textures.end()) {
                    shadowImages[i].imageView = textureIt->second.view;
                    shadowImages[i].sampler = textureIt->second.sampler;
                }
            }
        }
    }

    std::array<VkWriteDescriptorSet, 8> writes{};
    writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, program.descriptorSet3D,
                  1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &albedo, nullptr, nullptr };
    writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, program.descriptorSet3D,
                  2, 0, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, shadowImages.data(), nullptr, nullptr };
    for (uint32_t i = 0; i < 6; ++i) {
        writes[2 + i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, program.descriptorSet3D,
                          5 + i, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                          &materialImages[i], nullptr, nullptr };
    }
    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void QuarkVkRenderer::Write3DShaderUniform(uint32_t shaderId, int locIndex, float value) {
    Write3DShaderUniform(shaderId, locIndex, &value, sizeof(float));
}

void QuarkVkRenderer::Write3DShaderUniform(uint32_t shaderId, int locIndex, int value) {
    Write3DShaderUniform(shaderId, locIndex, &value, sizeof(int));
}

void QuarkVkRenderer::Write3DShaderUniform(uint32_t shaderId, int locIndex, const void* data, size_t bytes) {
    if (shaderId == 0 || locIndex < 0 || data == nullptr || bytes == 0 || m_3DDummyMapped == nullptr) {
        return;
    }
    const auto it = m_shaderPrograms.find(shaderId);
    if (it == m_shaderPrograms.end()) return;
    const auto nameIt = it->second.uniformNames.find(locIndex);
    if (nameIt == it->second.uniformNames.end()) return;
    const std::string& name = nameIt->second;

    const auto indexOf = [&name](const char* prefix, int& outIndex) -> bool {
        const std::string p(prefix);
        if (name.rfind(p, 0) != 0) return false;
        const size_t br = name.find('[');
        if (br == std::string::npos) return false;
        const size_t close = name.find(']', br);
        if (close == std::string::npos) return false;
        outIndex = std::atoi(name.c_str() + br + 1);
        return true;
    };

    int index = -1;
    if (indexOf("lightViews", index) && index >= 0 && index < 4 && bytes >= sizeof(float) * 16) {
        std::memcpy(static_cast<char*>(m_3DDummyMapped) + 512 + static_cast<size_t>(index) * 64,
                    data, sizeof(float) * 16);
        return;
    }
    if (indexOf("lightProjections", index) && index >= 0 && index < 4 && bytes >= sizeof(float) * 16) {
        std::memcpy(static_cast<char*>(m_3DDummyMapped) + 512 + static_cast<size_t>(index + 4) * 64,
                    data, sizeof(float) * 16);
        return;
    }

    if (name == "ambient" && bytes >= sizeof(float) * 4) {
        const float* f = static_cast<const float*>(data);
        float* lightData = static_cast<float*>(static_cast<void*>(static_cast<char*>(m_3DDummyMapped) + 1024));
        std::memcpy(lightData + 0, f, sizeof(float) * 4);
        return;
    }
    if (name == "colDiffuse" && bytes >= sizeof(float) * 4) {
        const float* f = static_cast<const float*>(data);
        float* lightData = static_cast<float*>(static_cast<void*>(static_cast<char*>(m_3DDummyMapped) + 1024));
        std::memcpy(lightData + 4, f, sizeof(float) * 4);
        return;
    }
    if (name == "viewPos" && bytes >= sizeof(float) * 3) {
        const float* f = static_cast<const float*>(data);
        float* lightData = static_cast<float*>(static_cast<void*>(static_cast<char*>(m_3DDummyMapped) + 1024));
        lightData[8] = f[0];
        lightData[9] = f[1];
        lightData[10] = f[2];
        lightData[11] = 1.0f;
        m_viewPos = Vec3{ f[0], f[1], f[2] };
        return;
    }

    if (name.rfind("lights[", 0) == 0 && indexOf("lights[", index) && index >= 0 && index < 4) {
        const size_t close = name.find(']');
        if (close == std::string::npos) return;
        const std::string field = name.substr(close + 2);
        float* lightData = static_cast<float*>(static_cast<void*>(static_cast<char*>(m_3DDummyMapped) + 1024));
        float* slot = lightData + 12 + static_cast<size_t>(index) * 16;

        if (field == "position") {
            const float* f = static_cast<const float*>(data);
            slot[0] = f[0]; slot[1] = f[1]; slot[2] = f[2]; slot[3] = 1.0f;
            m_lights[static_cast<size_t>(index)].position = Vec3{ f[0], f[1], f[2] };
        } else if (field == "target") {
            const float* f = static_cast<const float*>(data);
            slot[4] = f[0]; slot[5] = f[1]; slot[6] = f[2]; slot[7] = 1.0f;
            m_lights[static_cast<size_t>(index)].target = Vec3{ f[0], f[1], f[2] };
        } else if (field == "color") {
            const float* f = static_cast<const float*>(data);
            slot[8] = f[0]; slot[9] = f[1]; slot[10] = f[2]; slot[11] = 1.0f;
            m_lights[static_cast<size_t>(index)].color = Vec3{ f[0], f[1], f[2] };
        } else if (field == "attenuation") {
            slot[12] = *static_cast<const float*>(data);
            m_lights[static_cast<size_t>(index)].attenuation = slot[12];
        } else if (field == "enabled") {
            const int enabledValue = NormalizeIntValue(data);
            std::memcpy(slot + 13, &enabledValue, sizeof(int));
            m_lights[static_cast<size_t>(index)].enabled = enabledValue != 0;
            m_3DLightEnabled[static_cast<size_t>(index)] = enabledValue != 0;
        } else if (field == "type") {
            const int typeValue = NormalizeIntValue(data);
            std::memcpy(slot + 14, &typeValue, sizeof(int));
            m_lights[static_cast<size_t>(index)].type = typeValue;
        }
        return;
    }
}

} // namespace qc
