#ifndef __QUARK_VK_SHADER_COMPILER__
#define __QUARK_VK_SHADER_COMPILER__

#include "QuarkVkCommon.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace qci
{
    struct VkShaderProgramData
    {
        VkShaderModule m_VertexModule = VK_NULL_HANDLE;
        VkShaderModule m_FragmentModule = VK_NULL_HANDLE;
        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkPipeline m_PipelineOffscreen = VK_NULL_HANDLE;
        VkPipeline m_Pipeline3D = VK_NULL_HANDLE;
        VkPipeline m_Pipeline3DCulled = VK_NULL_HANDLE;
        VkPipeline m_Pipeline3DOffscreen = VK_NULL_HANDLE;
        VkPipeline m_Pipeline3DOffscreenCulled = VK_NULL_HANDLE;
        VkDescriptorSet m_DescriptorSet3D = VK_NULL_HANDLE;
        bool m_Supports3D = false;
        bool m_SupportsGpuMeshTransform = false;
        std::unordered_map<std::string, int> m_Uniforms;
        std::unordered_map<std::string, int> m_Attributes;
        std::unordered_map<int, std::vector<uint8_t>> m_vUniformValues;
        std::unordered_map<int, int> m_UniformTypes;
        std::unordered_map<int, std::string> m_UniformNames;
    };

    class QuarkVkShaderCompiler
    {
    public:
        void Initialize(VkDevice device);
        void Shutdown();
        bool IsInitialized() const;

        uint32_t LoadProgram(const char* pVsFile, const char* pFsFile);
        uint32_t CreateProgram(const char* pVsSource, const char* pFsSource);
        void DestroyProgram(uint32_t programId);
        void FreePipelines();
        bool IsProgramValid(uint32_t programId) const;
        const VkShaderProgramData* GetProgram(uint32_t programId) const;
        VkShaderProgramData* GetProgram(uint32_t programId);

        std::unordered_map<uint32_t, VkShaderProgramData>& Programs()
        {
            return m_Programs;
        }

        uint32_t CurrentProgramId() const;
        void SetCurrentProgram(uint32_t programId);
        void ClearCurrentProgram();

        int GetUniformLocation(uint32_t programId, const char* pUniformName);
        int GetUniformLocation(uint32_t programId, ShaderLocationIndex locIndex);
        int GetAttributeLocation(uint32_t programId, const char* pAttribName);

        void StoreUniformValue(uint32_t programId, int locIndex, int uniformType,
                               const void* pValue, size_t bytesPerElement, int count);

    private:
        void DestroyProgramData(VkShaderProgramData& program);
        VkShaderModule CreateModule(const std::vector<uint32_t>& vSpirv) const;

        VkDevice m_Device = VK_NULL_HANDLE;
        uint32_t m_NextProgramId = 1;
        uint32_t m_CurrentProgramId = 0;
        std::unordered_map<uint32_t, VkShaderProgramData> m_Programs;
    };
} // qci
#endif // __QUARK_VK_SHADER_COMPILER__
