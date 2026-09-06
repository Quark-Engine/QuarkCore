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

namespace qc {

namespace {

const char* kRuntime2DVertexShader = R"glsl(
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

const char* kRuntime2DFragmentShader = R"glsl(
#version 450
layout(set = 0, binding = 0) uniform sampler2D texture0;
layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = texture(texture0, vTexCoord) * vColor;
}
)glsl";

const char* kRuntime3DVertexShader = R"glsl(
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
    vNormal = normalize(aNormal.xyz);
    vWorldPosition = aWorldPosition.xyz;
    vTangent = vec4(normalize(aTangent.xyz), aTangent.w);
}
)glsl";

const char* kRuntime3DFragmentShader = R"glsl(
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
    if (length(normal) > 0.001) {
        lit = ApplyLights(vWorldPosition, normal, baseColor);
    }
    outColor = vec4(lit, texel.a * vColor.a);
}
)glsl";

std::vector<uint32_t> CompileBuiltInShader(const char* source,
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

VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint32_t>& spirv) {
    if (device == VK_NULL_HANDLE || spirv.empty()) {
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode    = spirv.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

} // namespace

void QuarkVkPipeline::Initialize(VkDevice device,
                                 VkDescriptorSetLayout textureSetLayout,
                                 VkDescriptorSetLayout materialSetLayout) {
    m_device = device;
    m_textureSetLayout = textureSetLayout;
    m_materialSetLayout = materialSetLayout;
    EnsureLayouts();
}

void QuarkVkPipeline::EnsureLayouts() {
    if (m_device == VK_NULL_HANDLE) {
        throw std::runtime_error("Cannot create Vulkan pipeline layouts without a device.");
    }

    if (m_layout2D == VK_NULL_HANDLE) {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset     = 0;
        pushConstantRange.size       = sizeof(VkPushConstants2D);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount         = 1;
        layoutInfo.pSetLayouts            = &m_textureSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges    = &pushConstantRange;

        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_layout2D) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan pipeline layout.");
        }
    }

    if (m_layout3D == VK_NULL_HANDLE) {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset     = 0;
        pushConstantRange.size       = sizeof(Vk3DPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType                 = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount        = 1;
        layoutInfo.pSetLayouts           = &m_materialSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges   = &pushConstantRange;

        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_layout3D) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan 3D pipeline layout.");
        }
    }
}

void QuarkVkPipeline::SetBlendMode(int mode) {
    m_blendMode = mode;
    if (m_device == VK_NULL_HANDLE || m_renderPass == VK_NULL_HANDLE) {
        return;
    }

    CreatePipelines(m_renderPass, m_offscreenRenderPass, m_msaaSamples);
}

