#include "QuarkInternal.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include <cstring>

namespace qc {

bool LoadImageFile(const char* path, ImageFileData& out, int desiredChannels) {
    out = {};

    if (path == nullptr || path[0] == '\0') {
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* decoded = stbi_load(path, &width, &height, &channels, desiredChannels);
    if (decoded == nullptr) {
        return false;
    }

    const int actualChannels = (desiredChannels > 0) ? desiredChannels : channels;
    if (width <= 0 || height <= 0 || actualChannels <= 0) {
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
    return true;
}

} // namespace qc
