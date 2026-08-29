#include "QuarkVkRenderer.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>

namespace qc {
namespace {

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

} // namespace

void QuarkVkRenderer::BeginShaderMode(const Shader& shader) {
    if (m_vkShaderCompiler.IsProgramValid(shader.id)) {
        m_vkShaderCompiler.SetCurrentProgram(shader.id);
    }
}

void QuarkVkRenderer::EndShaderMode() {
    m_vkShaderCompiler.ClearCurrentProgram();
}

Shader QuarkVkRenderer::LoadShader(const char* vs, const char* fs) {
    return FinishShaderProgram(m_vkShaderCompiler.LoadProgram(vs, fs));
}

Shader QuarkVkRenderer::LoadShaderFromMemory(const char* vs, const char* fs) {
    return FinishShaderProgram(m_vkShaderCompiler.CreateProgram(vs, fs));
}

Shader QuarkVkRenderer::FinishShaderProgram(uint32_t shaderId) {
    if (shaderId == 0) {
        return Shader{};
    }

    VkShaderProgramData& programData = *m_vkShaderCompiler.GetProgram(shaderId);

    if (programData.supports3D) {
        programData.pipeline3D = m_vkPipeline.Create3DPipeline(m_vkRenderPass.Get(),
                                                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                               programData.vertexModule,
                                                               programData.fragmentModule);
        if (m_vkRenderTarget.RenderPass() != VK_NULL_HANDLE) {
            programData.pipeline3DOffscreen = m_vkPipeline.Create3DPipeline(m_vkRenderTarget.RenderPass(),
                                                                            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                                            programData.vertexModule,
                                                                            programData.fragmentModule);
        }
        VkDescriptorPool descriptorPool3D = VK_NULL_HANDLE;
        if (Allocate3DDescriptorSet(programData.descriptorSet3D, descriptorPool3D)) {
            const VkTextureData* whiteTex = m_vkResources.Get(m_whiteTextureId);
            if (whiteTex != nullptr) {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = whiteTex->view;
                imageInfo.sampler = whiteTex->sampler;
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
        programData.pipeline = m_vkPipeline.Create2DPipeline(m_vkRenderPass.Get(),
                                                             programData.vertexModule,
                                                             programData.fragmentModule);
    }
    TraceLog(LogLevel::Info, "SHADER", TextFormat("[Vulkan] Shader program created successfully (ID: %u, Attributes: %zu, Uniforms: %zu, Pipeline: %p)",
        shaderId, programData.attributes.size(), programData.uniforms.size(), (void*)programData.pipeline));
    return Shader{shaderId};
}

void QuarkVkRenderer::UnloadShader(Shader& shader) {
    if (shader.id == 0) {
        return;
    }

    m_vkShaderCompiler.DestroyProgram(shader.id);
    shader = Shader{};
}

bool QuarkVkRenderer::isShaderValid(Shader& shader) {
    return m_vkShaderCompiler.IsProgramValid(shader.id);
}

int QuarkVkRenderer::GetShaderLocation(const Shader& shader, const char* uniformName) {
    return m_vkShaderCompiler.GetUniformLocation(shader.id, uniformName);
}

int QuarkVkRenderer::GetShaderLocation(const Shader& shader, ShaderLocationIndex locIndex) {
    return m_vkShaderCompiler.GetUniformLocation(shader.id, locIndex);
}

int QuarkVkRenderer::GetShaderAttributeLocation(const Shader& shader, const char* attribName) {
    return m_vkShaderCompiler.GetAttributeLocation(shader.id, attribName);
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, float value) {
    if (shader.id == 0 || locIndex < 0 || !m_vkShaderCompiler.IsProgramValid(shader.id)) {
        return;
    }
    m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_FLOAT, &value, sizeof(float), 1);
    Write3DShaderUniform(shader.id, locIndex, &value, sizeof(float));
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, int value) {
    if (shader.id == 0 || locIndex < 0 || !m_vkShaderCompiler.IsProgramValid(shader.id)) {
        return;
    }
    m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_INT, &value, sizeof(int), 1);
    Write3DShaderUniform(shader.id, locIndex, value);
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const Color& value) {
    if (shader.id == 0 || locIndex < 0 || !m_vkShaderCompiler.IsProgramValid(shader.id)) {
        return;
    }
    const float rgba[4] = {
        value.r / 255.0f,
        value.g / 255.0f,
        value.b / 255.0f,
        value.a / 255.0f
    };
    m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_VEC4, rgba, sizeof(float), 4);
    Write3DShaderUniform(shader.id, locIndex, rgba, sizeof(rgba));
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const qc::Vec2& value) {
    if (shader.id == 0 || locIndex < 0 || !m_vkShaderCompiler.IsProgramValid(shader.id)) {
        return;
    }
    const float vec[2] = { value.x, value.y };
    m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_VEC2, vec, sizeof(float), 2);
    Write3DShaderUniform(shader.id, locIndex, vec, sizeof(vec));
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const qc::Vec3& value) {
    if (shader.id == 0 || locIndex < 0 || !m_vkShaderCompiler.IsProgramValid(shader.id)) {
        return;
    }
    const float vec[3] = { value.x, value.y, value.z };
    m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_VEC3, vec, sizeof(float), 3);
    Write3DShaderUniform(shader.id, locIndex, vec, sizeof(vec));
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const qc::Vec4& value) {
    if (shader.id == 0 || locIndex < 0 || !m_vkShaderCompiler.IsProgramValid(shader.id)) {
        return;
    }
    const float vec[4] = { value.x, value.y, value.z, value.w };
    m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_VEC4, vec, sizeof(float), 4);
    Write3DShaderUniform(shader.id, locIndex, vec, sizeof(vec));
}

