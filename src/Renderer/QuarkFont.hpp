#pragma once
#include <cstdint>

namespace qc {

struct IFont {
    uint32_t id = 0;

    bool IsValid() const {
        return id != 0;
    }
};

struct FontMetrics {
    float ascent = 0.0f;
    float descent = 0.0f;
    float lineHeight = 0.0f;
    float pixelSize = 0.0f;
};

}