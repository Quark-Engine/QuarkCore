#ifndef __QUARK_D3D11_DEVICE__
#define __QUARK_D3D11_DEVICE__

#if defined(_WIN32)
#include "QuarkD3D11Common.hpp"
#include <wrl/client.h>
namespace qci {
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
} // namespace qci
#endif
#endif // __QUARK_D3D11_DEVICE__
