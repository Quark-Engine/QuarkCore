#include "QuarkVkShaderCompiler.hpp"

#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_glsl.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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

} // namespace

void QuarkVkShaderCompiler::Initialize(VkDevice device) {
    m_device = device;
}

void QuarkVkShaderCompiler::Shutdown() {
    for (auto& [id, program] : m_programs) {
        (void)id;
        DestroyProgramData(program);
    }
    m_programs.clear();
    m_currentProgramId = 0;
    m_device = VK_NULL_HANDLE;
}

bool QuarkVkShaderCompiler::IsInitialized() const {
    return m_device != VK_NULL_HANDLE;
}

uint32_t QuarkVkShaderCompiler::LoadProgram(const char* vsFile, const char* fsFile) {
    TraceLog(LogLevel::Trace, "SHADER", TextFormat("[Vulkan] Loading shader files: VS='%s', FS='%s'",
        vsFile ? vsFile : "<none>", fsFile ? fsFile : "<none>"));

    std::string vsSource;
    std::string fsSource;

    if (vsFile && !ReadTextFile(vsFile, vsSource)) {
        TraceLog(LogLevel::Error, "SHADER", TextFormat("[Vulkan] Failed to open vertex shader file: %s", vsFile));
        return 0;
    }
    if (fsFile && !ReadTextFile(fsFile, fsSource)) {
        TraceLog(LogLevel::Error, "SHADER", TextFormat("[Vulkan] Failed to open fragment shader file: %s", fsFile));
        return 0;
    }

    return CreateProgram(vsSource.empty() ? nullptr : vsSource.c_str(),
                         fsSource.empty() ? nullptr : fsSource.c_str());
}

uint32_t QuarkVkShaderCompiler::CreateProgram(const char* vs, const char* fs) {
    if (!vs && !fs) {
        return 0;
    }

    const uint32_t shaderId = m_nextProgramId++;
    VkShaderProgramData programData{};

    try {
        if (vs) {
            std::vector<uint32_t> spirv;
            if (!CompileGlslToSpirv(vs, shaderc_glsl_vertex_shader, "vertex", spirv)) {
                return 0;
            }
            ReflectShaderResources(programData, spirv, "vertex");
            programData.vertexModule = CreateModule(spirv);
        }

        if (fs) {
            std::vector<uint32_t> spirv;
            if (!CompileGlslToSpirv(fs, shaderc_glsl_fragment_shader, "fragment", spirv)) {
                if (programData.vertexModule != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(m_device, programData.vertexModule, nullptr);
                }
                return 0;
            }
            ReflectShaderResources(programData, spirv, "fragment");
            programData.fragmentModule = CreateModule(spirv);
        }
    } catch (const std::exception& ex) {
        TraceLog(LogLevel::Error, "SHADER", TextFormat("[Vulkan] Shader compilation failed: %s", ex.what()));
        if (programData.vertexModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, programData.vertexModule, nullptr);
        }
        if (programData.fragmentModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, programData.fragmentModule, nullptr);
        }
        return 0;
    }

    programData.supports3D = programData.attributes.find("aNormal") != programData.attributes.end() ||
                             programData.uniforms.find("matrices") != programData.uniforms.end();

    m_programs[shaderId] = std::move(programData);
    return shaderId;
}

void QuarkVkShaderCompiler::DestroyProgram(uint32_t programId) {
    const auto it = m_programs.find(programId);
    if (it == m_programs.end()) {
        return;
    }

    TraceLog(LogLevel::Info, "SHADER", TextFormat("[Vulkan] Shader program unloaded (ID: %u)", programId));
    DestroyProgramData(it->second);
    m_programs.erase(it);

    if (m_currentProgramId == programId) {
        m_currentProgramId = 0;
    }
}

void QuarkVkShaderCompiler::DestroyProgramData(VkShaderProgramData& program) {
    if (m_device == VK_NULL_HANDLE) {
        return;
    }

    if (program.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, program.pipeline, nullptr);
        program.pipeline = VK_NULL_HANDLE;
    }
    if (program.pipeline3D != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, program.pipeline3D, nullptr);
        program.pipeline3D = VK_NULL_HANDLE;
    }
    if (program.pipeline3DOffscreen != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, program.pipeline3DOffscreen, nullptr);
        program.pipeline3DOffscreen = VK_NULL_HANDLE;
    }
    if (program.vertexModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_device, program.vertexModule, nullptr);
        program.vertexModule = VK_NULL_HANDLE;
    }
    if (program.fragmentModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(m_device, program.fragmentModule, nullptr);
        program.fragmentModule = VK_NULL_HANDLE;
    }
}

