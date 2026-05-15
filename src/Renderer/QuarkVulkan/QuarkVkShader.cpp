#include "QuarkVkRenderer.hpp"

#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

namespace qc {

void QuarkVkRenderer::BeginShaderMode(const Shader& shader) {
    (void)shader;
}

void QuarkVkRenderer::EndShaderMode() {}

Shader QuarkVkRenderer::LoadShader(const char* vs, const char* fs) {
    (void)vs; (void)fs;
    return Shader{};
}

Shader QuarkVkRenderer::LoadShaderFromMemory(const char* vs, const char* fs) {
    (void)vs;(void)fs;
    return Shader{};
}

void QuarkVkRenderer::UnloadShader(Shader& shader) {
    (void)shader;
}

bool QuarkVkRenderer::isShaderValid(Shader& shader) {
    (void)shader;
    return false;
}

int QuarkVkRenderer::GetShaderLocation(const Shader& shader, const char* uniformName) {
    (void)shader; (void)uniformName;
    return -1;
}

int QuarkVkRenderer::GetShaderLocation(const Shader& shader, ShaderLocationIndex locIndex) {
    (void)shader; (void)locIndex;
    return -1;
}

int QuarkVkRenderer::GetShaderAttributeLocation(const Shader& shader, const char* attribName) {
    (void)shader; (void)attribName;
    return -1;
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

void QuarkVkRenderer::SetShaderValueMatrix(const Shader& shader, int locIndex, const float* mat) {
    (void)shader; (void)locIndex; (void)mat;
}

void QuarkVkRenderer::SetShaderValueSampler(const Shader& shader, int locIndex, int textureUnit) {
    (void)shader; (void)locIndex; (void)textureUnit;
}

}; // namespace qc