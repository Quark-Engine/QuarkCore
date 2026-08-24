#pragma once

#if defined(_WIN32)
#include "QuarkCore/QuarkCore.hpp"
#include <d3d11.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace qc::d3d11 {

inline void ThrowIfFailed(HRESULT result, const char *operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << " failed (HRESULT 0x" << std::uppercase << std::hex
                << static_cast<unsigned long>(result) << ")";
        TraceLog(LogLevel::Error, "D3D11", message.str().c_str());
        throw std::runtime_error(message.str());
    }
}

} // namespace qc::d3d11
#endif