#include "QuarkVkRenderer.hpp"

#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

namespace qc {

static float NormalizeColorComponent(std::uint8_t value) {
    return static_cast<float>(value) / 255.0f;
}

void QuarkVkRenderer::BeginMode2D(const Camera2D& camera) {
    m_camera2D = camera;
    m_camera2DActive = true;
}

void QuarkVkRenderer::EndMode2D() {
    m_camera2DActive = false;
}

void QuarkVkRenderer::DrawRectangle(float x, float y, float width, float height, Color color) {
    PushQuad(x, y, width, height, color);
}

void QuarkVkRenderer::DrawRectangle(const Rectangle& rectangle, Color color) {
    DrawRectangle(rectangle.x, rectangle.y, rectangle.width, rectangle.height, color);
}

void QuarkVkRenderer::DrawRectangleV(Vec2 position, Vec2 size, Color color) {
    DrawRectangle(position.x, position.y, size.x, size.y, color);
}

void QuarkVkRenderer::DrawRectangleLines(Rectangle rectangle, float lineWidth, Color color) {
    (void)rectangle; (void)lineWidth; (void)color;
}

void QuarkVkRenderer::DrawRectangleRounded(Rectangle rectangle, float roundness, int segments, Color color) {
    (void)rectangle; (void)roundness; (void)segments; (void)color;
}

void QuarkVkRenderer::DrawCircle(float cx, float cy, float r, Color color) {
    (void)cx; (void)cy; (void)r; (void)color;
}

void QuarkVkRenderer::DrawCircleLines(float cx, float cy, float r, Color color) {
    (void)cx; (void)cy; (void)r; (void)color;
}

void QuarkVkRenderer::DrawEllipse(float cx, float cy, float rH, float rV, Color color) {
    (void)cx; (void)cy; (void)rH; (void)rV; (void)color;
}

void QuarkVkRenderer::DrawLine(float x1, float y1, float x2, float y2, Color color) {
    auto it = m_textures.find(m_whiteTextureId);
    if (it == m_textures.end()) {
        return;
    }

    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.f) {
        return;
    }

    const float angle = std::atan2(dy, dx);
    const float halfThickness = 0.5f;
    const float c = std::cos(angle);
    const float s = std::sin(angle);

    const auto rotate = [&](float px, float py) -> Vec2 {
        return Vec2{
            x1 + px * c - py * s,
            y1 + px * s + py * c
        };
    };

    const Vec2 a = rotate(0.f, -halfThickness);
    const Vec2 b = rotate(length, -halfThickness);
    const Vec2 c0 = rotate(length, halfThickness);
    const Vec2 d = rotate(0.f, halfThickness);

    const float r = NormalizeColorComponent(color.r);
    const float g = NormalizeColorComponent(color.g);
    const float bCol = NormalizeColorComponent(color.b);
    const float aCol = NormalizeColorComponent(color.a);

    AppendQuad(it->second.descriptorSet,
               a.x, a.y,
               b.x, b.y,
               c0.x, c0.y,
               d.x, d.y,
               r, g, bCol, aCol);
}

void QuarkVkRenderer::DrawLineV(Vec2 start, Vec2 end, Color color) {
    DrawLine(start.x, start.y, end.x, end.y, color);
}

void QuarkVkRenderer::DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color) {
    auto it = m_textures.find(m_whiteTextureId);
    if (it == m_textures.end()) {
        return;
    }

    const float r = NormalizeColorComponent(color.r);
    const float g = NormalizeColorComponent(color.g);
    const float b = NormalizeColorComponent(color.b);
    const float a = NormalizeColorComponent(color.a);

    std::vector<VkBatchVertex>* vertices = &m_batchVertices;
    std::vector<uint32_t>* indices = &m_batchIndices;
    std::vector<VkDrawItem>* drawItems = &m_batchDrawItems;
    if (m_activeRenderTargetId != 0) {
        auto rt = m_renderTargets.find(m_activeRenderTargetId);
        if (rt != m_renderTargets.end()) {
            vertices = &rt->second.vertices;
            indices = &rt->second.indices;
            drawItems = &rt->second.drawItems;
        }
    }

    const uint32_t base = static_cast<uint32_t>(vertices->size());
    vertices->push_back({ v1.x, v1.y, 0.f, 0.f, r, g, b, a });
    vertices->push_back({ v2.x, v2.y, 0.f, 0.f, r, g, b, a });
    vertices->push_back({ v3.x, v3.y, 0.f, 0.f, r, g, b, a });

    const uint32_t firstIndex = static_cast<uint32_t>(indices->size());
    indices->push_back(base + 0);
    indices->push_back(base + 1);
    indices->push_back(base + 2);

    if (!drawItems->empty() &&
        drawItems->back().descriptorSet == it->second.descriptorSet &&
        drawItems->back().firstIndex + drawItems->back().indexCount == firstIndex) {
        drawItems->back().indexCount += 3;
    } else {
        drawItems->push_back({ 0, it->second.descriptorSet, firstIndex, 3 });
    }
}

void QuarkVkRenderer::DrawPoly(Vec2 center, int sides, float radius, float rotation, Color color) {
    (void)center; (void)sides; (void)radius; (void)rotation; (void)color;
}
    
}; // namespace qc