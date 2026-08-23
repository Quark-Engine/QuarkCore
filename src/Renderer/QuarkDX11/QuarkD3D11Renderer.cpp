#include "QuarkD3D11Renderer.hpp"

#if defined(_WIN32)
#include <SDL3/SDL_video.h>
#include <d3dcompiler.h>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace qc {

namespace {

void ThrowIfFailed(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << operation << " failed (HRESULT 0x" << std::uppercase << std::hex
                << static_cast<unsigned long>(result) << ")";
        TraceLog(LogLevel::Error, "D3D11", message.str().c_str());
        throw std::runtime_error(message.str());
    }
}

const char* FeatureLevelName(D3D_FEATURE_LEVEL level) {
    switch (level) {
        case D3D_FEATURE_LEVEL_11_1: return "11.1";
        case D3D_FEATURE_LEVEL_11_0: return "11.0";
        default: return "unknown";
    }
}

const char* VendorName(UINT vendorId) {
    switch (vendorId) {
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        case 0x1414: return "Microsoft";
        default: return "Unknown";
    }
}

std::string AdapterName(const wchar_t* name) {
    if (!name) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, name, -1, result.data(), size, nullptr, nullptr);
    result.resize(static_cast<size_t>(size - 1));
    return result;
}

} // namespace

QuarkD3D11Renderer::~QuarkD3D11Renderer() {
    Shutdown();
}

