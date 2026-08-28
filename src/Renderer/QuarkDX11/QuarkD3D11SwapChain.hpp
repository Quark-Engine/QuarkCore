#ifndef __QUARK_D3D11_SWAP_CHAIN__
#define __QUARK_D3D11_SWAP_CHAIN__

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
    ID3D11DepthStencilView *DepthStencilView() const { return m_depthStencilView.Get(); }
    IDXGISwapChain *Get() const { return m_swapChain.Get(); }

private:
    void CreateRenderTarget(const D3D11Device &device);
    void CreateDepthStencil(const D3D11Device &device);
    void ReleaseDepthStencil();
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTarget;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_depthStencilTexture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_depthStencilView;
};

} // namespace qc
#endif

#endif // __QUARK_D3D11_SWAP_CHAIN__
