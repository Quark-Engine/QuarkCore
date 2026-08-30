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

    void SetMSAASamples(UINT samples);
    UINT MSAASamples() const { return m_msaaSamples; }
    void Resolve();
    bool ReadBackBufferPixels(void *outPixels);

    ID3D11RenderTargetView *RenderTarget() const {
        return (m_msaaSamples > 1 && m_msaaColorRenderTarget) ? m_msaaColorRenderTarget.Get()
                                                              : m_renderTarget.Get();
    }
    ID3D11DepthStencilView *DepthStencilView() const {
        return (m_msaaSamples > 1 && m_msaaDepthStencilView) ? m_msaaDepthStencilView.Get()
                                                              : m_depthStencilView.Get();
    }
    IDXGISwapChain *Get() const { return m_swapChain.Get(); }

private:
    void CreateRenderTarget(const D3D11Device &device);
    void CreateDepthStencil(const D3D11Device &device);
    void ReleaseDepthStencil();
    bool IsSampleCountSupported(UINT samples) const;
    void CreateMSAATargets();
    void DestroyMSAATargets();
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_backBuffer;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTarget;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_depthStencilTexture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_depthStencilView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_msaaColorTexture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_msaaColorRenderTarget;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_msaaDepthStencilTexture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_msaaDepthStencilView;
    ID3D11Device *m_device = nullptr;
    ID3D11DeviceContext *m_context = nullptr;
    UINT m_msaaSamples = 1;
    UINT m_width = 0;
    UINT m_height = 0;
};

} // namespace qc
#endif

#endif // __QUARK_D3D11_SWAP_CHAIN__