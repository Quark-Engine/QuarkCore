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

static constexpr float kPi = 3.14159265358979323846f;

static float NormalizeColorComponent(std::uint8_t value) {
    return static_cast<float>(value) / 255.0f;
}

Vec2 QuarkVkRenderer::ApplyCameraTransform(Vec2 position) const {
    if (!m_camera2DActive) {
        return position;
    }
    return GetWorldToScreen2D(position, m_camera2D);
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
    (void)lineWidth;

    DrawLine(rectangle.x, rectangle.y, rectangle.x + rectangle.width, rectangle.y, color);
    DrawLine(rectangle.x + rectangle.width, rectangle.y, rectangle.x + rectangle.width, rectangle.y + rectangle.height, color);
    DrawLine(rectangle.x + rectangle.width, rectangle.y + rectangle.height, rectangle.x, rectangle.y + rectangle.height, color);
    DrawLine(rectangle.x, rectangle.y + rectangle.height, rectangle.x, rectangle.y, color);
}

void QuarkVkRenderer::DrawRectangleRounded(Rectangle rectangle, float roundness, int segments, Color color) {
    const float radius = roundness * std::min(rectangle.width, rectangle.height) / 2.0f;
    const float clampedRadius = std::min(radius, std::min(rectangle.width / 2.0f, rectangle.height / 2.0f));

    if (clampedRadius <= 0.0f) {
        DrawRectangle(rectangle, color);
        return;
    }

    const float x = rectangle.x;
    const float y = rectangle.y;
    const float w = rectangle.width;
    const float h = rectangle.height;

    DrawCircle(x + clampedRadius, y + clampedRadius, clampedRadius, color);
    DrawCircle(x + w - clampedRadius, y + clampedRadius, clampedRadius, color);
    DrawCircle(x + w - clampedRadius, y + h - clampedRadius, clampedRadius, color);
    DrawCircle(x + clampedRadius, y + h - clampedRadius, clampedRadius, color);

    DrawRectangle(x + clampedRadius, y, w - 2.0f * clampedRadius, h, color);
    DrawRectangle(x, y + clampedRadius, w, h - 2.0f * clampedRadius, color);

    (void)segments;
}

void QuarkVkRenderer::DrawCircle(float cx, float cy, float r, Color color) {
    if (r <= 0.0f) {
        return;
    }

    constexpr int segments = 48;
    const float angleStep = 2.0f * kPi / static_cast<float>(segments);

    Vec2 previous{ cx + r, cy };
    for (int i = 1; i <= segments; ++i) {
        const float angle = static_cast<float>(i) * angleStep;
        const Vec2 current{ cx + std::cos(angle) * r, cy + std::sin(angle) * r };
        DrawTriangle({ cx, cy }, previous, current, color);
        previous = current;
    }
}

void QuarkVkRenderer::DrawCircleLines(float cx, float cy, float r, Color color) {
    if (r <= 0.0f) {
        return;
    }

    constexpr int segments = 36;
    Vec2 previous{ cx + r, cy };

    for (int i = 1; i <= segments; ++i) {
        const float angle = (2.0f * kPi * static_cast<float>(i)) / static_cast<float>(segments);
        const Vec2 current{ cx + std::cos(angle) * r, cy + std::sin(angle) * r };
        DrawLine(previous.x, previous.y, current.x, current.y, color);
        previous = current;
    }
}

void QuarkVkRenderer::DrawEllipse(float cx, float cy, float rH, float rV, Color color) {
    if (rH <= 0.0f || rV <= 0.0f) {
        return;
    }

    constexpr int segments = 36;
    const float angleStep = 2.0f * kPi / static_cast<float>(segments);

    Vec2 previous{ cx + rH, cy };
    for (int i = 1; i <= segments; ++i) {
        const float angle = static_cast<float>(i) * angleStep;
        const Vec2 current{ cx + std::cos(angle) * rH, cy + std::sin(angle) * rV };
        DrawTriangle({ cx, cy }, previous, current, color);
        previous = current;
    }
}

void QuarkVkRenderer::DrawLine(float x1, float y1, float x2, float y2, Color color) {
    const VkDescriptorSet whiteDs = m_vkResources.DescriptorSet(m_whiteTextureId);
    if (whiteDs == VK_NULL_HANDLE) {
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

    AppendQuad(whiteDs,
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
    const VkDescriptorSet whiteDs = m_vkResources.DescriptorSet(m_whiteTextureId);
    if (whiteDs == VK_NULL_HANDLE) {
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

    const Vec2 p1 = ApplyCameraTransform(v1);
    const Vec2 p2 = ApplyCameraTransform(v2);
    const Vec2 p3 = ApplyCameraTransform(v3);

    const uint32_t base = static_cast<uint32_t>(vertices->size());
    vertices->push_back({ p1.x, p1.y, 0.f, 0.f, r, g, b, a });
    vertices->push_back({ p2.x, p2.y, 0.f, 0.f, r, g, b, a });
    vertices->push_back({ p3.x, p3.y, 0.f, 0.f, r, g, b, a });

    const uint32_t firstIndex = static_cast<uint32_t>(indices->size());
    indices->push_back(base + 0);
    indices->push_back(base + 1);
    indices->push_back(base + 2);

    if (!drawItems->empty() &&
        drawItems->back().descriptorSet == whiteDs &&
        drawItems->back().shaderProgramId == m_vkShaderCompiler.CurrentProgramId() &&
        drawItems->back().firstIndex + drawItems->back().indexCount == firstIndex) {
        drawItems->back().indexCount += 3;
    } else {
        drawItems->push_back({ 0, m_vkShaderCompiler.CurrentProgramId(), whiteDs, firstIndex, 3 });
    }
}

void QuarkVkRenderer::DrawPoly(Vec2 center, int sides, float radius, float rotation, Color color) {
    if (sides < 3 || radius <= 0.0f) {
        return;
    }

    const float rotationRad = rotation * kPi / 180.0f;
    for (int i = 0; i < sides; ++i) {
        const float a0 = (static_cast<float>(i) / static_cast<float>(sides)) * 2.0f * kPi + rotationRad;
        const float a1 = (static_cast<float>(i + 1) / static_cast<float>(sides)) * 2.0f * kPi + rotationRad;

        const Vec2 p0{ center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius };
        const Vec2 p1{ center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius };
        DrawTriangle(center, p0, p1, color);
    }
}
    
}; // namespace qc