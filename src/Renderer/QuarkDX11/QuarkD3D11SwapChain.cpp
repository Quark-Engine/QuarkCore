#include "QuarkD3D11SwapChain.hpp"

#include <cstring>
#include <dxgi1_5.h>

#ifndef DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
#define DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING 2048u
#endif
#ifndef DXGI_PRESENT_ALLOW_TEARING
#define DXGI_PRESENT_ALLOW_TEARING 0x00000200u
#endif
#ifndef DXGI_FEATURE_PRESENT_ALLOW_TEARING
#define DXGI_FEATURE_PRESENT_ALLOW_TEARING ((DXGI_FEATURE)0)
#endif

#if defined(_WIN32)
namespace qci
{
    void D3D11SwapChain::Initialize(const D3D11Device& device, SDL_Window* pWindow, int width,
                                    int height)
    {
        TraceLog(LogLevel::Info, "D3D11",
                 TextFormat("Creating swap chain (%dx%d, Buffers: 3, Format: RGBA8)", width, height));

        if (!pWindow)
        {
            throw std::runtime_error("D3D11 requires a valid SDL window");
        }

        const SDL_PropertiesID properties = SDL_GetWindowProperties(pWindow);
        HWND hwnd = static_cast<HWND>(
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));

        if (!hwnd)
        {
            throw std::runtime_error("SDL did not provide a Win32 window handle");
        }

        m_pDevice = device.Get();
        m_pContext = device.Context();
        m_Width = static_cast<UINT>(width);
        m_Height = static_cast<UINT>(height);
        m_MsaaSamples = 1;

        DXGI_SWAP_CHAIN_DESC description{};
        description.BufferDesc.Width = m_Width;
        description.BufferDesc.Height = m_Height;
        description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.BufferDesc.RefreshRate = {0, 1};
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 3;
        description.OutputWindow = hwnd;
        description.Windowed = TRUE;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        Microsoft::WRL::ComPtr<IDXGIFactory> factory;

