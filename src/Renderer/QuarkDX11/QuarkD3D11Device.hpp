#pragma once

#if defined(_WIN32)
#include "QuarkD3D11Common.hpp"
#include <wrl/client.h>

namespace qc {

class D3D11Device {
public:
    void Initialize();
    void Shutdown();

    ID3D11Device *Get() const { return m_device.Get(); }
    ID3D11DeviceContext *Context() const { return m_context.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
};

} // namespace qc
#endif