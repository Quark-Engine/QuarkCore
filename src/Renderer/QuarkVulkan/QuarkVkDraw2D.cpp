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

void QuarkVkRenderer::BeginMode2D(const Camera2D& camera) {
    m_camera2D = camera;
    m_camera2DActive = true;
}

void QuarkVkRenderer::EndMode2D() {
    m_camera2DActive = false;
}

void QuarkVkRenderer::DrawRectangle(float x, float y, float width, float height, Color color) {
    (void)x; (void)y; (void)width; (void)height; (void)color;
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
    (void)x1; (void)y1; (void)x2; (void)y2; (void)color;
}

void QuarkVkRenderer::DrawLineV(Vec2 start, Vec2 end, Color color) {
    DrawLine(start.x, start.y, end.x, end.y, color);
}

void QuarkVkRenderer::DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color) {
    (void)v1; (void)v2; (void)v3; (void)color;
}

void QuarkVkRenderer::DrawPoly(Vec2 center, int sides, float radius, float rotation, Color color) {
    (void)center; (void)sides; (void)radius; (void)rotation; (void)color;
}
    
}; // namespace qc