#include "QuarkD3D11Resources.hpp"

#include <cstring>

#if defined(_WIN32)
namespace qc {

void D3D11Resources::Initialize(ID3D11Device *device)
{
    TraceLog(LogLevel::Trace, "D3D11", "Creating dynamic triangle vertex buffer...");
    CreateDynamicVertexBuffer(device, sizeof(float) * 6 * 3);
    TraceLog(LogLevel::Info, "D3D11", "Dynamic triangle vertex buffer created (72 bytes).");
}

ID3D11Buffer *D3D11Resources::CreateDynamicVertexBuffer(ID3D11Device *device, UINT byteWidth)
{
    TraceLog(LogLevel::Trace, "D3D11",
             TextFormat("Creating dynamic vertex buffer (%u bytes).", byteWidth));

    D3D11_BUFFER_DESC description{};
    description.ByteWidth = byteWidth;
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    d3d11::ThrowIfFailed(device->CreateBuffer(&description, nullptr, &m_triangleVertexBuffer),
                         "ID3D11Device::CreateBuffer");

    return m_triangleVertexBuffer.Get();
}

void D3D11Resources::UpdateDynamicBuffer(ID3D11DeviceContext *context, ID3D11Buffer *buffer,
                                         const void *data, size_t size) const
{
    D3D11_MAPPED_SUBRESOURCE mapped{};

    d3d11::ThrowIfFailed(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped),
                         "ID3D11DeviceContext::Map");

    std::memcpy(mapped.pData, data, size);
    context->Unmap(buffer, 0);

}

void D3D11Resources::Shutdown()
{
    TraceLog(LogLevel::Trace, "D3D11", "Releasing D3D11 resources...");
    m_triangleVertexBuffer.Reset();
    TraceLog(LogLevel::Trace, "D3D11", "D3D11 resources released.");
}

} // namespace qc
#endif