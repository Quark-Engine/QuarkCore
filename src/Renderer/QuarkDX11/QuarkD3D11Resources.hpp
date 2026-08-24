#pragma once

#if defined(_WIN32)
#include "QuarkD3D11Common.hpp"
#include <wrl/client.h>

namespace qc {

class D3D11Resources {
public:
    void Initialize(ID3D11Device *device);
    void Shutdown();
    ID3D11Buffer *CreateDynamicVertexBuffer(ID3D11Device *device, UINT byteWidth);
    void UpdateDynamicBuffer(ID3D11DeviceContext *context, ID3D11Buffer *buffer, const void *data,
                             size_t size) const;
    ID3D11Buffer *VertexBuffer() const { return m_triangleVertexBuffer.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_triangleVertexBuffer;
};

} // namespace qc
#endif