void QuarkVkShaderCompiler::FreePipelines() {
    for (auto& [id, program] : m_programs) {
        (void)id;
        if (program.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, program.pipeline, nullptr);
            program.pipeline = VK_NULL_HANDLE;
        }
        if (program.pipeline3D != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, program.pipeline3D, nullptr);
            program.pipeline3D = VK_NULL_HANDLE;
        }
        if (program.pipeline3DOffscreen != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, program.pipeline3DOffscreen, nullptr);
            program.pipeline3DOffscreen = VK_NULL_HANDLE;
        }
    }
}

bool QuarkVkShaderCompiler::IsProgramValid(uint32_t programId) const {
    return programId != 0 && m_programs.find(programId) != m_programs.end();
}

const VkShaderProgramData* QuarkVkShaderCompiler::GetProgram(uint32_t programId) const {
    const auto it = m_programs.find(programId);
    return it != m_programs.end() ? &it->second : nullptr;
}

VkShaderProgramData* QuarkVkShaderCompiler::GetProgram(uint32_t programId) {
    const auto it = m_programs.find(programId);
    return it != m_programs.end() ? &it->second : nullptr;
}

uint32_t QuarkVkShaderCompiler::CurrentProgramId() const {
    return m_currentProgramId;
}

void QuarkVkShaderCompiler::SetCurrentProgram(uint32_t programId) {
    m_currentProgramId = programId;
}

void QuarkVkShaderCompiler::ClearCurrentProgram() {
    m_currentProgramId = 0;
}

int QuarkVkShaderCompiler::GetUniformLocation(uint32_t programId, const char* uniformName) {
    if (!uniformName || programId == 0) {
        return -1;
    }

    VkShaderProgramData* program = GetProgram(programId);
    if (program == nullptr) {
        return -1;
    }

    auto& locationMap = program->uniforms;
    const auto locIt = locationMap.find(uniformName);
    if (locIt != locationMap.end()) {
        program->uniformNames[locIt->second] = uniformName;
        return locIt->second;
    }

    const int newLocation = static_cast<int>(locationMap.size());
    locationMap.emplace(uniformName, newLocation);
    program->uniformNames[newLocation] = uniformName;
    return newLocation;
}

int QuarkVkShaderCompiler::GetUniformLocation(uint32_t programId, ShaderLocationIndex locIndex) {
    if (locIndex < SHADER_LOC_VERTEX_POSITION || locIndex >= SHADER_LOC_COUNT) {
        return -1;
    }
    return GetUniformLocation(programId, shaderLocationNames[locIndex]);
}

int QuarkVkShaderCompiler::GetAttributeLocation(uint32_t programId, const char* attribName) {
    if (!attribName || programId == 0) {
        return -1;
    }

    VkShaderProgramData* program = GetProgram(programId);
    if (program == nullptr) {
        return -1;
    }

    auto& locationMap = program->attributes;
    const auto locIt = locationMap.find(attribName);
    if (locIt != locationMap.end()) {
        return locIt->second;
    }

    const int newLocation = static_cast<int>(locationMap.size());
    locationMap.emplace(attribName, newLocation);
    return newLocation;
}

void QuarkVkShaderCompiler::StoreUniformValue(uint32_t programId,
                                              int locIndex,
                                              int uniformType,
                                              const void* value,
                                              size_t bytesPerElement,
                                              int count) {
    VkShaderProgramData* program = GetProgram(programId);
    if (program == nullptr) {
        return;
    }
    if (locIndex < 0 || value == nullptr || count <= 0) {
        return;
    }

    const size_t totalBytes = static_cast<size_t>(count) * bytesPerElement;
    auto& storage = program->uniformValues[locIndex];
    storage.assign(static_cast<const uint8_t*>(value),
                   static_cast<const uint8_t*>(value) + totalBytes);
    program->uniformTypes[locIndex] = uniformType;
}

VkShaderModule QuarkVkShaderCompiler::CreateModule(const std::vector<uint32_t>& spirv) const {
    if (m_device == VK_NULL_HANDLE || spirv.empty()) {
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode    = spirv.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        TraceLog(LogLevel::Error, "VULKAN", "[Vulkan] Failed to create Vulkan shader module.");
        throw std::runtime_error("Failed to create Vulkan shader module.");
    }
    TraceLog(LogLevel::Trace, "SHADER", TextFormat("[Vulkan] Created VkShaderModule (%zu words / %zu bytes, Handle: %p)",
        spirv.size(), createInfo.codeSize, (void*)module));
    return module;
}

} // namespace qc