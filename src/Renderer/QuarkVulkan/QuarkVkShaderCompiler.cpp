#include <spirv_cross/spirv_glsl.hpp>

#include "QuarkVkShaderCompiler.hpp"

#include <shaderc/shaderc.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace qci
{
    namespace
    {
        static const char* s_apShaderLocationNames[SHADER_LOC_COUNT] =
            {
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
                "instanceTransform"};

        bool ReadTextFile(const char* pPath, std::string& out)
        {
            if (!pPath)
            {
                return false;
            }
            std::ifstream file(pPath, std::ios::in | std::ios::binary);
            if (!file.is_open())
            {
                return false;
            }
            out.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            return true;
        }

        bool CompileGlslToSpirv(const std::string& source,
                                shaderc_shader_kind shaderKind,
                                const char* pStageName,
                                std::vector<uint32_t>& vOutSpirv)
        {
            TraceLog(LogLevel::Trace, "SHADER", TextFormat("[Vulkan] Compiling %s GLSL to SPIR-V (%zu bytes source)...", pStageName, source.size()));

            static std::unordered_map<std::string, std::vector<uint32_t>> s_Cache;
            const std::string cacheKey = std::string(pStageName) + "\n" + source;
            const auto cached = s_Cache.find(cacheKey);
            if (cached != s_Cache.end())
            {
                vOutSpirv = cached->second;
                return true;
            }

            uint64_t hash = 14695981039346656037ull;
            for (unsigned char character : cacheKey)
            {
                hash ^= character;
                hash *= 1099511628211ull;
            }

            std::error_code error;
            const std::filesystem::path cachePath =
                std::filesystem::temp_directory_path(error) / "QuarkCore" / "shader-cache" /
                ("user-" + std::to_string(hash) + ".spv");
            if (!error)
            {
                std::ifstream file(cachePath, std::ios::binary);
                uint32_t wordCount = 0;
                if (file && file.read(reinterpret_cast<char*>(&wordCount), sizeof(wordCount)) && wordCount > 0)
                {
                    vOutSpirv.resize(wordCount);
                    if (file.read(reinterpret_cast<char*>(vOutSpirv.data()),
                                  static_cast<std::streamsize>(vOutSpirv.size() * sizeof(uint32_t))))
                    {
                        s_Cache.emplace(cacheKey, vOutSpirv);
                        TraceLog(LogLevel::Trace, "SHADER", TextFormat("[Vulkan] Loaded user %s shader from cache", pStageName));
                        return true;
                    }
                    vOutSpirv.clear();
                }
            }

            shaderc::Compiler compiler;
            shaderc::CompileOptions options;
            options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);

            const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
                source, shaderKind, "QuarkCore runtime shader", options);
            if (result.GetCompilationStatus() != shaderc_compilation_status_success)
            {
                TraceLog(LogLevel::Error, "SHADER",
                         TextFormat("[Vulkan] Failed to compile %s shader with Shaderc: %s",
                                    pStageName, result.GetErrorMessage().c_str()));
                return false;
            }

            vOutSpirv.assign(result.cbegin(), result.cend());
            if (!error)
            {
                std::error_code directoryError;
                std::filesystem::create_directories(cachePath.parent_path(), directoryError);
                std::ofstream file(cachePath, std::ios::binary | std::ios::trunc);
                const uint32_t wordCount = static_cast<uint32_t>(vOutSpirv.size());
                if (file && file.write(reinterpret_cast<const char*>(&wordCount), sizeof(wordCount)))
                {
                    file.write(reinterpret_cast<const char*>(vOutSpirv.data()),
                               static_cast<std::streamsize>(vOutSpirv.size() * sizeof(uint32_t)));
                }
            }
            s_Cache.emplace(cacheKey, vOutSpirv);
            TraceLog(LogLevel::Trace, "SHADER", TextFormat("[Vulkan] Compiled %s shader to %zu SPIR-V words (%zu bytes)", pStageName, vOutSpirv.size(), vOutSpirv.size() * sizeof(uint32_t)));
            return !vOutSpirv.empty();
        }

        void ReflectShaderResources(VkShaderProgramData& programData,
                                    const std::vector<uint32_t>& vSpirv,
                                    const char* pStageName)
        {
            if (vSpirv.empty())
            {
                return;
            }

            try
            {
                spirv_cross::CompilerGLSL compiler(vSpirv);
                const auto resources = compiler.get_shader_resources();
                int attribCount = 0;
                int uniformCount = 0;

                for (const auto& resource : resources.stage_inputs)
                {
                    const std::string name = compiler.get_name(resource.id);
                    if (name.empty())
                    {
                        continue;
                    }
                    const uint32_t location = compiler.get_decoration(resource.id, spv::DecorationLocation);
                    programData.m_Attributes[name] = static_cast<int>(location);
                    attribCount++;
                }

                for (const auto& resource : resources.uniform_buffers)
                {
                    const std::string name = compiler.get_name(resource.id);
                    if (!name.empty() && programData.m_Uniforms.find(name) == programData.m_Uniforms.end())
                    {
                        const int binding = static_cast<int>(compiler.get_decoration(resource.id, spv::DecorationBinding));
                        programData.m_Uniforms[name] = binding;
                        uniformCount++;
                    }
                }

                for (const auto& resource : resources.sampled_images)
                {
                    const std::string name = compiler.get_name(resource.id);
                    if (!name.empty() && programData.m_Uniforms.find(name) == programData.m_Uniforms.end())
                    {
                        const int binding = static_cast<int>(compiler.get_decoration(resource.id, spv::DecorationBinding));
                        programData.m_Uniforms[name] = binding;
                        uniformCount++;
                    }
                }

                TraceLog(LogLevel::Trace, "SHADER", TextFormat("[Vulkan] Reflected %s stage: %d attributes, %d uniform/sampler bindings", pStageName, attribCount, uniformCount));
            }
            catch (const std::exception& ex)
            {
                TraceLog(LogLevel::Warn, "SHADER",
                         TextFormat("[Vulkan] Failed to reflect %s shader resources: %s",
                                    pStageName, ex.what()));
            }
        }
    } // anonymous

    void QuarkVkShaderCompiler::Initialize(VkDevice device)
    {
        m_Device = device;
    }

    void QuarkVkShaderCompiler::Shutdown()
    {
        for (auto& [id, program] : m_Programs)
        {
            (void)id;
            DestroyProgramData(program);
        }
        m_Programs.clear();
        m_CurrentProgramId = 0;
        m_Device = VK_NULL_HANDLE;
    }

    bool QuarkVkShaderCompiler::IsInitialized() const
    {
        return m_Device != VK_NULL_HANDLE;
    }

    uint32_t QuarkVkShaderCompiler::LoadProgram(const char* pVsFile, const char* pFsFile)
    {
        TraceLog(LogLevel::Trace, "SHADER", TextFormat("[Vulkan] Loading shader files: VS='%s', FS='%s'", pVsFile ? pVsFile : "<none>", pFsFile ? pFsFile : "<none>"));

        std::string vsSource;
        std::string fsSource;

        if (pVsFile && !ReadTextFile(pVsFile, vsSource))
        {
            TraceLog(LogLevel::Error, "SHADER", TextFormat("[Vulkan] Failed to open vertex shader file: %s", pVsFile));
            return 0;
        }
        if (pFsFile && !ReadTextFile(pFsFile, fsSource))
        {
            TraceLog(LogLevel::Error, "SHADER", TextFormat("[Vulkan] Failed to open fragment shader file: %s", pFsFile));
            return 0;
        }

        return CreateProgram(vsSource.empty() ? nullptr : vsSource.c_str(),
                             fsSource.empty() ? nullptr : fsSource.c_str());
    }

    uint32_t QuarkVkShaderCompiler::CreateProgram(const char* pVsSource, const char* pFsSource)
    {
        if (!pVsSource && !pFsSource)
        {
            return 0;
        }

        const uint32_t shaderId = m_NextProgramId++;
        VkShaderProgramData programData{};
        programData.m_SupportsGpuMeshTransform = pVsSource &&
                                               std::string(pVsSource).find("QUARK_VULKAN_GPU_MESH_TRANSFORM") != std::string::npos;

        try
        {
            if (pVsSource)
            {
                std::vector<uint32_t> vSpirv;
                if (!CompileGlslToSpirv(pVsSource, shaderc_glsl_vertex_shader, "vertex", vSpirv))
                {
                    return 0;
                }
                ReflectShaderResources(programData, vSpirv, "vertex");
                programData.m_VertexModule = CreateModule(vSpirv);
            }

            if (pFsSource)
            {
                std::vector<uint32_t> vSpirv;
                if (!CompileGlslToSpirv(pFsSource, shaderc_glsl_fragment_shader, "fragment", vSpirv))
                {
                    if (programData.m_VertexModule != VK_NULL_HANDLE)
                    {
                        vkDestroyShaderModule(m_Device, programData.m_VertexModule, nullptr);
                    }
                    return 0;
                }
                ReflectShaderResources(programData, vSpirv, "fragment");
                programData.m_FragmentModule = CreateModule(vSpirv);
            }
        }
        catch (const std::exception& ex)
        {
            TraceLog(LogLevel::Error, "SHADER", TextFormat("[Vulkan] Shader compilation failed: %s", ex.what()));
            if (programData.m_VertexModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(m_Device, programData.m_VertexModule, nullptr);
            }
            if (programData.m_FragmentModule != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(m_Device, programData.m_FragmentModule, nullptr);
            }
            return 0;
        }

        programData.m_Supports3D = programData.m_Attributes.find("aNormal") != programData.m_Attributes.end() ||
                                 programData.m_Uniforms.find("matrices") != programData.m_Uniforms.end();

        m_Programs[shaderId] = std::move(programData);
        return shaderId;
    }

    void QuarkVkShaderCompiler::DestroyProgram(uint32_t programId)
    {
        const auto it = m_Programs.find(programId);
        if (it == m_Programs.end())
        {
            return;
        }

        TraceLog(LogLevel::Info, "SHADER", TextFormat("[Vulkan] Shader program unloaded (ID: %u)", programId));
        DestroyProgramData(it->second);
        m_Programs.erase(it);

        if (m_CurrentProgramId == programId)
        {
            m_CurrentProgramId = 0;
        }
    }

    void QuarkVkShaderCompiler::DestroyProgramData(VkShaderProgramData& program)
    {
        if (m_Device == VK_NULL_HANDLE)
        {
            return;
        }

        if (program.m_Pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, program.m_Pipeline, nullptr);
            program.m_Pipeline = VK_NULL_HANDLE;
        }
        if (program.m_PipelineOffscreen != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, program.m_PipelineOffscreen, nullptr);
            program.m_PipelineOffscreen = VK_NULL_HANDLE;
        }
        if (program.m_Pipeline3D != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, program.m_Pipeline3D, nullptr);
            program.m_Pipeline3D = VK_NULL_HANDLE;
        }
        if (program.m_Pipeline3DCulled != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, program.m_Pipeline3DCulled, nullptr);
            program.m_Pipeline3DCulled = VK_NULL_HANDLE;
        }
        if (program.m_Pipeline3DOffscreen != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, program.m_Pipeline3DOffscreen, nullptr);
            program.m_Pipeline3DOffscreen = VK_NULL_HANDLE;
        }
        if (program.m_Pipeline3DOffscreenCulled != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, program.m_Pipeline3DOffscreenCulled, nullptr);
            program.m_Pipeline3DOffscreenCulled = VK_NULL_HANDLE;
        }
        if (program.m_VertexModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(m_Device, program.m_VertexModule, nullptr);
            program.m_VertexModule = VK_NULL_HANDLE;
        }
        if (program.m_FragmentModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(m_Device, program.m_FragmentModule, nullptr);
            program.m_FragmentModule = VK_NULL_HANDLE;
        }
    }

    void QuarkVkShaderCompiler::FreePipelines()
    {
        for (auto& [id, program] : m_Programs)
        {
            (void)id;
            if (program.m_Pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(m_Device, program.m_Pipeline, nullptr);
                program.m_Pipeline = VK_NULL_HANDLE;
            }
            if (program.m_PipelineOffscreen != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(m_Device, program.m_PipelineOffscreen, nullptr);
                program.m_PipelineOffscreen = VK_NULL_HANDLE;
            }
            if (program.m_Pipeline3D != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(m_Device, program.m_Pipeline3D, nullptr);
                program.m_Pipeline3D = VK_NULL_HANDLE;
            }
            if (program.m_Pipeline3DCulled != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(m_Device, program.m_Pipeline3DCulled, nullptr);
                program.m_Pipeline3DCulled = VK_NULL_HANDLE;
            }
            if (program.m_Pipeline3DOffscreen != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(m_Device, program.m_Pipeline3DOffscreen, nullptr);
                program.m_Pipeline3DOffscreen = VK_NULL_HANDLE;
            }
            if (program.m_Pipeline3DOffscreenCulled != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(m_Device, program.m_Pipeline3DOffscreenCulled, nullptr);
                program.m_Pipeline3DOffscreenCulled = VK_NULL_HANDLE;
            }
        }
    }

    bool QuarkVkShaderCompiler::IsProgramValid(uint32_t programId) const
    {
        return programId != 0 && m_Programs.find(programId) != m_Programs.end();
    }

    const VkShaderProgramData* QuarkVkShaderCompiler::GetProgram(uint32_t programId) const
    {
        const auto it = m_Programs.find(programId);
        return it != m_Programs.end() ? &it->second : nullptr;
    }

    VkShaderProgramData* QuarkVkShaderCompiler::GetProgram(uint32_t programId)
    {
        const auto it = m_Programs.find(programId);
        return it != m_Programs.end() ? &it->second : nullptr;
    }

    uint32_t QuarkVkShaderCompiler::CurrentProgramId() const
    {
        return m_CurrentProgramId;
    }

    void QuarkVkShaderCompiler::SetCurrentProgram(uint32_t programId)
    {
        m_CurrentProgramId = programId;
    }

    void QuarkVkShaderCompiler::ClearCurrentProgram()
    {
        m_CurrentProgramId = 0;
    }

    int QuarkVkShaderCompiler::GetUniformLocation(uint32_t programId, const char* pUniformName)
    {
        if (!pUniformName || programId == 0)
        {
            return -1;
        }

        VkShaderProgramData* pProgram = GetProgram(programId);
        if (pProgram == nullptr)
        {
            return -1;
        }

        auto& locationMap = pProgram->m_Uniforms;
        const auto locIt = locationMap.find(pUniformName);
        if (locIt != locationMap.end())
        {
            pProgram->m_UniformNames[locIt->second] = pUniformName;
            return locIt->second;
        }

        const int newLocation = static_cast<int>(locationMap.size());
        locationMap.emplace(pUniformName, newLocation);
        pProgram->m_UniformNames[newLocation] = pUniformName;
        return newLocation;
    }

    int QuarkVkShaderCompiler::GetUniformLocation(uint32_t programId, ShaderLocationIndex locIndex)
    {
        if (locIndex < SHADER_LOC_VERTEX_POSITION || locIndex >= SHADER_LOC_COUNT)
        {
            return -1;
        }
        return GetUniformLocation(programId, s_apShaderLocationNames[locIndex]);
    }

    int QuarkVkShaderCompiler::GetAttributeLocation(uint32_t programId, const char* pAttribName)
    {
        if (!pAttribName || programId == 0)
        {
            return -1;
        }

        VkShaderProgramData* pProgram = GetProgram(programId);
        if (pProgram == nullptr)
        {
            return -1;
        }

        auto& locationMap = pProgram->m_Attributes;
        const auto locIt = locationMap.find(pAttribName);
        if (locIt != locationMap.end())
        {
            return locIt->second;
        }

        const int newLocation = static_cast<int>(locationMap.size());
        locationMap.emplace(pAttribName, newLocation);
        return newLocation;
    }

    void QuarkVkShaderCompiler::StoreUniformValue(uint32_t programId,
                                                  int locIndex,
                                                  int uniformType,
                                                  const void* pValue,
                                                  size_t bytesPerElement,
                                                  int count)
    {
        VkShaderProgramData* pProgram = GetProgram(programId);
        if (pProgram == nullptr)
        {
            return;
        }
        if (locIndex < 0 || pValue == nullptr || count <= 0)
        {
            return;
        }

        const size_t totalBytes = static_cast<size_t>(count) * bytesPerElement;
        auto& vStorage = pProgram->m_vUniformValues[locIndex];
        vStorage.assign(static_cast<const uint8_t*>(pValue),
                        static_cast<const uint8_t*>(pValue) + totalBytes);
        pProgram->m_UniformTypes[locIndex] = uniformType;
    }

    VkShaderModule QuarkVkShaderCompiler::CreateModule(const std::vector<uint32_t>& vSpirv) const
    {
        if (m_Device == VK_NULL_HANDLE || vSpirv.empty())
        {
            return VK_NULL_HANDLE;
        }

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = vSpirv.size() * sizeof(uint32_t);
        createInfo.pCode = vSpirv.data();

        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(m_Device, &createInfo, nullptr, &module) != VK_SUCCESS)
        {
            TraceLog(LogLevel::Error, "VULKAN", "[Vulkan] Failed to create Vulkan shader module.");
            throw std::runtime_error("Failed to create Vulkan shader module.");
        }
        TraceLog(LogLevel::Trace, "SHADER", TextFormat("[Vulkan] Created VkShaderModule (%zu words / %zu bytes, Handle: %p)", vSpirv.size(), createInfo.codeSize, (void*)module));
        return module;
    }
} // qci
