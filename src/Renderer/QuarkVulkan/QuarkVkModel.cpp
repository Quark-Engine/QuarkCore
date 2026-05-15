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

Model QuarkVkRenderer::LoadModel(const char* filePath) {
    TraceLog(LogLevel::Warn, "VULKAN", TextFormat("LoadModel is not implemented for Vulkan yet: %s", filePath));
    return Model{};
}

void QuarkVkRenderer::UnloadModel(Model& model) {
    (void)model;
}

void QuarkVkRenderer::DrawModel(const Model& model, const Vec3& position, float scale,
                                 float rotationX, float rotationY, float rotationZ) {
    (void)model; (void)position; (void)scale; (void)rotationX; (void)rotationY; (void)rotationZ;
}

void QuarkVkRenderer::DrawModelEx(const Model& model, const Mat4& transform) {
    (void)model; (void)transform;
}

void QuarkVkRenderer::DrawModelEx(const Model& model, const Mat4& transform, Color tint) {
    (void)model; (void)transform; (void)tint;
}

void QuarkVkRenderer::UploadMesh(Mesh& mesh, bool dynamic) {
    (void)mesh; (void)dynamic;
}

void QuarkVkRenderer::UpdateMeshBuffer(Mesh& mesh, int index, const void* data, int dataSize, int offset) {
    (void)mesh; (void)index; (void)data; (void)dataSize; (void)offset;
}

void QuarkVkRenderer::UnloadMesh(Mesh& mesh) {
    (void)mesh;
}

void QuarkVkRenderer::DrawMesh(const Mesh& mesh, const Material& material, const Mat4& transform) {
    (void)mesh; (void)material; (void)transform;
}

void QuarkVkRenderer::DrawMeshInstanced(const Mesh& mesh, const Material& material, const Mat4* transforms, int instances) {
    (void)mesh; (void)material; (void)transforms; (void)instances;
}

}; // namespace qc