#include "QuarkD3D11SwapChain.hpp"

#if defined(_WIN32)
namespace qc {

void D3D11SwapChain::Initialize(const D3D11Device &device, SDL_Window *window, int width,
                                int height)
{
    TraceLog(LogLevel::Info, "D3D11",
             TextFormat("Creating swap chain (%dx%d, Buffers: 2, Format: RGBA8)", width, height));

    if (!window)
    {
        throw std::runtime_error("D3D11 requires a valid SDL window");
    }

    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    HWND hwnd = static_cast<HWND>(
        SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));

    if (!hwnd)
    {
        throw std::runtime_error("SDL did not provide a Win32 window handle");
    }

    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferDesc.Width = static_cast<UINT>(width);
    description.BufferDesc.Height = static_cast<UINT>(height);
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferDesc.RefreshRate = {60, 1};
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.OutputWindow = hwnd;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<IDXGIFactory> factory;

    d3d11::ThrowIfFailed(device.Get()->QueryInterface(IID_PPV_ARGS(&dxgiDevice)),
                         "QueryInterface IDXGIDevice");
    d3d11::ThrowIfFailed(dxgiDevice->GetAdapter(&adapter), "IDXGIDevice::GetAdapter");
    d3d11::ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(&factory)), "IDXGIAdapter::GetParent");
    d3d11::ThrowIfFailed(factory->CreateSwapChain(device.Get(), &description, &m_swapChain),
                         "IDXGIFactory::CreateSwapChain");

    TraceLog(LogLevel::Trace, "D3D11", "Swap chain created successfully.");
    CreateRenderTarget(device);
}

void D3D11SwapChain::CreateRenderTarget(const D3D11Device &device)
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;

    d3d11::ThrowIfFailed(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)),
                         "IDXGISwapChain::GetBuffer");
    d3d11::ThrowIfFailed(
        device.Get()->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_renderTarget),
        "ID3D11Device::CreateRenderTargetView");

    TraceLog(LogLevel::Info, "D3D11", "Back-buffer render target view created.");
}

void D3D11SwapChain::Resize(const D3D11Device &device, int width, int height)
{
    TraceLog(LogLevel::Info, "D3D11",
             TextFormat("Resizing swap chain to %dx%d...", width, height));

    m_renderTarget.Reset();

    d3d11::ThrowIfFailed(m_swapChain->ResizeBuffers(0, static_cast<UINT>(width),
                                                    static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN,
                                                    0),
                         "IDXGISwapChain::ResizeBuffers");

    CreateRenderTarget(device);

    TraceLog(LogLevel::Info, "D3D11",
             TextFormat("Swap chain resized successfully (%dx%d).", width, height));
}

void D3D11SwapChain::Present(bool vsync)
{
    const HRESULT result = m_swapChain->Present(vsync ? 1u : 0u, 0);

    if (result != DXGI_STATUS_OCCLUDED)
    {
        d3d11::ThrowIfFailed(result, "IDXGISwapChain::Present");
    }
}

void D3D11SwapChain::Shutdown()
{
    TraceLog(LogLevel::Info, "D3D11", "Releasing swap chain resources...");

    m_renderTarget.Reset();
    m_swapChain.Reset();

    TraceLog(LogLevel::Info, "D3D11", "Swap chain shut down successfully.");
}

} // namespace qc
#endif
