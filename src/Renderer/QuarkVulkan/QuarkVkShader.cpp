#include "QuarkVkRenderer.hpp"

#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

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

std::filesystem::path FindGlslangValidator() {
    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::array<std::filesystem::path, 4> candidates = {
        cwd / "external" / "Vulkan" / "lib" / "glslangValidator.exe",
        cwd / "external" / "Vulkan" / "lib" / "glslangValidator",
        std::filesystem::path("external") / "Vulkan" / "lib" / "glslangValidator.exe",
        std::filesystem::path("external") / "Vulkan" / "lib" / "glslangValidator"
    };

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
    }
    return {};
}

bool RunCompilerProcess(const std::filesystem::path& validator,
                        const std::filesystem::path& sourcePath,
                        const std::filesystem::path& outputPath,
                        const std::string& stageName) {
#if defined(_WIN32)
    const std::wstring validatorW = validator.wstring();
    const std::wstring sourceW = sourcePath.wstring();
    const std::wstring outputW = outputPath.wstring();
    const std::wstring stageW(stageName.begin(), stageName.end());
    std::wstring commandLine = L"\"" + validatorW + L"\" -V -S " + stageW + L" \"" + sourceW + L"\" -o \"" + outputW + L"\"";

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    if (!CreateProcessW(validatorW.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        TraceLog(LogLevel::Error, "VULKAN", TextFormat("Failed to launch glslangValidator: %lu", GetLastError()));
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return exitCode == 0;
#else
    std::ostringstream command;
    command << '"' << validator.string() << '"' << " -V -S " << stageName << " "
            << '"' << sourcePath.string() << '"' << " -o " << '"' << outputPath.string() << '"';
    return std::system(command.str().c_str()) == 0;
#endif
}

bool CompileGlslToSpirv(const std::string& source, const char* stageName, std::vector<uint32_t>& outSpirv) {
    const std::filesystem::path validator = FindGlslangValidator();
    if (validator.empty()) {
        TraceLog(LogLevel::Error, "VULKAN", "glslangValidator was not found in external/Vulkan/lib.");
        return false;
    }

    std::error_code ec;
    const std::filesystem::path tempBase = std::filesystem::temp_directory_path(ec);
    std::filesystem::path tempDir = tempBase / "quarkvkshader";
    for (int attempt = 0; attempt < 1000; ++attempt) {
        tempDir = tempBase / (std::string("quarkvkshader-") + std::to_string(std::rand()) + std::to_string(attempt));
        if (!std::filesystem::exists(tempDir, ec)) {
            break;
        }
    }
    std::filesystem::create_directories(tempDir, ec);
    if (ec) {
        TraceLog(LogLevel::Error, "VULKAN", "Failed to create temporary directory for shader compilation.");
        return false;
    }

    const std::filesystem::path sourcePath = tempDir / (std::string("shader.") + stageName + ".glsl");
    const std::filesystem::path outputPath = tempDir / "shader.spv";

    {
        std::ofstream sourceFile(sourcePath, std::ios::binary);
        if (!sourceFile.is_open()) {
            std::filesystem::remove_all(tempDir, ec);
            return false;
        }
        sourceFile << source;
    }

    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Compiling shader with validator: %s", validator.string().c_str()));
    const bool compiled = RunCompilerProcess(validator, sourcePath, outputPath, stageName);

    if (!compiled || !std::filesystem::exists(outputPath, ec)) {
        std::filesystem::remove_all(tempDir, ec);
        TraceLog(LogLevel::Error, "VULKAN", TextFormat("Failed to compile %s shader with glslangValidator.", stageName));
        return false;
    }

    std::ifstream binary(outputPath, std::ios::binary | std::ios::ate);
    if (!binary.is_open()) {
        std::filesystem::remove_all(tempDir, ec);
        return false;
    }

    const std::streamsize size = binary.tellg();
    binary.seekg(0);
    if (size <= 0) {
        std::filesystem::remove_all(tempDir, ec);
        return false;
    }

    std::vector<char> bytes(static_cast<size_t>(size));
    binary.read(bytes.data(), size);
    outSpirv.clear();
    outSpirv.resize(bytes.size() / sizeof(uint32_t));
    std::memcpy(outSpirv.data(), bytes.data(), bytes.size());

    std::filesystem::remove_all(tempDir, ec);
    return true;
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
            if (!CompileGlslToSpirv(vs, "vert", spirv)) {
                return Shader{};
            }
            programData.vertexModule = CreateShaderModule(spirv);
        }

        if (fs) {
            std::vector<uint32_t> spirv;
            if (!CompileGlslToSpirv(fs, "frag", spirv)) {
                if (programData.vertexModule != VK_NULL_HANDLE) {
                    vkDestroyShaderModule(m_device, programData.vertexModule, nullptr);
                }
                return Shader{};
            }
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

    m_shaderPrograms[shaderId] = std::move(programData);
    return Shader{shaderId};
}

void QuarkVkRenderer::UnloadShader(Shader& shader) {
    if (shader.id == 0) {
        return;
    }

    const auto it = m_shaderPrograms.find(shader.id);
    if (it != m_shaderPrograms.end()) {
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
    (void)shader; (void)locIndex; (void)value;
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, int value) {
    (void)shader; (void)locIndex; (void)value;
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const Color& value) {
    (void)shader; (void)locIndex; (void)value;
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const qc::Vec2& value) {
    (void)shader; (void)locIndex; (void)value;
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const qc::Vec3& value) {
    (void)shader; (void)locIndex; (void)value;
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const qc::Vec4& value) {
    (void)shader; (void)locIndex; (void)value;
}

void QuarkVkRenderer::SetShaderValueMatrix(const Shader& shader, int locIndex, const float* mat) {
    (void)shader; (void)locIndex; (void)mat;
}

void QuarkVkRenderer::SetShaderValueSampler(const Shader& shader, int locIndex, int textureUnit) {
    (void)shader; (void)locIndex; (void)textureUnit;
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const void* value, int uniformType) {
    (void)shader; (void)locIndex; (void)value; (void)uniformType;
}

void QuarkVkRenderer::SetShaderValueV(const Shader& shader, int locIndex, const void* value, int uniformType, int count) {
    (void)shader; (void)locIndex; (void)value; (void)uniformType; (void)count;
}

void QuarkVkRenderer::SetShaderValueMatrix(const Shader& shader, int locIndex, const Matrix& mat) {
    (void)shader; (void)locIndex; (void)mat;
}

void QuarkVkRenderer::SetShaderValueTexture(const Shader& shader, int locIndex, const ITexture& texture) {
    (void)shader; (void)locIndex; (void)texture;
}

} // namespace qc
