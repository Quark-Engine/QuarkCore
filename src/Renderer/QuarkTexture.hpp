#pragma once
#include <cstdint>

namespace qc {

struct ITexture {
    uint32_t id = 0;

    bool IsValid() const {
        return id != 0;
    }
};

struct IRenderTexture {
    unsigned int id = 0;
    ITexture texture;
    unsigned int depthId = 0;
};

}