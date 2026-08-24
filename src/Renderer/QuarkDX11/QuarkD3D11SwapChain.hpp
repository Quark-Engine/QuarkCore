#pragma once

#if defined(_WIN32)
#include "QuarkD3D11Common.hpp"
#include "QuarkD3D11Device.hpp"
#include <SDL3/SDL_video.h>
#include <wrl/client.h>

namespace qc {

class D3D11SwapChain {
public:
    void Initialize(const D3D11Device &device, SDL_Window *window, int width, int height);
    void Shutdown();
    void Resize(const D3D11Device &device, int width, int height);
    void Present(bool vsync);

    ID3D11RenderTargetView *RenderTarget() const { return m_renderTarget.Get(); }
    IDXGISwapChain *Get() const { return m_swapChain.Get(); }

private:
    void CreateRenderTarget(const D3D11Device &device);
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTarget;
};

} // namespace qc
#endif