        d3d11::ThrowIfFailed(device.Get()->QueryInterface(IID_PPV_ARGS(&dxgiDevice)),
                             "QueryInterface IDXGIDevice");
        d3d11::ThrowIfFailed(dxgiDevice->GetAdapter(&adapter), "IDXGIDevice::GetAdapter");
        d3d11::ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(&factory)), "IDXGIAdapter::GetParent");

        BOOL allowTearing = FALSE;
        Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory.As(&factory5)))
        {
            if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))))
            {
                allowTearing = FALSE;
            }
        }
        m_TearingSupported = (allowTearing == TRUE);
        TraceLog(LogLevel::Info, "D3D11", TextFormat("Variable refresh rate / tearing support: %s", m_TearingSupported ? "Yes" : "No"));

        description.Flags = m_TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        HRESULT swapChainResult = factory->CreateSwapChain(device.Get(), &description, &m_SwapChain);
        if (FAILED(swapChainResult) && m_TearingSupported)
        {
            m_TearingSupported = false;
            description.Flags = 0;
            swapChainResult = factory->CreateSwapChain(device.Get(), &description, &m_SwapChain);
        }
        if (FAILED(swapChainResult))
        {
            description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            description.Flags = 0;
            m_TearingSupported = false;
            swapChainResult = factory->CreateSwapChain(device.Get(), &description, &m_SwapChain);
        }
        if (FAILED(swapChainResult))
        {
            description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            swapChainResult = factory->CreateSwapChain(device.Get(), &description, &m_SwapChain);
        }
        d3d11::ThrowIfFailed(swapChainResult,
                             "IDXGIFactory::CreateSwapChain");

        TraceLog(LogLevel::Trace, "D3D11", "Swap chain created successfully.");
        CreateRenderTarget(device);
        CreateDepthStencil(device);
    }

    void D3D11SwapChain::CreateRenderTarget(const D3D11Device& device)
    {
        d3d11::ThrowIfFailed(m_SwapChain->GetBuffer(0, IID_PPV_ARGS(&m_BackBuffer)),
                             "IDXGISwapChain::GetBuffer");
        d3d11::ThrowIfFailed(
            device.Get()->CreateRenderTargetView(m_BackBuffer.Get(), nullptr, &m_RenderTarget),
            "ID3D11Device::CreateRenderTargetView");

        TraceLog(LogLevel::Info, "D3D11", "Back-buffer render target view created.");
    }

    void D3D11SwapChain::CreateDepthStencil(const D3D11Device& device)
    {
        DXGI_SWAP_CHAIN_DESC chainDescription{};
        m_SwapChain->GetDesc(&chainDescription);

        D3D11_TEXTURE2D_DESC textureDescription{};
        textureDescription.Width = chainDescription.BufferDesc.Width;
        textureDescription.Height = chainDescription.BufferDesc.Height;
        textureDescription.MipLevels = 1;
        textureDescription.ArraySize = 1;
        textureDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Usage = D3D11_USAGE_DEFAULT;
        textureDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        d3d11::ThrowIfFailed(
            device.Get()->CreateTexture2D(&textureDescription, nullptr, &m_DepthStencilTexture),
            "ID3D11Device::CreateTexture2D depth stencil");
        d3d11::ThrowIfFailed(
            device.Get()->CreateDepthStencilView(m_DepthStencilTexture.Get(), nullptr,
                                                 &m_DepthStencilView),
            "ID3D11Device::CreateDepthStencilView");

        TraceLog(LogLevel::Info, "D3D11",
                 TextFormat("Depth-stencil buffer created (%ux%u).",
                            textureDescription.Width, textureDescription.Height));
    }

    void D3D11SwapChain::ReleaseDepthStencil()
    {
        m_DepthStencilView.Reset();
        m_DepthStencilTexture.Reset();
    }

    bool D3D11SwapChain::IsSampleCountSupported(UINT samples) const
    {
        if (!m_pDevice || samples <= 1)
        {
            return true;
        }

        const DXGI_FORMAT aFormats[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT};
        for (const DXGI_FORMAT format : aFormats)
        {
            UINT qualityLevels = 0;
            if (FAILED(m_pDevice->CheckMultisampleQualityLevels(format, samples, &qualityLevels)) ||
                qualityLevels == 0)
            {
                return false;
            }
        }
        return true;
    }

    void D3D11SwapChain::CreateMSAATargets()
    {
        if (!m_pDevice || m_Width == 0 || m_Height == 0)
        {
            return;
        }

        D3D11_TEXTURE2D_DESC textureDescription{};
        textureDescription.Width = m_Width;
        textureDescription.Height = m_Height;
        textureDescription.MipLevels = 1;
        textureDescription.ArraySize = 1;
        textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDescription.SampleDesc.Count = m_MsaaSamples;
        textureDescription.SampleDesc.Quality = 0;
        textureDescription.Usage = D3D11_USAGE_DEFAULT;
        textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET;

        d3d11::ThrowIfFailed(
            m_pDevice->CreateTexture2D(&textureDescription, nullptr, &m_MsaaColorTexture),
            "ID3D11Device::CreateTexture2D MSAA color");
        d3d11::ThrowIfFailed(
            m_pDevice->CreateRenderTargetView(m_MsaaColorTexture.Get(), nullptr,
                                              &m_MsaaColorRenderTarget),
            "ID3D11Device::CreateRenderTargetView MSAA color");

        textureDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        textureDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        d3d11::ThrowIfFailed(
            m_pDevice->CreateTexture2D(&textureDescription, nullptr, &m_MsaaDepthStencilTexture),
            "ID3D11Device::CreateTexture2D MSAA depth stencil");
        d3d11::ThrowIfFailed(
            m_pDevice->CreateDepthStencilView(m_MsaaDepthStencilTexture.Get(), nullptr,
                                              &m_MsaaDepthStencilView),
            "ID3D11Device::CreateDepthStencilView MSAA depth stencil");

        TraceLog(LogLevel::Info, "D3D11",
                 TextFormat("MSAA render targets created (%ux%u, Samples: %u).", m_Width, m_Height,
                            m_MsaaSamples));
    }

    void D3D11SwapChain::DestroyMSAATargets()
    {
        m_MsaaColorRenderTarget.Reset();
        m_MsaaColorTexture.Reset();
        m_MsaaDepthStencilView.Reset();
        m_MsaaDepthStencilTexture.Reset();
    }

    void D3D11SwapChain::SetMSAASamples(UINT samples)
    {
        if (samples != 2 && samples != 4 && samples != 8)
        {
            samples = 1;
        }

        if (m_pDevice != nullptr && !IsSampleCountSupported(samples))
        {
            TraceLog(LogLevel::Warn, "D3D11",
                     TextFormat("Requested MSAA sample count %u is not supported; disabling MSAA.",
                                samples));
            samples = 1;
        }

        if (samples == m_MsaaSamples)
        {
            return;
        }

        DestroyMSAATargets();
        m_MsaaSamples = samples;

        if (m_MsaaSamples > 1)
        {
            CreateMSAATargets();
        }
    }

    void D3D11SwapChain::Resolve()
    {
        if (m_MsaaSamples <= 1 || !m_pContext || !m_MsaaColorTexture || !m_BackBuffer)
        {
            return;
        }

        m_pContext->ResolveSubresource(m_BackBuffer.Get(), 0, m_MsaaColorTexture.Get(), 0,
                                       DXGI_FORMAT_R8G8B8A8_UNORM);
    }

    bool D3D11SwapChain::ReadBackBufferPixels(void* pOutPixels)
    {
        if (!m_pDevice || !m_pContext || !m_BackBuffer || !pOutPixels || m_Width == 0 || m_Height == 0)
        {
            return false;
        }

        Resolve();

        D3D11_TEXTURE2D_DESC description{};
        m_BackBuffer->GetDesc(&description);
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        description.MiscFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        if (FAILED(m_pDevice->CreateTexture2D(&description, nullptr, &staging)))
        {
            return false;
        }

        m_pContext->CopyResource(staging.Get(), m_BackBuffer.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(m_pContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            return false;
        }

        const size_t rowBytes = static_cast<size_t>(m_Width) * 4;
        const uint8_t* pSrc = static_cast<const uint8_t*>(mapped.pData);
        uint8_t* pDst = static_cast<uint8_t*>(pOutPixels);
        for (UINT y = 0; y < m_Height; ++y)
        {
            std::memcpy(pDst + static_cast<size_t>(y) * rowBytes, pSrc + static_cast<size_t>(y) * mapped.RowPitch, rowBytes);
        }

        m_pContext->Unmap(staging.Get(), 0);
        return true;
    }

    void D3D11SwapChain::Resize(const D3D11Device& device, int width, int height)
    {
        TraceLog(LogLevel::Info, "D3D11",
                 TextFormat("Resizing swap chain to %dx%d...", width, height));

        m_RenderTarget.Reset();
        m_BackBuffer.Reset();
        ReleaseDepthStencil();
        DestroyMSAATargets();

        d3d11::ThrowIfFailed(m_SwapChain->ResizeBuffers(0, static_cast<UINT>(width),
                                                        static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN,
                                                        m_TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u),
                             "IDXGISwapChain::ResizeBuffers");
        if (false)
        {
            (void)(0 + "IDXGISwapChain::ResizeBuffers");
        }

        m_Width = static_cast<UINT>(width);
        m_Height = static_cast<UINT>(height);

        CreateRenderTarget(device);
        CreateDepthStencil(device);

        if (m_MsaaSamples > 1)
        {
            CreateMSAATargets();
        }

        TraceLog(LogLevel::Info, "D3D11",
                 TextFormat("Swap chain resized successfully (%dx%d).", width, height));
    }

    void D3D11SwapChain::Present(bool vsync)
    {
        const UINT syncInterval = vsync ? 1u : 0u;
        const UINT presentFlags = (!vsync && m_TearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        const HRESULT result = m_SwapChain->Present(syncInterval, presentFlags);

        if (result != DXGI_STATUS_OCCLUDED)
        {
            d3d11::ThrowIfFailed(result, "IDXGISwapChain::Present");
        }
    }

    void D3D11SwapChain::Shutdown()
    {
        TraceLog(LogLevel::Info, "D3D11", "Releasing swap chain resources...");

        DestroyMSAATargets();
        m_RenderTarget.Reset();
        m_BackBuffer.Reset();
        ReleaseDepthStencil();
        m_SwapChain.Reset();
        m_pDevice = nullptr;
        m_pContext = nullptr;
        m_MsaaSamples = 1;

        TraceLog(LogLevel::Info, "D3D11", "Swap chain shut down successfully.");
    }
} // qci
#endif
