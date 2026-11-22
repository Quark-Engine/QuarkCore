#include "QuarkVkRenderer.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>

namespace qci
{
    namespace
    {
        int NormalizeIntValue(const void* pData)
        {
            int intValue = 0;
            std::memcpy(&intValue, pData, sizeof(int));
            return intValue;
        }
    } // anonymous

    void QuarkVkRenderer::BeginShaderMode(const Shader& shader)
    {
        if (m_VkShaderCompiler.IsProgramValid(shader.id))
        {
            m_VkShaderCompiler.SetCurrentProgram(shader.id);
        }
    }

    void QuarkVkRenderer::EndShaderMode()
    {
        m_VkShaderCompiler.ClearCurrentProgram();
    }

    Shader QuarkVkRenderer::LoadShader(const char* pVs, const char* pFs)
    {
        return FinishShaderProgram(m_VkShaderCompiler.LoadProgram(pVs, pFs));
    }

    Shader QuarkVkRenderer::LoadShaderFromMemory(const char* pVs, const char* pFs)
    {
        return FinishShaderProgram(m_VkShaderCompiler.CreateProgram(pVs, pFs));
    }

    Shader QuarkVkRenderer::FinishShaderProgram(uint32_t shaderId)
    {
        if (shaderId == 0)
        {
            return Shader{};
        }

        VkShaderProgramData& programData = *m_VkShaderCompiler.GetProgram(shaderId);

        if (programData.m_Supports3D)
        {
            programData.m_Pipeline3D = m_VkPipeline.Create3DPipeline(m_VkRenderPass.Get(),
                                                                   VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                                   programData.m_VertexModule,
                                                                   programData.m_FragmentModule,
                                                                   programData.m_SupportsGpuMeshTransform,
                                                                   false);
            programData.m_Pipeline3DCulled = m_VkPipeline.Create3DPipeline(m_VkRenderPass.Get(),
                                                                         VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                                         programData.m_VertexModule,
                                                                         programData.m_FragmentModule,
                                                                         programData.m_SupportsGpuMeshTransform,
                                                                         true);
            if (m_VkRenderTarget.RenderPass() != VK_NULL_HANDLE)
            {
                programData.m_Pipeline3DOffscreen = m_VkPipeline.Create3DPipeline(m_VkRenderTarget.RenderPass(),
                                                                                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                                                programData.m_VertexModule,
                                                                                programData.m_FragmentModule,
                                                                                programData.m_SupportsGpuMeshTransform,
                                                                                false);
                programData.m_Pipeline3DOffscreenCulled = m_VkPipeline.Create3DPipeline(m_VkRenderTarget.RenderPass(),
                                                                                      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                                                      programData.m_VertexModule,
                                                                                      programData.m_FragmentModule,
                                                                                      programData.m_SupportsGpuMeshTransform,
                                                                                      true);
            }
            VkDescriptorPool descriptorPool3D = VK_NULL_HANDLE;
            if (Allocate3DDescriptorSet(programData.m_DescriptorSet3D, descriptorPool3D))
            {
                const VkTextureData* pWhiteTex = m_VkResources.Get(m_WhiteTextureId);
                if (pWhiteTex != nullptr)
                {
                    VkDescriptorImageInfo imageInfo{};
                    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imageInfo.imageView = pWhiteTex->m_View;
                    imageInfo.sampler = pWhiteTex->m_Sampler;
                    std::array<VkDescriptorImageInfo, 4> shadowImages{imageInfo, imageInfo, imageInfo, imageInfo};
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
                    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, programData.m_DescriptorSet3D, 0, 0, 1,
                                 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &bufferInfo, nullptr};
                    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, programData.m_DescriptorSet3D, 1, 0, 1,
                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfo, nullptr, nullptr};
                    writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, programData.m_DescriptorSet3D, 2, 0, 4,
                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, shadowImages.data(), nullptr, nullptr};
                    writes[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, programData.m_DescriptorSet3D, 3, 0, 1,
                                 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &shadowBufferInfo, nullptr};
                    writes[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, programData.m_DescriptorSet3D, 4, 0, 1,
                                 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &lightBufferInfo, nullptr};
                    vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
                }
            }
        }
        else
        {
            programData.m_Pipeline = m_VkPipeline.Create2DPipeline(m_VkRenderPass.Get(),
                                                                 programData.m_VertexModule,
                                                                 programData.m_FragmentModule);
            if (m_VkRenderTarget.RenderPass() != VK_NULL_HANDLE)
            {
                programData.m_PipelineOffscreen = m_VkPipeline.Create2DPipeline(m_VkRenderTarget.RenderPass(),
                                                                                programData.m_VertexModule,
                                                                                programData.m_FragmentModule);
            }
        }
        TraceLog(LogLevel::Info, "SHADER", TextFormat("[Vulkan] Shader program created successfully (ID: %u, Attributes: %zu, Uniforms: %zu, Pipeline: %p)", shaderId, programData.m_Attributes.size(), programData.m_Uniforms.size(), (void*)programData.m_Pipeline));
        return Shader{shaderId};
    }

    void QuarkVkRenderer::UnloadShader(Shader& shader)
    {
        if (shader.id == 0)
        {
            return;
        }

        if (m_Device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_Device);
        }
        m_VkShaderCompiler.DestroyProgram(shader.id);
        shader = Shader{};
    }

    bool QuarkVkRenderer::isShaderValid(Shader& shader)
    {
        return m_VkShaderCompiler.IsProgramValid(shader.id);
    }

    int QuarkVkRenderer::GetShaderLocation(const Shader& shader, const char* pUniformName)
    {
        return m_VkShaderCompiler.GetUniformLocation(shader.id, pUniformName);
    }

    int QuarkVkRenderer::GetShaderLocation(const Shader& shader, ShaderLocationIndex locIndex)
    {
        return m_VkShaderCompiler.GetUniformLocation(shader.id, locIndex);
    }

    int QuarkVkRenderer::GetShaderAttributeLocation(const Shader& shader, const char* pAttribName)
    {
        return m_VkShaderCompiler.GetAttributeLocation(shader.id, pAttribName);
    }

    void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, float value)
    {
        if (shader.id == 0 || locIndex < 0 || !m_VkShaderCompiler.IsProgramValid(shader.id))
        {
            return;
        }
        m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_FLOAT, &value, sizeof(float), 1);
        Write3DShaderUniform(shader.id, locIndex, &value, sizeof(float));
    }

    void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, int value)
    {
        if (shader.id == 0 || locIndex < 0 || !m_VkShaderCompiler.IsProgramValid(shader.id))
        {
            return;
        }
        m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_INT, &value, sizeof(int), 1);
        Write3DShaderUniform(shader.id, locIndex, value);
    }

    void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const Color& value)
    {
        if (shader.id == 0 || locIndex < 0 || !m_VkShaderCompiler.IsProgramValid(shader.id))
        {
            return;
        }
        const float rgba[4] = {
            value.r / 255.0f,
            value.g / 255.0f,
            value.b / 255.0f,
            value.a / 255.0f};
        m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_VEC4, rgba, sizeof(float), 4);
        Write3DShaderUniform(shader.id, locIndex, rgba, sizeof(rgba));
    }

    void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const Vec2& value)
    {
        if (shader.id == 0 || locIndex < 0 || !m_VkShaderCompiler.IsProgramValid(shader.id))
        {
            return;
        }
        const float vec[2] = {value.x, value.y};
        m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_VEC2, vec, sizeof(float), 2);
        Write3DShaderUniform(shader.id, locIndex, vec, sizeof(vec));
    }

    void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const Vec3& value)
    {
        if (shader.id == 0 || locIndex < 0 || !m_VkShaderCompiler.IsProgramValid(shader.id))
        {
            return;
        }
        const float vec[3] = {value.x, value.y, value.z};
        m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_VEC3, vec, sizeof(float), 3);
        Write3DShaderUniform(shader.id, locIndex, vec, sizeof(vec));
    }

    void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const Vec4& value)
    {
        if (shader.id == 0 || locIndex < 0 || !m_VkShaderCompiler.IsProgramValid(shader.id))
        {
            return;
        }
        const float vec[4] = {value.x, value.y, value.z, value.w};
        m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_VEC4, vec, sizeof(float), 4);
        Write3DShaderUniform(shader.id, locIndex, vec, sizeof(vec));
    }

    void QuarkVkRenderer::SetShaderValueMatrix(const Shader& shader, int locIndex, const float* pMat)
    {
        if (shader.id == 0 || locIndex < 0 || pMat == nullptr || !m_VkShaderCompiler.IsProgramValid(shader.id))
        {
            return;
        }
        m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_FLOAT, pMat, sizeof(float), 16);
        Write3DShaderUniform(shader.id, locIndex, pMat, sizeof(float) * 16);
    }

    void QuarkVkRenderer::SetShaderValueSampler(const Shader& shader, int locIndex, int textureUnit)
    {
        if (shader.id == 0 || locIndex < 0 || !m_VkShaderCompiler.IsProgramValid(shader.id))
        {
            return;
        }
        m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureUnit, sizeof(int), 1);
        VkShaderProgramData* pProgram = m_VkShaderCompiler.GetProgram(shader.id);
        if (pProgram != nullptr)
        {
            Update3DDescriptorSet(*pProgram);
        }
    }

    void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const void* pValue, int uniformType)
    {
        if (shader.id == 0 || locIndex < 0 || pValue == nullptr || !m_VkShaderCompiler.IsProgramValid(shader.id))
        {
            return;
        }

        size_t writtenBytes = 0;
        switch (uniformType)
        {
        case SHADER_UNIFORM_FLOAT:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(float), 1);
            writtenBytes = sizeof(float);
            break;
        case SHADER_UNIFORM_VEC2:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(float), 2);
            writtenBytes = sizeof(float) * 2;
            break;
        case SHADER_UNIFORM_VEC3:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(float), 3);
            writtenBytes = sizeof(float) * 3;
            break;
        case SHADER_UNIFORM_VEC4:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(float), 4);
            writtenBytes = sizeof(float) * 4;
            break;
        case SHADER_UNIFORM_INT:
        case SHADER_UNIFORM_SAMPLER2D:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(int), 1);
            writtenBytes = sizeof(float);
            break;
        case SHADER_UNIFORM_IVEC2:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(int), 2);
            writtenBytes = sizeof(float) * 2;
            break;
        case SHADER_UNIFORM_IVEC3:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(int), 3);
            writtenBytes = sizeof(float) * 3;
            break;
        case SHADER_UNIFORM_IVEC4:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(int), 4);
            writtenBytes = sizeof(float) * 4;
            break;
        default:
            break;
        }
        if (writtenBytes > 0)
        {
            Write3DShaderUniform(shader.id, locIndex, pValue, writtenBytes);
        }
    }

    void QuarkVkRenderer::SetShaderValueV(const Shader& shader, int locIndex, const void* pValue, int uniformType, int count)
    {
        if (shader.id == 0 || locIndex < 0 || pValue == nullptr || count <= 0 || !m_VkShaderCompiler.IsProgramValid(shader.id))
        {
            return;
        }

        switch (uniformType)
        {
        case SHADER_UNIFORM_FLOAT:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(float), count);
            break;
        case SHADER_UNIFORM_VEC2:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(float) * 2, count);
            break;
        case SHADER_UNIFORM_VEC3:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(float) * 3, count);
            break;
        case SHADER_UNIFORM_VEC4:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(float) * 4, count);
            break;
        case SHADER_UNIFORM_INT:
        case SHADER_UNIFORM_SAMPLER2D:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(int), count);
            break;
        case SHADER_UNIFORM_IVEC2:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(int) * 2, count);
            break;
        case SHADER_UNIFORM_IVEC3:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(int) * 3, count);
            break;
        case SHADER_UNIFORM_IVEC4:
            m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, pValue, sizeof(int) * 4, count);
            break;
        default:
            break;
        }
    }

    void QuarkVkRenderer::SetShaderValueMatrix(const Shader& shader, int locIndex, const Matrix& mat)
    {
        if (shader.id == 0 || locIndex < 0 || !m_VkShaderCompiler.IsProgramValid(shader.id))
        {
            return;
        }
        const float* pData = mat.m;
        m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_FLOAT, pData, sizeof(float), 16);
        Write3DShaderUniform(shader.id, locIndex, pData, sizeof(float) * 16);
    }

    void QuarkVkRenderer::SetShaderValueTexture(const Shader& shader, int locIndex, const ITexture& texture)
    {
        if (shader.id == 0 || locIndex < 0 || !m_VkShaderCompiler.IsProgramValid(shader.id))
        {
            return;
        }
        const int textureUnit = static_cast<int>(texture.m_Id);
        m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureUnit, sizeof(int), 1);
    }

    void QuarkVkRenderer::SetShaderValueTextureUnit(const Shader& shader, int locIndex, const ITexture& texture, int textureUnit)
    {
        if (shader.id == 0 || locIndex < 0 || !m_VkShaderCompiler.IsProgramValid(shader.id))
        {
            return;
        }
        const int textureId = texture.m_Valid ? static_cast<int>(texture.m_Id) : textureUnit;
        m_VkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureId, sizeof(int), 1);
        VkShaderProgramData* pProgram = m_VkShaderCompiler.GetProgram(shader.id);
        if (pProgram != nullptr)
        {
            Update3DDescriptorSet(*pProgram);
        }
    }

    void QuarkVkRenderer::Update3DDescriptorSet(VkShaderProgramData& program)
    {
        if (program.m_DescriptorSet3D == VK_NULL_HANDLE)
            return;

        const VkTextureData* pWhiteTex = m_VkResources.Get(m_WhiteTextureId);
        if (pWhiteTex == nullptr)
            return;

        const VkTextureData& white = *pWhiteTex;
        std::array<VkDescriptorImageInfo, 4> shadowImages{};
        for (auto& image : shadowImages)
        {
            image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            image.imageView = white.m_View;
            image.sampler = white.m_Sampler;
        }
        VkDescriptorImageInfo albedo = shadowImages[0];

        const auto textureFromUniform = [&](const char* name, VkDescriptorImageInfo& output)
        {
            const auto locationIt = program.m_Uniforms.find(name);
            if (locationIt == program.m_Uniforms.end())
                return;
            const auto valueIt = program.m_vUniformValues.find(locationIt->second);
            if (valueIt == program.m_vUniformValues.end() || valueIt->second.size() < sizeof(int))
                return;
            int textureId = 0;
            std::memcpy(&textureId, valueIt->second.data(), sizeof(textureId));
            const VkTextureData* pTexture = m_VkResources.Get(static_cast<uint32_t>(textureId));
            if (pTexture != nullptr)
            {
                output.imageView = pTexture->m_View;
                output.sampler = pTexture->m_Sampler;
            }
        };

        textureFromUniform("uTexture", albedo);
        std::array<VkDescriptorImageInfo, 6> materialImages{};
        materialImages.fill(albedo);
        const std::array<const char*, 6> materialNames = {
            "albedo", "metalness", "normal", "roughness", "occlusion", "emission"};
        for (size_t i = 0; i < materialNames.size(); ++i)
        {
            textureFromUniform(materialNames[i], materialImages[i]);
        }
        const auto shadowsIt = program.m_Uniforms.find("shadowMaps");
        if (shadowsIt != program.m_Uniforms.end())
        {
            const auto valuesIt = program.m_vUniformValues.find(shadowsIt->second);
            if (valuesIt != program.m_vUniformValues.end())
            {
                const size_t count = std::min<size_t>(4, valuesIt->second.size() / sizeof(int));
                for (size_t i = 0; i < count; ++i)
                {
                    int textureId = 0;
                    std::memcpy(&textureId, valuesIt->second.data() + i * sizeof(int), sizeof(textureId));
                    const VkTextureData* pTexture = m_VkResources.Get(static_cast<uint32_t>(textureId));
                    if (pTexture != nullptr)
                    {
                        shadowImages[i].imageView = pTexture->m_View;
                        shadowImages[i].sampler = pTexture->m_Sampler;
                    }
                }
            }
        }

        std::array<VkWriteDescriptorSet, 8> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, program.m_DescriptorSet3D,
                     1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &albedo, nullptr, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, program.m_DescriptorSet3D,
                     2, 0, 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, shadowImages.data(), nullptr, nullptr};
        for (uint32_t i = 0; i < 6; ++i)
        {
            writes[2 + i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, program.m_DescriptorSet3D,
                             5 + i, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             &materialImages[i], nullptr, nullptr};
        }
        vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void QuarkVkRenderer::Write3DShaderUniform(uint32_t shaderId, int locIndex, float value)
    {
        Write3DShaderUniform(shaderId, locIndex, &value, sizeof(float));
    }

    void QuarkVkRenderer::Write3DShaderUniform(uint32_t shaderId, int locIndex, int value)
    {
        Write3DShaderUniform(shaderId, locIndex, &value, sizeof(int));
    }

    void QuarkVkRenderer::Write3DShaderUniform(uint32_t shaderId, int locIndex, const void* pData, size_t bytes)
    {
        if (shaderId == 0 || locIndex < 0 || pData == nullptr || bytes == 0 || m_p3DDummyMapped == nullptr)
        {
            return;
        }
        const auto* pProgram = m_VkShaderCompiler.GetProgram(shaderId);
        if (pProgram == nullptr)
            return;
        const auto nameIt = pProgram->m_UniformNames.find(locIndex);
        if (nameIt == pProgram->m_UniformNames.end())
            return;
        const std::string& name = nameIt->second;

        const auto indexOf = [&name](const char* prefix, int& outIndex) -> bool
        {
            const std::string p(prefix);
            if (name.rfind(p, 0) != 0)
                return false;
            const size_t br = name.find('[');
            if (br == std::string::npos)
                return false;
            const size_t close = name.find(']', br);
            if (close == std::string::npos)
                return false;
            outIndex = std::atoi(name.c_str() + br + 1);
            return true;
        };

        int index = -1;
        if (indexOf("lightViews", index) && index >= 0 && index < 4 && bytes >= sizeof(float) * 16)
        {
            std::memcpy(static_cast<char*>(m_p3DDummyMapped) + 512 + static_cast<size_t>(index) * 64,
                        pData, sizeof(float) * 16);
            return;
        }
        if (indexOf("lightProjections", index) && index >= 0 && index < 4 && bytes >= sizeof(float) * 16)
        {
            std::memcpy(static_cast<char*>(m_p3DDummyMapped) + 512 + static_cast<size_t>(index + 4) * 64,
                        pData, sizeof(float) * 16);
            return;
        }

        if (name == "ambient" && bytes >= sizeof(float) * 4)
        {
            const float* pF = static_cast<const float*>(pData);
            float* pLightData = static_cast<float*>(static_cast<void*>(static_cast<char*>(m_p3DDummyMapped) + 1024));
            std::memcpy(pLightData + 0, pF, sizeof(float) * 4);
            return;
        }
        if (name == "colDiffuse" && bytes >= sizeof(float) * 4)
        {
            const float* pF = static_cast<const float*>(pData);
            float* pLightData = static_cast<float*>(static_cast<void*>(static_cast<char*>(m_p3DDummyMapped) + 1024));
            std::memcpy(pLightData + 4, pF, sizeof(float) * 4);
            return;
        }
        if (name == "viewPos" && bytes >= sizeof(float) * 3)
        {
            const float* pF = static_cast<const float*>(pData);
            float* pLightData = static_cast<float*>(static_cast<void*>(static_cast<char*>(m_p3DDummyMapped) + 1024));
            pLightData[8] = pF[0];
            pLightData[9] = pF[1];
            pLightData[10] = pF[2];
            pLightData[11] = 1.0f;
            m_ViewPos = Vec3{pF[0], pF[1], pF[2]};
            return;
        }

        if (name.rfind("lights[", 0) == 0 && indexOf("lights[", index) && index >= 0 && index < 4)
        {
            const size_t close = name.find(']');
            if (close == std::string::npos)
                return;
            const std::string field = name.substr(close + 2);
            float* pLightData = static_cast<float*>(static_cast<void*>(static_cast<char*>(m_p3DDummyMapped) + 1024));
            float* pSlot = pLightData + 12 + static_cast<size_t>(index) * 16;

            if (field == "position")
            {
                const float* pF = static_cast<const float*>(pData);
                pSlot[0] = pF[0];
                pSlot[1] = pF[1];
                pSlot[2] = pF[2];
                pSlot[3] = 1.0f;
                m_aLights[static_cast<size_t>(index)].m_Position = Vec3{pF[0], pF[1], pF[2]};
            }
            else if (field == "target")
            {
                const float* pF = static_cast<const float*>(pData);
                pSlot[4] = pF[0];
                pSlot[5] = pF[1];
                pSlot[6] = pF[2];
                pSlot[7] = 1.0f;
                m_aLights[static_cast<size_t>(index)].m_Target = Vec3{pF[0], pF[1], pF[2]};
            }
            else if (field == "color")
            {
                const float* pF = static_cast<const float*>(pData);
                pSlot[8] = pF[0];
                pSlot[9] = pF[1];
                pSlot[10] = pF[2];
                pSlot[11] = 1.0f;
                m_aLights[static_cast<size_t>(index)].m_Color = Vec3{pF[0], pF[1], pF[2]};
            }
            else if (field == "attenuation")
            {
                pSlot[12] = *static_cast<const float*>(pData);
                m_aLights[static_cast<size_t>(index)].m_Attenuation = pSlot[12];
            }
            else if (field == "enabled")
            {
                const int enabledValue = NormalizeIntValue(pData);
                std::memcpy(pSlot + 13, &enabledValue, sizeof(int));
                m_aLights[static_cast<size_t>(index)].m_Enabled = enabledValue != 0;
                m_3DLightEnabled[static_cast<size_t>(index)] = enabledValue != 0;
            }
            else if (field == "type")
            {
                const int typeValue = NormalizeIntValue(pData);
                std::memcpy(pSlot + 14, &typeValue, sizeof(int));
                m_aLights[static_cast<size_t>(index)].m_Type = typeValue;
            }
            return;
        }
    }
} // qci
