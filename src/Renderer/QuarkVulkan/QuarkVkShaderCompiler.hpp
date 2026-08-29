#ifndef __QUARK_VK_SHADER_COMPILER__
#define __QUARK_VK_SHADER_COMPILER__

#include "QuarkVkCommon.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace qc {

struct VkShaderProgramData {
    VkShaderModule vertexModule   = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    VkPipeline     pipeline       = VK_NULL_HANDLE;
    VkPipeline     pipeline3D     = VK_NULL_HANDLE;
    VkPipeline     pipeline3DOffscreen = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet3D = VK_NULL_HANDLE;
    bool            supports3D    = false;
    std::unordered_map<std::string, int> uniforms;
    std::unordered_map<std::string, int> attributes;
    std::unordered_map<int, std::vector<uint8_t>> uniformValues;
    std::unordered_map<int, int> uniformTypes;
    std::unordered_map<int, std::string> uniformNames;
};

class QuarkVkShaderCompiler {
public:
    void Initialize(VkDevice device);
    void Shutdown();
    bool IsInitialized() const;

    uint32_t LoadProgram(const char* vsFile, const char* fsFile);
    uint32_t CreateProgram(const char* vsSource, const char* fsSource);
    void     DestroyProgram(uint32_t programId);
    void     FreePipelines();
    bool     IsProgramValid(uint32_t programId) const;
    const VkShaderProgramData* GetProgram(uint32_t programId) const;
    VkShaderProgramData* GetProgram(uint32_t programId);

    std::unordered_map<uint32_t, VkShaderProgramData>& Programs() { return m_programs; }

    uint32_t CurrentProgramId() const;
    void     SetCurrentProgram(uint32_t programId);
    void     ClearCurrentProgram();

    int  GetUniformLocation(uint32_t programId, const char* uniformName);
    int  GetUniformLocation(uint32_t programId, ShaderLocationIndex locIndex);
    int  GetAttributeLocation(uint32_t programId, const char* attribName);

    void StoreUniformValue(uint32_t programId, int locIndex, int uniformType,
                           const void* value, size_t bytesPerElement, int count);

private:
    void DestroyProgramData(VkShaderProgramData& program);
    VkShaderModule CreateModule(const std::vector<uint32_t>& spirv) const;

    VkDevice m_device = VK_NULL_HANDLE;
    uint32_t m_nextProgramId = 1;
    uint32_t m_currentProgramId = 0;
    std::unordered_map<uint32_t, VkShaderProgramData> m_programs;
};

} // namespace qc

#endif // __QUARK_VK_SHADER_COMPILER__