void QuarkVkPipeline::CreatePipelines(VkRenderPass renderPass,
                                      VkRenderPass offscreenRenderPass,
                                      VkSampleCountFlagBits msaaSamples) {
    m_renderPass = renderPass;
    m_offscreenRenderPass = offscreenRenderPass;
    m_msaaSamples = msaaSamples;

    DestroyPipelines();

    if (m_device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE) {
        return;
    }

    m_pipeline2D = Create2DPipeline(renderPass);
    if (offscreenRenderPass != VK_NULL_HANDLE) {
        m_offscreenPipeline2D = Create2DPipeline(offscreenRenderPass);
    }
    m_pipeline3DTri = Create3DPipeline(renderPass, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    m_pipeline3DLines = Create3DPipeline(renderPass, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    if (offscreenRenderPass != VK_NULL_HANDLE) {
        m_offscreenPipeline3DTri = Create3DPipeline(offscreenRenderPass, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        m_offscreenPipeline3DLines = Create3DPipeline(offscreenRenderPass, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    }
}

void QuarkVkPipeline::DestroyPipelines() {
    if (m_device == VK_NULL_HANDLE) {
        return;
    }

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
}

void QuarkVkPipeline::Shutdown() {
    DestroyPipelines();

    if (m_device == VK_NULL_HANDLE) {
        return;
    }

    if (m_layout3D != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_layout3D, nullptr);
        m_layout3D = VK_NULL_HANDLE;
    }
    if (m_layout2D != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_layout2D, nullptr);
        m_layout2D = VK_NULL_HANDLE;
    }

    m_device = VK_NULL_HANDLE;
    m_renderPass = VK_NULL_HANDLE;
    m_offscreenRenderPass = VK_NULL_HANDLE;
    m_textureSetLayout = VK_NULL_HANDLE;
    m_materialSetLayout = VK_NULL_HANDLE;
    m_backfaceCullingEnabled = false;
}

void QuarkVkPipeline::SetBackfaceCulling(bool enabled) {
    if (m_backfaceCullingEnabled == enabled || m_device == VK_NULL_HANDLE) {
        m_backfaceCullingEnabled = enabled;
        return;
    }

    m_backfaceCullingEnabled = enabled;

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

    if (m_renderPass != VK_NULL_HANDLE) {
        m_pipeline3DTri = Create3DPipeline(m_renderPass, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        m_pipeline3DLines = Create3DPipeline(m_renderPass, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    }
    if (m_offscreenRenderPass != VK_NULL_HANDLE) {
        m_offscreenPipeline3DTri = Create3DPipeline(m_offscreenRenderPass, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        m_offscreenPipeline3DLines = Create3DPipeline(m_offscreenRenderPass, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    }
}

VkPipeline QuarkVkPipeline::Create2DPipeline(VkRenderPass renderPass,
                                             VkShaderModule vertexModule,
                                             VkShaderModule fragmentModule) {
    if (m_device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    EnsureLayouts();

    bool ownsVertexModule = vertexModule == VK_NULL_HANDLE;
    bool ownsFragmentModule = fragmentModule == VK_NULL_HANDLE;
    if (ownsVertexModule) {
        const std::vector<uint32_t> vertCode = CompileBuiltInShader(
            kRuntime2DVertexShader, shaderc_vertex_shader, "built-in 2D vertex shader");
        vertexModule = CreateShaderModule(m_device, vertCode);
    }
    if (ownsFragmentModule) {
        const std::vector<uint32_t> fragCode = CompileBuiltInShader(
            kRuntime2DFragmentShader, shaderc_fragment_shader, "built-in 2D fragment shader");
        fragmentModule = CreateShaderModule(m_device, fragCode);
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
    colorBlendAttachment.blendEnable = VK_TRUE;

    switch (m_blendMode) {
        case BLEND_ADDITIVE:
        case BLEND_ADD_COLORS:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp         = VK_BLEND_OP_ADD;
            break;
        case BLEND_MULTIPLIED:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor  = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp         = VK_BLEND_OP_ADD;
            break;
        case BLEND_MOD_COLOR:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor  = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp         = VK_BLEND_OP_ADD;
            break;
        case BLEND_SUBTRACT_COLORS:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_REVERSE_SUBTRACT;
            colorBlendAttachment.srcAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp         = VK_BLEND_OP_REVERSE_SUBTRACT;
            break;
        case BLEND_ALPHA:
        default:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor  = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp         = VK_BLEND_OP_ADD;
            break;
    }

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
    pipelineInfo.layout              = m_layout2D;
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

VkPipeline QuarkVkPipeline::Create3DPipeline(VkRenderPass renderPass,
                                             VkPrimitiveTopology topology,
                                             VkShaderModule vertexModule,
                                             VkShaderModule fragmentModule) {
    if (m_device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    EnsureLayouts();

    const bool ownsVertexModule = vertexModule == VK_NULL_HANDLE;
    const bool ownsFragmentModule = fragmentModule == VK_NULL_HANDLE;
    if (ownsVertexModule) {
        const std::vector<uint32_t> vertCode = CompileBuiltInShader(
            kRuntime3DVertexShader, shaderc_vertex_shader, "built-in 3D vertex shader");
        vertexModule = CreateShaderModule(m_device, vertCode);
    }
    if (ownsFragmentModule) {
        const std::vector<uint32_t> fragCode = CompileBuiltInShader(
            kRuntime3DFragmentShader, shaderc_fragment_shader, "built-in 3D fragment shader");
        fragmentModule = CreateShaderModule(m_device, fragCode);
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

    std::array<VkVertexInputAttributeDescription, 6> attributes{};
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

    attributes[5].binding  = 0;
    attributes[5].location = 5;
    attributes[5].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[5].offset   = offsetof(Vk3DVertex, tx);

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
    colorBlendAttachment.blendEnable = VK_TRUE;

    switch (m_blendMode) {
        case BLEND_ADDITIVE:
        case BLEND_ADD_COLORS:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp         = VK_BLEND_OP_ADD;
            break;
        case BLEND_MULTIPLIED:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor  = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp         = VK_BLEND_OP_ADD;
            break;
        case BLEND_MOD_COLOR:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor  = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp         = VK_BLEND_OP_ADD;
            break;
        case BLEND_SUBTRACT_COLORS:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_REVERSE_SUBTRACT;
            colorBlendAttachment.srcAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp         = VK_BLEND_OP_REVERSE_SUBTRACT;
            break;
        case BLEND_ALPHA:
        default:
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor  = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp         = VK_BLEND_OP_ADD;
            break;
    }

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
    pipelineInfo.layout               = m_layout3D;
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

} // namespace qc