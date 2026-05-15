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

void QuarkVkRenderer::DrawText(const char* text, int x, int y, int fontSize, Color color) {
    (void)text; (void)x; (void)y; (void)fontSize; (void)color;
}

void QuarkVkRenderer::DrawTextEx(IFont font, const char* text, Vec2 position, float fontSize, float spacing, Color tint) {
    (void)font; (void)text; (void)position; (void)fontSize; (void)spacing; (void)tint;
}

Vec2 QuarkVkRenderer::MeasureTextEx(IFont font, const char* text, float fontSize, float spacing) {
    (void)font;
    const int length = text ? static_cast<int>(std::strlen(text)) : 0;
    return Vec2{ length * (fontSize * 0.5f + spacing), fontSize };
}

int QuarkVkRenderer::MeasureText(const char* text, int fontSize) {
    if (!text) return 0;
    return static_cast<int>(std::strlen(text)) * (fontSize / 2);
}

IFont QuarkVkRenderer::LoadFont(const char* filePath, int fontSize) {
    TraceLog(LogLevel::Warn, "VULKAN", TextFormat("LoadFont is not implemented for Vulkan yet: %s", filePath));
    (void)filePath; (void)fontSize;
    return IFont{};
}

void QuarkVkRenderer::UnloadFont(IFont& font) {
    (void)font;
}

}; // namespace qc