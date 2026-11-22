#include "QuarkVkPipeline.hpp"

#include <shaderc/shaderc.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace qci
{
    namespace
    {
        const char* s_pRuntime2DVertexShader = R"glsl(
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

        const char* s_pRuntime2DFragmentShader = R"glsl(
            #version 450
            layout(set = 0, binding = 0) uniform sampler2D texture0;
            layout(location = 0) in vec2 vTexCoord;
            layout(location = 1) in vec4 vColor;
            layout(location = 0) out vec4 outColor;
            void main() {
                outColor = texture(texture0, vTexCoord) * vColor;
            }
        )glsl";

        const char* s_pRuntime3DVertexShader = R"glsl(
            #version 450
            layout(location = 0) in vec4 aPosition;
            layout(location = 1) in vec2 aTexCoord;
            layout(location = 2) in vec4 aColor;
            layout(location = 3) in vec4 aNormal;
            layout(location = 4) in vec4 aWorldPosition;
            layout(location = 5) in vec4 aTangent;
            layout(location = 0) out vec2 vTexCoord;
            layout(location = 1) out vec4 vColor;
            layout(location = 2) out vec3 vNormal;
            layout(location = 3) out vec3 vWorldPosition;
            layout(location = 4) out vec4 vTangent;
            void main() {
                gl_Position = aPosition;
                vTexCoord = aTexCoord;
                vColor = aColor;
                float normalLength = length(aNormal.xyz);
                vNormal = normalLength > 0.0001 ? aNormal.xyz / normalLength : vec3(0.0);
                vWorldPosition = aWorldPosition.xyz;
                vTangent = vec4(normalize(aTangent.xyz), aTangent.w);
            }
        )glsl";

        const char* s_pRuntime3DFragmentShader = R"glsl(
            #version 450
            layout(set = 0, binding = 1) uniform sampler2D albedoMap;
            layout(set = 0, binding = 4) uniform LightBlock {
                vec4 ambient;
                vec4 viewPos;
                vec4 lightPositions[4];
                vec4 lightColors[4];
                vec4 lightParams[4];
            } lights;
            layout(set = 0, binding = 6) uniform sampler2D uNormalMap;
            layout(location = 0) in vec2 vTexCoord;
            layout(location = 1) in vec4 vColor;
            layout(location = 2) in vec3 vNormal;
            layout(location = 3) in vec3 vWorldPosition;
            layout(location = 4) in vec4 vTangent;
            layout(location = 0) out vec4 outColor;

            vec3 ApplyLights(vec3 worldPos, vec3 normal, vec3 baseColor) {
                vec3 result = baseColor * lights.ambient.rgb;
                vec3 n = normalize(normal);
                int enabled = 0;
                for (int i = 0; i < 4; ++i) {
                    if (lights.lightParams[i].y < 0.5) { continue; }
                    enabled = 1;
                    float dist = length(lights.lightPositions[i].xyz - worldPos);
                    if (dist < 0.0001) { dist = 0.0001; }
                    vec3 toLight = (lights.lightPositions[i].xyz - worldPos) / dist;
                    if (lights.lightParams[i].z < 0.5) {
                        toLight = -normalize(lights.lightPositions[i].xyz);
                    }
                    float diff = max(dot(n, toLight), 0.0);
                    float attenuation = 1.0 / (1.0 + lights.lightParams[i].x * dist * dist);
                    result += baseColor * lights.lightColors[i].rgb * diff * attenuation;
                }
                if (enabled == 0) { return baseColor; }
                return clamp(result, 0.0, 1.0);
            }

            void main() {
                vec4 texel = texture(albedoMap, vTexCoord);
                vec3 baseColor = texel.rgb * vColor.rgb;
                vec3 normal = vNormal;
                if (vTangent.w > 0.5) {
                    vec2 mapNormal = texture(uNormalMap, vTexCoord).xy * 2.0 - 1.0;
                    float z = sqrt(max(0.0, 1.0 - dot(mapNormal, mapNormal)));
                    vec3 t = normalize(vTangent.xyz);
                    vec3 n = normalize(vNormal);
                    vec3 b = normalize(cross(n, t));
                    normal = normalize(t * mapNormal.x + b * mapNormal.y + n * z);
                }
                vec3 lit = baseColor;
                if (dot(normal, normal) > 0.000001) {
                    lit = ApplyLights(vWorldPosition, normal, baseColor);
                }
                outColor = vec4(lit, texel.a * vColor.a);
            }
        )glsl";

        const char* s_pRuntime3DLineVertexShader = R"glsl(
            #version 450
            layout(location = 0) in vec4 aPosition;
            layout(location = 2) in vec4 aColor;
            layout(location = 0) out vec4 vColor;
            void main() {
                gl_Position = aPosition;
                vColor = aColor;
            }
        )glsl";

        const char* s_pRuntime3DLineFragmentShader = R"glsl(
            #version 450
            layout(location = 0) in vec4 vColor;
            layout(location = 0) out vec4 outColor;
            void main() {
                outColor = vColor;
            }
        )glsl";

        std::vector<uint32_t> CompileBuiltInShader(const char* pSource,
                                                   shaderc_shader_kind kind,
                                                   const char* pStageName)
        {
            static std::unordered_map<std::string, std::vector<uint32_t>> s_Cache;
            const std::string sourceText = pSource ? pSource : "";
            const std::string cacheKey = std::string(pStageName) + "\n" + sourceText;
            const auto cached = s_Cache.find(cacheKey);
            if (cached != s_Cache.end())
            {
                return cached->second;
            }

            const auto hashText = [](const std::string& text)
            {
                uint64_t hash = 14695981039346656037ull;
                for (unsigned char character : text)
                {
                    hash ^= character;
                    hash *= 1099511628211ull;
                }
                return hash;
            };
            const auto cacheFile = [&]() -> std::filesystem::path
            {
                std::error_code error;
                const std::filesystem::path directory =
                    std::filesystem::temp_directory_path(error) / "QuarkCore" / "shader-cache";
                if (error)
                {
                    return {};
                }
                return directory / (std::to_string(hashText(cacheKey)) + ".spv");
            };
            const std::filesystem::path path = cacheFile();
            if (!path.empty())
            {
                std::ifstream file(path, std::ios::binary);
                uint32_t wordCount = 0;
                if (file && file.read(reinterpret_cast<char*>(&wordCount), sizeof(wordCount)) && wordCount > 0)
                {
                    std::vector<uint32_t> vSpirv(wordCount);
                    if (file.read(reinterpret_cast<char*>(vSpirv.data()), static_cast<std::streamsize>(vSpirv.size() * sizeof(uint32_t))))
                    {
                        s_Cache.emplace(cacheKey, vSpirv);
                        return vSpirv;
                    }
                }
            }

            shaderc::Compiler compiler;
            shaderc::CompileOptions options;
            options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
            const auto result = compiler.CompileGlslToSpv(pSource, kind, pStageName, options);
            if (result.GetCompilationStatus() != shaderc_compilation_status_success)
            {
                throw std::runtime_error(std::string("Failed to compile runtime Vulkan ") + pStageName + ": " + result.GetErrorMessage());
            }
            std::vector<uint32_t> vSpirv(result.cbegin(), result.cend());
            if (!path.empty())
            {
                std::error_code error;
                std::filesystem::create_directories(path.parent_path(), error);
                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                const uint32_t wordCount = static_cast<uint32_t>(vSpirv.size());
                if (file && file.write(reinterpret_cast<const char*>(&wordCount), sizeof(wordCount)))
                {
                    file.write(reinterpret_cast<const char*>(vSpirv.data()),
                               static_cast<std::streamsize>(vSpirv.size() * sizeof(uint32_t)));
                }
            }
            s_Cache.emplace(cacheKey, vSpirv);
            return vSpirv;
        }

        VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint32_t>& vSpirv)
        {
            if (device == VK_NULL_HANDLE || vSpirv.empty())
            {
                return VK_NULL_HANDLE;
            }

            VkShaderModuleCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.codeSize = vSpirv.size() * sizeof(uint32_t);
            createInfo.pCode = vSpirv.data();

            VkShaderModule shaderModule = VK_NULL_HANDLE;
            if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
            {
                return VK_NULL_HANDLE;
            }
            return shaderModule;
        }
    } // anonymous

    void QuarkVkPipeline::Initialize(VkDevice device,
                                     VkDescriptorSetLayout textureSetLayout,
                                     VkDescriptorSetLayout materialSetLayout)
    {
        m_Device = device;
        m_TextureSetLayout = textureSetLayout;
        m_MaterialSetLayout = materialSetLayout;
        EnsureLayouts();
    }

    void QuarkVkPipeline::EnsureLayouts()
    {
        if (m_Device == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Cannot create Vulkan pipeline layouts without a device.");
        }

        if (m_Layout2D == VK_NULL_HANDLE)
        {
            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = sizeof(VkPushConstants2D);

            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &m_TextureSetLayout;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushConstantRange;

            if (vkCreatePipelineLayout(m_Device, &layoutInfo, nullptr, &m_Layout2D) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create Vulkan pipeline layout.");
            }
        }

        if (m_Layout3D == VK_NULL_HANDLE)
        {
            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = sizeof(Vk3DPushConstants);

            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &m_MaterialSetLayout;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushConstantRange;

            if (vkCreatePipelineLayout(m_Device, &layoutInfo, nullptr, &m_Layout3D) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create Vulkan 3D pipeline layout.");
            }
        }
    }

    void QuarkVkPipeline::SetBlendMode(int mode)
    {
        m_BlendMode = mode;
        if (m_Device == VK_NULL_HANDLE || m_RenderPass == VK_NULL_HANDLE)
        {
            return;
        }

        CreatePipelines(m_RenderPass, m_OffscreenRenderPass, m_MsaaSamples);
    }

    void QuarkVkPipeline::CreatePipelines(VkRenderPass renderPass,
                                          VkRenderPass offscreenRenderPass,
                                          VkSampleCountFlagBits msaaSamples)
    {
        m_RenderPass = renderPass;
        m_OffscreenRenderPass = offscreenRenderPass;
        m_MsaaSamples = msaaSamples;

        DestroyPipelines();

        if (m_Device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE)
        {
            return;
        }

        m_Pipeline2D = Create2DPipeline(renderPass);
        if (offscreenRenderPass != VK_NULL_HANDLE)
        {
            m_OffscreenPipeline2D = Create2DPipeline(offscreenRenderPass);
        }
        m_Pipeline3DTri = Create3DPipeline(renderPass, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                           VK_NULL_HANDLE, VK_NULL_HANDLE, false, false);
        m_Pipeline3DTriCulled = Create3DPipeline(renderPass, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                 VK_NULL_HANDLE, VK_NULL_HANDLE, false, true);
        m_Pipeline3DLines = Create3DPipeline(renderPass, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
        if (offscreenRenderPass != VK_NULL_HANDLE)
        {
            m_OffscreenPipeline3DTri = Create3DPipeline(offscreenRenderPass, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                        VK_NULL_HANDLE, VK_NULL_HANDLE, false, false);
            m_OffscreenPipeline3DTriCulled = Create3DPipeline(offscreenRenderPass, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                              VK_NULL_HANDLE, VK_NULL_HANDLE, false, true);
            m_OffscreenPipeline3DLines = Create3DPipeline(offscreenRenderPass, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
        }
    }

    void QuarkVkPipeline::DestroyPipelines()
    {
        if (m_Device == VK_NULL_HANDLE)
        {
            return;
        }

        if (m_OffscreenPipeline2D != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, m_OffscreenPipeline2D, nullptr);
            m_OffscreenPipeline2D = VK_NULL_HANDLE;
        }
        if (m_OffscreenPipeline3DLines != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, m_OffscreenPipeline3DLines, nullptr);
            m_OffscreenPipeline3DLines = VK_NULL_HANDLE;
        }
        if (m_OffscreenPipeline3DTriCulled != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, m_OffscreenPipeline3DTriCulled, nullptr);
            m_OffscreenPipeline3DTriCulled = VK_NULL_HANDLE;
        }
        if (m_OffscreenPipeline3DTri != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, m_OffscreenPipeline3DTri, nullptr);
            m_OffscreenPipeline3DTri = VK_NULL_HANDLE;
        }
        if (m_Pipeline2D != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, m_Pipeline2D, nullptr);
            m_Pipeline2D = VK_NULL_HANDLE;
        }
        if (m_Pipeline3DLines != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, m_Pipeline3DLines, nullptr);
            m_Pipeline3DLines = VK_NULL_HANDLE;
        }
        if (m_Pipeline3DTriCulled != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, m_Pipeline3DTriCulled, nullptr);
            m_Pipeline3DTriCulled = VK_NULL_HANDLE;
        }
        if (m_Pipeline3DTri != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, m_Pipeline3DTri, nullptr);
            m_Pipeline3DTri = VK_NULL_HANDLE;
        }
    }

    void QuarkVkPipeline::Shutdown()
    {
        DestroyPipelines();

        if (m_Device == VK_NULL_HANDLE)
        {
            return;
        }

        if (m_Layout3D != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(m_Device, m_Layout3D, nullptr);
            m_Layout3D = VK_NULL_HANDLE;
        }
        if (m_Layout2D != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(m_Device, m_Layout2D, nullptr);
            m_Layout2D = VK_NULL_HANDLE;
        }

        m_Device = VK_NULL_HANDLE;
        m_RenderPass = VK_NULL_HANDLE;
        m_OffscreenRenderPass = VK_NULL_HANDLE;
        m_TextureSetLayout = VK_NULL_HANDLE;
        m_MaterialSetLayout = VK_NULL_HANDLE;
        m_BackfaceCullingEnabled = false;
    }

    void QuarkVkPipeline::SetBackfaceCulling(bool enabled)
    {
        m_BackfaceCullingEnabled = enabled;
    }

    VkPipeline QuarkVkPipeline::Create2DPipeline(VkRenderPass renderPass,
                                                 VkShaderModule vertexModule,
                                                 VkShaderModule fragmentModule)
    {
        if (m_Device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE)
        {
            return VK_NULL_HANDLE;
        }

        EnsureLayouts();

        bool ownsVertexModule = vertexModule == VK_NULL_HANDLE;
        bool ownsFragmentModule = fragmentModule == VK_NULL_HANDLE;
        if (ownsVertexModule)
        {
            const std::vector<uint32_t> vVertCode = CompileBuiltInShader(
                s_pRuntime2DVertexShader, shaderc_vertex_shader, "built-in 2D vertex shader");
            vertexModule = CreateShaderModule(m_Device, vVertCode);
        }
        if (ownsFragmentModule)
        {
            const std::vector<uint32_t> vFragCode = CompileBuiltInShader(
                s_pRuntime2DFragmentShader, shaderc_fragment_shader, "built-in 2D fragment shader");
            fragmentModule = CreateShaderModule(m_Device, vFragCode);
        }

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertexModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragmentModule;
        fragStage.pName = "main";

        VkVertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = 0;
        bindingDesc.stride = sizeof(VkBatchVertex);
        bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 3> aAttributes{};
        aAttributes[0].binding = 0;
        aAttributes[0].location = 0;
        aAttributes[0].format = VK_FORMAT_R32G32_SFLOAT;
        aAttributes[0].offset = offsetof(VkBatchVertex, m_X);

        aAttributes[1].binding = 0;
        aAttributes[1].location = 1;
        aAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        aAttributes[1].offset = offsetof(VkBatchVertex, m_U);

        aAttributes[2].binding = 0;
        aAttributes[2].location = 2;
        aAttributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        aAttributes[2].offset = offsetof(VkBatchVertex, m_R);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDesc;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(aAttributes.size());
        vertexInput.pVertexAttributeDescriptions = aAttributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = (renderPass == m_RenderPass) ? m_MsaaSamples : VK_SAMPLE_COUNT_1_BIT;
        multisampling.sampleShadingEnable = VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                              VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT |
                                              VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE;

        switch (m_BlendMode)
        {
        case BLEND_ADDITIVE:
        case BLEND_ADD_COLORS:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case BLEND_MULTIPLIED:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case BLEND_MOD_COLOR:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case BLEND_SUBTRACT_COLORS:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
            break;
        case BLEND_ALPHA:
        default:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        }

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkDynamicState aDynamicStates[] =
            {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(sizeof(aDynamicStates) / sizeof(aDynamicStates[0]));
        dynamicState.pDynamicStates = aDynamicStates;

        std::array<VkPipelineShaderStageCreateInfo, 2> aStages =
            {
                vertStage, fragStage};

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(aStages.size());
        pipelineInfo.pStages = aStages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_Layout2D;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
        {
            if (ownsFragmentModule)
            {
                vkDestroyShaderModule(m_Device, fragmentModule, nullptr);
            }
            if (ownsVertexModule)
            {
                vkDestroyShaderModule(m_Device, vertexModule, nullptr);
            }
            throw std::runtime_error("Failed to create Vulkan 2D pipeline.");
        }

        if (ownsFragmentModule)
        {
            vkDestroyShaderModule(m_Device, fragmentModule, nullptr);
        }
        if (ownsVertexModule)
        {
            vkDestroyShaderModule(m_Device, vertexModule, nullptr);
        }
        return pipeline;
    }

    VkPipeline QuarkVkPipeline::Create3DPipeline(VkRenderPass renderPass,
                                                 VkPrimitiveTopology topology,
                                                 VkShaderModule vertexModule,
                                                 VkShaderModule fragmentModule,
                                                 bool gpuMeshTransform,
                                                 bool backfaceCulling)
    {
        if (m_Device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE)
        {
            return VK_NULL_HANDLE;
        }

        EnsureLayouts();

        const bool ownsVertexModule = vertexModule == VK_NULL_HANDLE;
        const bool ownsFragmentModule = fragmentModule == VK_NULL_HANDLE;
        const bool unlitLinePipeline = topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST &&
                                       ownsVertexModule && ownsFragmentModule;
        if (ownsVertexModule)
        {
            const std::vector<uint32_t> vVertCode = CompileBuiltInShader(
                unlitLinePipeline ? s_pRuntime3DLineVertexShader : s_pRuntime3DVertexShader,
                shaderc_vertex_shader,
                unlitLinePipeline ? "built-in unlit 3D line vertex shader" : "built-in 3D vertex shader");
            vertexModule = CreateShaderModule(m_Device, vVertCode);
        }
        if (ownsFragmentModule)
        {
            const std::vector<uint32_t> vFragCode = CompileBuiltInShader(
                unlitLinePipeline ? s_pRuntime3DLineFragmentShader : s_pRuntime3DFragmentShader,
                shaderc_fragment_shader,
                unlitLinePipeline ? "built-in unlit 3D line fragment shader" : "built-in 3D fragment shader");
            fragmentModule = CreateShaderModule(m_Device, vFragCode);
        }

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertexModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragmentModule;
        fragStage.pName = "main";

        VkVertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = 0;
        bindingDesc.stride = gpuMeshTransform ? sizeof(VkGpuMeshVertex) : sizeof(Vk3DVertex);
        bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 6> aAttributes{};
        aAttributes[0].binding = 0;
        aAttributes[0].location = 0;
        aAttributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        aAttributes[0].offset = offsetof(Vk3DVertex, m_X);

        aAttributes[1].binding = 0;
        aAttributes[1].location = 1;
        aAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        aAttributes[1].offset = offsetof(Vk3DVertex, m_U);

        aAttributes[2].binding = 0;
        aAttributes[2].location = 2;
        aAttributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        aAttributes[2].offset = offsetof(Vk3DVertex, m_R);

        aAttributes[3].binding = 0;
        aAttributes[3].location = 3;
        aAttributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        aAttributes[3].offset = offsetof(Vk3DVertex, m_Nx);

        aAttributes[4].binding = 0;
        aAttributes[4].location = 4;
        aAttributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        aAttributes[4].offset = gpuMeshTransform ? offsetof(VkGpuMeshVertex, m_Wx) : offsetof(Vk3DVertex, m_Wx);

        aAttributes[5].binding = 0;
        aAttributes[5].location = 5;
        aAttributes[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        aAttributes[5].offset = gpuMeshTransform ? offsetof(VkGpuMeshVertex, m_Tx) : offsetof(Vk3DVertex, m_Tx);

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &bindingDesc;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(aAttributes.size());
        vertexInput.pVertexAttributeDescriptions = aAttributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = topology;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = backfaceCulling ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = (renderPass == m_RenderPass) ? m_MsaaSamples : VK_SAMPLE_COUNT_1_BIT;
        multisampling.sampleShadingEnable = VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                              VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT |
                                              VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE;

        switch (m_BlendMode)
        {
        case BLEND_ADDITIVE:
        case BLEND_ADD_COLORS:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case BLEND_MULTIPLIED:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case BLEND_MOD_COLOR:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case BLEND_SUBTRACT_COLORS:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
            break;
        case BLEND_ALPHA:
        default:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        }

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkDynamicState aDynamicStates[] =
            {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(sizeof(aDynamicStates) / sizeof(aDynamicStates[0]));
        dynamicState.pDynamicStates = aDynamicStates;

        std::array<VkPipelineShaderStageCreateInfo, 2> aStages =
            {
                vertStage, fragStage};

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(aStages.size());
        pipelineInfo.pStages = aStages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_Layout3D;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
        {
            if (ownsFragmentModule)
            {
                vkDestroyShaderModule(m_Device, fragmentModule, nullptr);
            }
            if (ownsVertexModule)
            {
                vkDestroyShaderModule(m_Device, vertexModule, nullptr);
            }
            throw std::runtime_error("Failed to create Vulkan 3D pipeline.");
        }

        if (ownsFragmentModule)
        {
            vkDestroyShaderModule(m_Device, fragmentModule, nullptr);
        }
        if (ownsVertexModule)
        {
            vkDestroyShaderModule(m_Device, vertexModule, nullptr);
        }
        return pipeline;
    }
} // qci
