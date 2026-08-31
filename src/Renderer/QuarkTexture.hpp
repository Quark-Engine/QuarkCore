#ifndef __QUARK_TEXTURE__
#define __QUARK_TEXTURE__
#include <cstdint>

namespace qc {

struct ITexture {
    uint32_t id = 0;
    int width = 0;
    int height = 0;
    int mipmaps = 1;
    int format = 0;
    bool valid = false;

    bool IsValid() const {
        return id != 0 && width > 0 && height > 0 && valid;
    }
};

struct IRenderTexture {
    unsigned int id = 0;
    ITexture texture;
    unsigned int depthId = 0;
};

}

#endif // __QUARK_TEXTURE__