void QuarkVkRenderer::SetShaderValueMatrix(const Shader& shader, int locIndex, const float* mat) {
    if (shader.id == 0 || locIndex < 0 || mat == nullptr || !m_vkShaderCompiler.IsProgramValid(shader.id)) {
        return;
    }
    m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_FLOAT, mat, sizeof(float), 16);
    Write3DShaderUniform(shader.id, locIndex, mat, sizeof(float) * 16);
}

void QuarkVkRenderer::SetShaderValueSampler(const Shader& shader, int locIndex, int textureUnit) {
    if (shader.id == 0 || locIndex < 0 || !m_vkShaderCompiler.IsProgramValid(shader.id)) {
        return;
    }
    m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureUnit, sizeof(int), 1);
    VkShaderProgramData* program = m_vkShaderCompiler.GetProgram(shader.id);
    if (program != nullptr) {
        Update3DDescriptorSet(*program);
    }
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const void* value, int uniformType) {
    if (shader.id == 0 || locIndex < 0 || value == nullptr || !m_vkShaderCompiler.IsProgramValid(shader.id)) {
        return;
    }

    size_t writtenBytes = 0;
    switch (uniformType) {
        case SHADER_UNIFORM_FLOAT:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(float), 1);
            writtenBytes = sizeof(float);
            break;
        case SHADER_UNIFORM_VEC2:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(float), 2);
            writtenBytes = sizeof(float) * 2;
            break;
        case SHADER_UNIFORM_VEC3:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(float), 3);
            writtenBytes = sizeof(float) * 3;
            break;
        case SHADER_UNIFORM_VEC4:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(float), 4);
            writtenBytes = sizeof(float) * 4;
            break;
        case SHADER_UNIFORM_INT:
        case SHADER_UNIFORM_SAMPLER2D:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(int), 1);
            writtenBytes = sizeof(float);
            break;
        case SHADER_UNIFORM_IVEC2:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(int), 2);
            writtenBytes = sizeof(float) * 2;
            break;
        case SHADER_UNIFORM_IVEC3:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(int), 3);
            writtenBytes = sizeof(float) * 3;
            break;
        case SHADER_UNIFORM_IVEC4:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(int), 4);
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
    if (shader.id == 0 || locIndex < 0 || value == nullptr || count <= 0 || !m_vkShaderCompiler.IsProgramValid(shader.id)) {
        return;
    }

    switch (uniformType) {
        case SHADER_UNIFORM_FLOAT:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(float), count);
            break;
        case SHADER_UNIFORM_VEC2:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(float) * 2, count);
            break;
        case SHADER_UNIFORM_VEC3:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(float) * 3, count);
            break;
        case SHADER_UNIFORM_VEC4:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(float) * 4, count);
            break;
        case SHADER_UNIFORM_INT:
        case SHADER_UNIFORM_SAMPLER2D:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(int), count);
            break;
        case SHADER_UNIFORM_IVEC2:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(int) * 2, count);
            break;
        case SHADER_UNIFORM_IVEC3:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(int) * 3, count);
            break;
        case SHADER_UNIFORM_IVEC4:
            m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, uniformType, value, sizeof(int) * 4, count);
            break;
        default:
            break;
    }
}

void QuarkVkRenderer::SetShaderValueMatrix(const Shader& shader, int locIndex, const Matrix& mat) {
    if (shader.id == 0 || locIndex < 0 || !m_vkShaderCompiler.IsProgramValid(shader.id)) {
        return;
    }
    const float* data = mat.m;
    m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_FLOAT, data, sizeof(float), 16);
    Write3DShaderUniform(shader.id, locIndex, data, sizeof(float) * 16);
}

void QuarkVkRenderer::SetShaderValueTexture(const Shader& shader, int locIndex, const ITexture& texture) {
    if (shader.id == 0 || locIndex < 0 || !m_vkShaderCompiler.IsProgramValid(shader.id)) {
        return;
    }
    const int textureUnit = static_cast<int>(texture.id);
    m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureUnit, sizeof(int), 1);
}

void QuarkVkRenderer::SetShaderValueTextureUnit(const Shader& shader, int locIndex, const ITexture& texture, int textureUnit) {
    if (shader.id == 0 || locIndex < 0 || !m_vkShaderCompiler.IsProgramValid(shader.id)) {
        return;
    }
    const int textureId = texture.valid ? static_cast<int>(texture.id) : textureUnit;
    m_vkShaderCompiler.StoreUniformValue(shader.id, locIndex, SHADER_UNIFORM_SAMPLER2D, &textureId, sizeof(int), 1);
    VkShaderProgramData* program = m_vkShaderCompiler.GetProgram(shader.id);
    if (program != nullptr) {
        Update3DDescriptorSet(*program);
    }
}

void QuarkVkRenderer::Update3DDescriptorSet(VkShaderProgramData& program) {
    if (program.descriptorSet3D == VK_NULL_HANDLE) return;

    const VkTextureData* whiteTex = m_vkResources.Get(m_whiteTextureId);
    if (whiteTex == nullptr) return;

    const VkTextureData& white = *whiteTex;
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
        const VkTextureData* texture = m_vkResources.Get(static_cast<uint32_t>(textureId));
        if (texture != nullptr) {
            output.imageView = texture->view;
            output.sampler = texture->sampler;
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
                const VkTextureData* texture = m_vkResources.Get(static_cast<uint32_t>(textureId));
                if (texture != nullptr) {
                    shadowImages[i].imageView = texture->view;
                    shadowImages[i].sampler = texture->sampler;
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
    const auto* program = m_vkShaderCompiler.GetProgram(shaderId);
    if (program == nullptr) return;
    const auto nameIt = program->uniformNames.find(locIndex);
    if (nameIt == program->uniformNames.end()) return;
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