void QuarkD3D11Renderer::Init(SDL_Window* window, int width, int height) {
    TraceLog(LogLevel::Info, "D3D11", TextFormat("Initializing renderer (Window: %dx%d)", width, height));
    if (!window) {
        TraceLog(LogLevel::Error, "D3D11", "Initialization failed: SDL window is null.");
        throw std::runtime_error("D3D11 requires a valid SDL window");
    }

    m_window = window;
    m_width = width;
    m_height = height;
    m_lastFrame = std::chrono::steady_clock::now();

    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
        properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) {
        TraceLog(LogLevel::Error, "D3D11", "SDL did not provide a Win32 window handle.");
        throw std::runtime_error("SDL did not provide a Win32 window handle");
    }
    TraceLog(LogLevel::Trace, "D3D11", TextFormat("Win32 window handle acquired: %p", hwnd));

    DXGI_SWAP_CHAIN_DESC swapChainDesc{};
    swapChainDesc.BufferDesc.Width = static_cast<UINT>(width);
    swapChainDesc.BufferDesc.Height = static_cast<UINT>(height);
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.OutputWindow = hwnd;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT creationFlags = 0;
#if defined(_DEBUG)
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    D3D_FEATURE_LEVEL selectedLevel{};
    const char* selectedDriver = "Hardware";
    bool debugLayerEnabled = creationFlags != 0;
    auto createDevice = [&](D3D_DRIVER_TYPE driverType, UINT flags) {
        const char* driverName = driverType == D3D_DRIVER_TYPE_HARDWARE ? "Hardware" : "WARP";
        TraceLog(LogLevel::Trace, "D3D11", TextFormat("Creating %s device (Flags: 0x%X)...", driverName, flags));
        return D3D11CreateDeviceAndSwapChain(
            nullptr, driverType, nullptr, flags,
            featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
            &swapChainDesc, &m_swapChain, &m_device, &selectedLevel, &m_context);
    };

    HRESULT result = createDevice(D3D_DRIVER_TYPE_HARDWARE, creationFlags);
    if (FAILED(result) && creationFlags != 0) {
        TraceLog(LogLevel::Info, "D3D11", TextFormat("D3D11 Debug Layer unavailable (HRESULT: 0x%08X, DXGI_ERROR_SDK_COMPONENT_MISSING), retrying without it.", static_cast<unsigned long>(result)));
        debugLayerEnabled = false;
        result = createDevice(D3D_DRIVER_TYPE_HARDWARE, 0);
    }
    if (FAILED(result)) {
        TraceLog(LogLevel::Warn, "D3D11", TextFormat("Hardware device creation failed (HRESULT: 0x%08X), falling back to WARP.", static_cast<unsigned long>(result)));
        selectedDriver = "WARP";
        result = createDevice(D3D_DRIVER_TYPE_WARP, 0);
    }
    ThrowIfFailed(result, "D3D11CreateDeviceAndSwapChain");
    TraceLog(LogLevel::Info, "D3D11", TextFormat("Device created successfully (Driver: %s, Feature Level: %s, Debug: %s)",
        selectedDriver, FeatureLevelName(selectedLevel),
        debugLayerEnabled ? "enabled" : "disabled"));

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC adapterDesc{};
    if (SUCCEEDED(m_device.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetDesc(&adapterDesc))) {
        const std::string adapterName = AdapterName(adapterDesc.Description);
        TraceLog(LogLevel::Info, "D3D11", TextFormat("GPU: %s (%s, Vendor ID: 0x%04X, Device ID: 0x%04X)",
            adapterName.c_str(), VendorName(adapterDesc.VendorId), adapterDesc.VendorId, adapterDesc.DeviceId));
        TraceLog(LogLevel::Info, "D3D11", TextFormat("Memory: Dedicated %.2f GB, Shared %.2f GB",
            static_cast<double>(adapterDesc.DedicatedVideoMemory) / (1024.0 * 1024.0 * 1024.0),
            static_cast<double>(adapterDesc.SharedSystemMemory) / (1024.0 * 1024.0 * 1024.0)));
    } else {
        TraceLog(LogLevel::Warn, "D3D11", "DXGI adapter information is unavailable.");
    }
    TraceLog(LogLevel::Info, "D3D11", "API: Direct3D 11 (D3D11 SDK version 7).");

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    TraceLog(LogLevel::Trace, "D3D11", "Creating back-buffer render target view...");
    ThrowIfFailed(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "IDXGISwapChain::GetBuffer");
    ThrowIfFailed(m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_renderTarget),
                 "ID3D11Device::CreateRenderTargetView");

    static constexpr char vertexShaderSource[] = R"(
        struct VSInput { float2 position : POSITION; float4 color : COLOR; };
        struct VSOutput { float4 position : SV_POSITION; float4 color : COLOR; };
        VSOutput main(VSInput input) {
            VSOutput output;
            output.position = float4(input.position, 0.0, 1.0);
            output.color = input.color;
            return output;
        }
    )";
    static constexpr char pixelShaderSource[] = R"(
        struct PSInput { float4 position : SV_POSITION; float4 color : COLOR; };
        float4 main(PSInput input) : SV_TARGET { return input.color; }
    )";

    Microsoft::WRL::ComPtr<ID3DBlob> vertexBytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelBytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    TraceLog(LogLevel::Trace, "SHADER", "[D3D11] Compiling built-in vertex shader (vs_5_0)...");
    ThrowIfFailed(D3DCompile(vertexShaderSource, sizeof(vertexShaderSource) - 1,
                             nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0,
                             &vertexBytecode, &errors), "D3DCompile vertex shader");
    TraceLog(LogLevel::Info, "SHADER", TextFormat("[D3D11] Built-in vertex shader compiled (%zu bytes).", vertexBytecode->GetBufferSize()));
    TraceLog(LogLevel::Trace, "SHADER", "[D3D11] Compiling built-in pixel shader (ps_5_0)...");
    ThrowIfFailed(D3DCompile(pixelShaderSource, sizeof(pixelShaderSource) - 1,
                             nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0,
                             &pixelBytecode, &errors), "D3DCompile pixel shader");
    TraceLog(LogLevel::Info, "SHADER", TextFormat("[D3D11] Built-in pixel shader compiled (%zu bytes).", pixelBytecode->GetBufferSize()));
    TraceLog(LogLevel::Trace, "D3D11", "Creating built-in shader objects...");
    ThrowIfFailed(m_device->CreateVertexShader(vertexBytecode->GetBufferPointer(),
                                               vertexBytecode->GetBufferSize(), nullptr,
                                               &m_triangleVertexShader),
                  "ID3D11Device::CreateVertexShader");
    ThrowIfFailed(m_device->CreatePixelShader(pixelBytecode->GetBufferPointer(),
                                              pixelBytecode->GetBufferSize(), nullptr,
                                              &m_trianglePixelShader),
                  "ID3D11Device::CreatePixelShader");

    const D3D11_INPUT_ELEMENT_DESC inputElements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
         D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    ThrowIfFailed(m_device->CreateInputLayout(inputElements, ARRAYSIZE(inputElements),
                                              vertexBytecode->GetBufferPointer(),
                                              vertexBytecode->GetBufferSize(),
                                              &m_triangleInputLayout),
                  "ID3D11Device::CreateInputLayout");

    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = sizeof(float) * 6 * 3;
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ThrowIfFailed(m_device->CreateBuffer(&bufferDesc, nullptr, &m_triangleVertexBuffer),
                  "ID3D11Device::CreateBuffer");

    D3D11_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.DepthClipEnable = TRUE;
    ThrowIfFailed(m_device->CreateRasterizerState(&rasterizerDesc, &m_triangleRasterizerState),
                  "ID3D11Device::CreateRasterizerState");
    RefreshViewport();
    TraceLog(LogLevel::Info, "D3D11", "Renderer initialized successfully.");
}

