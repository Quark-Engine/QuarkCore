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

void QuarkVkRenderer::DrawTexture(const ITexture& texture, float x, float y, Color tint) {
    (void)texture; (void)x; (void)y; (void)tint;
}

void QuarkVkRenderer::DrawTextureV(const ITexture& texture, Vec2 position, Color tint) {
    DrawTexture(texture, position.x, position.y, tint);
}

void QuarkVkRenderer::DrawTextureEx(const ITexture& texture, Vec2 position, float rotation, float scale, Color tint) {
    (void)texture; (void)position; (void)rotation; (void)scale; (void)tint;
}

void QuarkVkRenderer::DrawTextureRec(const ITexture& texture, Rectangle source, Vec2 position, Color tint) {
    (void)texture; (void)source; (void)position; (void)tint;
}

void QuarkVkRenderer::DrawTexturePro(ITexture texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint) {
    (void)texture; (void)source; (void)dest; (void)origin; (void)rotation; (void)tint;
}

void QuarkVkRenderer::DrawTextureTiled(ITexture texture, float scale, Vec2 offset, Color tint) {
    (void)texture; (void)scale; (void)offset; (void)tint;
}

void QuarkVkRenderer::DrawTextureNPatch(ITexture texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint) {
    (void)texture; (void)source; (void)dest; (void)origin; (void)rotation; (void)tint;
}

ITexture QuarkVkRenderer::LoadTexture(const char* filePath) {
    TraceLog(LogLevel::Warn, "VULKAN", TextFormat("LoadTexture is not implemented for Vulkan yet: %s", filePath));
    return ITexture{};
}

ITexture QuarkVkRenderer::GetRenderTextureTexture(IRenderTexture target) {
    (void)target; return ITexture{};
}

void QuarkVkRenderer::UnloadTexture(ITexture& texture) {
    (void)texture;
}

bool QuarkVkRenderer::isTextureValid(ITexture& texture) {
    (void)texture;
    return false;
}

IRenderTexture QuarkVkRenderer::LoadRenderTexture(int width, int height) {
    return CreateRenderTargetInternal(width, height);
}

void QuarkVkRenderer::UnloadRenderTexture(IRenderTexture target) {
    (void)target;
}

bool QuarkVkRenderer::isRenderTextureValid(IRenderTexture& target) {
    (void)target;
    return false;
}

ITexture QuarkVkRenderer::GenCheckerTexture(int width, int height, int cellSize, Color colorA, Color colorB) {
    (void)width; (void)height; (void)cellSize; (void)colorA; (void)colorB;
    return ITexture{};
}

void QuarkVkRenderer::BeginTextureMode(IRenderTexture target) {
    (void)target;
}

void QuarkVkRenderer::EndTextureMode() {}

}; // namespace qc