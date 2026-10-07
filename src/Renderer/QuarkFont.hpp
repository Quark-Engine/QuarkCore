#ifndef __QUARK_FONT__
#define __QUARK_FONT__
#include <cstdint>
namespace qci {
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

} // namespace qci
#endif // __QUARK_FONT__