void QuarkD3D11Renderer::Shutdown() {
    TraceLog(LogLevel::Info, "D3D11", "Shutting down renderer...");
    if (m_context) m_context->ClearState();
    m_renderTarget.Reset();
    m_triangleVertexBuffer.Reset();
    m_triangleRasterizerState.Reset();
    m_triangleInputLayout.Reset();
    m_trianglePixelShader.Reset();
    m_triangleVertexShader.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();
    m_window = nullptr;
    m_drawing = false;
    TraceLog(LogLevel::Info, "D3D11", "Renderer shut down.");
}

void QuarkD3D11Renderer::RefreshViewport() {
    if (!m_context) return;
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);
    TraceLog(LogLevel::Trace, "D3D11", TextFormat("Viewport refreshed (%dx%d).", m_width, m_height));
}

void QuarkD3D11Renderer::BeginDrawing() {
    const auto now = std::chrono::steady_clock::now();
    m_frameTime = std::chrono::duration<float>(now - m_lastFrame).count();
    m_lastFrame = now;
    m_drawing = true;
    if (m_context && m_renderTarget) m_context->OMSetRenderTargets(1, m_renderTarget.GetAddressOf(), nullptr);
}

void QuarkD3D11Renderer::EndDrawing() {
    if (m_swapChain) {
        const UINT syncInterval = m_vsync ? 1u : 0u;
        const HRESULT result = m_swapChain->Present(syncInterval, 0);
        if (result == DXGI_STATUS_OCCLUDED) m_shouldClose = false;
        else ThrowIfFailed(result, "IDXGISwapChain::Present");
    }
    m_drawing = false;
}

void QuarkD3D11Renderer::ClearBackground(Color color) {
    if (!m_context || !m_renderTarget) return;
    const float clearColor[] = {
        static_cast<float>(color.r) / 255.0f,
        static_cast<float>(color.g) / 255.0f,
        static_cast<float>(color.b) / 255.0f,
        static_cast<float>(color.a) / 255.0f
    };
    m_context->ClearRenderTargetView(m_renderTarget.Get(), clearColor);
}

void QuarkD3D11Renderer::DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color) {
    if (!m_context || !m_triangleVertexBuffer) return;

    const float red = static_cast<float>(color.r) / 255.0f;
    const float green = static_cast<float>(color.g) / 255.0f;
    const float blue = static_cast<float>(color.b) / 255.0f;
    const float alpha = static_cast<float>(color.a) / 255.0f;
    const Vec2 points[] = {v1, v2, v3};
    float vertices[18]{};
    for (int i = 0; i < 3; ++i) {
        vertices[i * 6] = (points[i].x / static_cast<float>(m_width)) * 2.0f - 1.0f;
        vertices[i * 6 + 1] = 1.0f - (points[i].y / static_cast<float>(m_height)) * 2.0f;
        vertices[i * 6 + 2] = red;
        vertices[i * 6 + 3] = green;
        vertices[i * 6 + 4] = blue;
        vertices[i * 6 + 5] = alpha;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    ThrowIfFailed(m_context->Map(m_triangleVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD,
                                 0, &mapped), "ID3D11DeviceContext::Map");
    std::memcpy(mapped.pData, vertices, sizeof(vertices));
    m_context->Unmap(m_triangleVertexBuffer.Get(), 0);

    const UINT stride = sizeof(float) * 6;
    const UINT offset = 0;
    m_context->IASetInputLayout(m_triangleInputLayout.Get());
    m_context->IASetVertexBuffers(0, 1, m_triangleVertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->RSSetState(m_triangleRasterizerState.Get());
    m_context->VSSetShader(m_triangleVertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_trianglePixelShader.Get(), nullptr, 0);
    m_context->Draw(3, 0);
}

} // namespace qc
#endif
