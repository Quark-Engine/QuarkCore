#include "QuarkInternal.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include <cstring>

namespace qc {

bool LoadImageFile(const char* path, ImageFileData& out, int desiredChannels) {
    out = {};

    if (path == nullptr || path[0] == '\0') {
        TraceLog(LogLevel::Warn, "IMAGE", "Cannot load image: path is null or empty");
        return false;
    }

    TraceLog(LogLevel::Trace, "IMAGE", TextFormat("Decoding image file: %s (requested channels: %d)", path, desiredChannels));

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* decoded = stbi_load(path, &width, &height, &channels, desiredChannels);
    if (decoded == nullptr) {
        const char* reason = stbi_failure_reason();
        TraceLog(LogLevel::Error, "IMAGE", TextFormat("Failed to decode image '%s': %s", path, reason ? reason : "Unknown STB error"));
        return false;
    }

    const int actualChannels = (desiredChannels > 0) ? desiredChannels : channels;
    if (width <= 0 || height <= 0 || actualChannels <= 0) {
        TraceLog(LogLevel::Error, "IMAGE", TextFormat("Invalid image dimensions for '%s': %dx%d (%d channels)", path, width, height, actualChannels));
        stbi_image_free(decoded);
        return false;
    }

    const size_t sizeBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(actualChannels);
    out.width = width;
    out.height = height;
    out.channels = actualChannels;
    out.pixels.resize(sizeBytes);
    std::memcpy(out.pixels.data(), decoded, sizeBytes);

    stbi_image_free(decoded);

    TraceLog(LogLevel::Info, "IMAGE", TextFormat("Image decoded successfully: %s (%dx%d, %d channels [source: %d], %zu bytes)",
        path, width, height, actualChannels, channels, sizeBytes));
    return true;
}

} // namespace qc
