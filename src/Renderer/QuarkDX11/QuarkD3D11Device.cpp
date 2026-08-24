#include "QuarkD3D11Device.hpp"

#if defined(_WIN32)
#include <string>

namespace qc {

namespace {

const char* FeatureLevelName(D3D_FEATURE_LEVEL level) {
    switch (level) {
    case D3D_FEATURE_LEVEL_11_1:
        return "11.1";
    case D3D_FEATURE_LEVEL_11_0:
        return "11.0";
    default:
        return "unknown";
    }
}

const char* DriverName(D3D_DRIVER_TYPE driver) {
    return driver == D3D_DRIVER_TYPE_HARDWARE ? "Hardware" : "WARP";
}

const char* VendorName(UINT vendorId) {
    switch (vendorId) {
    case 0x10DE:
        return "NVIDIA";
    case 0x1002:
        return "AMD";
    case 0x8086:
        return "Intel";
    case 0x1414:
        return "Microsoft";
    default:
        return "Unknown";
    }
}

std::string AdapterName(const wchar_t* name) {
    if (!name) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        name,
        -1,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 1) {
        return {};
    }

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, name, -1, result.data(), size, nullptr, nullptr);
    result.resize(static_cast<size_t>(size - 1));
    return result;
}

} // namespace

void D3D11Device::Initialize()
{
    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    TraceLog(LogLevel::Info, "D3D11", "Creating Direct3D 11 device...");
    TraceLog(LogLevel::Trace, "D3D11", TextFormat("Device creation flags: 0x%08X", flags));

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL selectedLevel{};
    D3D_DRIVER_TYPE selectedDriver = D3D_DRIVER_TYPE_HARDWARE;
    bool debugLayerEnabled = flags != 0;

    const auto create = [&](D3D_DRIVER_TYPE driver, UINT creationFlags) {
        TraceLog(LogLevel::Trace,
                 "D3D11",
                 TextFormat("Trying %s device (Flags: 0x%08X)...",
                            DriverName(driver),
                            creationFlags));
        return D3D11CreateDevice(nullptr, driver, nullptr, creationFlags, levels, ARRAYSIZE(levels),
                                 D3D11_SDK_VERSION, &m_device, &selectedLevel, &m_context);
    };

    HRESULT result = create(D3D_DRIVER_TYPE_HARDWARE, flags);

    if (FAILED(result) && flags != 0)
    {
        TraceLog(LogLevel::Info, "D3D11", "Debug layer unavailable; retrying without it.");
        debugLayerEnabled = false;
        result = create(D3D_DRIVER_TYPE_HARDWARE, 0);
    }

    if (FAILED(result))
    {
        TraceLog(LogLevel::Warn, "D3D11", "Hardware device creation failed; falling back to WARP.");
        selectedDriver = D3D_DRIVER_TYPE_WARP;
        result = create(D3D_DRIVER_TYPE_WARP, 0);
    }

    d3d11::ThrowIfFailed(result, "D3D11CreateDevice");

    TraceLog(LogLevel::Info,
             "D3D11",
             TextFormat("Device created successfully (Driver: %s, Feature Level: %s, Debug: %s)",
                        DriverName(selectedDriver),
                        FeatureLevelName(selectedLevel),
                        debugLayerEnabled ? "enabled" : "disabled"));

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC adapterDescription{};

    if (SUCCEEDED(m_device.As(&dxgiDevice)) &&
        SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetDesc(&adapterDescription))) {
        const std::string adapterName = AdapterName(adapterDescription.Description);
        TraceLog(LogLevel::Info,
                 "D3D11",
                 TextFormat("GPU: %s (%s, Vendor ID: 0x%04X, Device ID: 0x%04X)",
                            adapterName.c_str(),
                            VendorName(adapterDescription.VendorId),
                            adapterDescription.VendorId,
                            adapterDescription.DeviceId));
        TraceLog(LogLevel::Trace,
                 "D3D11",
                 TextFormat("Memory: Dedicated %.2f GB, Shared %.2f GB",
                            static_cast<double>(adapterDescription.DedicatedVideoMemory) /
                                (1024.0 * 1024.0 * 1024.0),
                            static_cast<double>(adapterDescription.SharedSystemMemory) /
                                (1024.0 * 1024.0 * 1024.0)));
    } else {
        TraceLog(LogLevel::Warn, "D3D11", "DXGI adapter information is unavailable.");
    }

    TraceLog(LogLevel::Info, "D3D11", "API: Direct3D 11 (D3D11 SDK version 7).");
}

void D3D11Device::Shutdown()
{
    TraceLog(LogLevel::Info, "D3D11", "Shutting down Direct3D 11 device...");

    if (m_context)
    {
        m_context->ClearState();
    }

    m_context.Reset();
    m_device.Reset();

    TraceLog(LogLevel::Info, "D3D11", "Direct3D 11 device shut down successfully.");
}

} // namespace qc
#endif