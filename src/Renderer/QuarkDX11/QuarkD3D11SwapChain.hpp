#ifndef __QUARK_D3D11_SWAP_CHAIN__
#define __QUARK_D3D11_SWAP_CHAIN__

#if defined(_WIN32)
#include "QuarkD3D11Common.hpp"
#include "QuarkD3D11Device.hpp"
#include <SDL3/SDL_video.h>
#include <wrl/client.h>

namespace qci
{
    class D3D11SwapChain
    {
    public:
        void Initialize(const D3D11Device& device, SDL_Window* pWindow, int width, int height);
        void Shutdown();
        void Resize(const D3D11Device& device, int width, int height);
        void Present(bool vsync);

        void SetMSAASamples(UINT samples);
        UINT MSAASamples() const
        {
            return m_MsaaSamples;
        }
        void Resolve();
        bool ReadBackBufferPixels(void* pOutPixels);

        ID3D11RenderTargetView* RenderTarget() const
        {
            return (m_MsaaSamples > 1 && m_MsaaColorRenderTarget) ? m_MsaaColorRenderTarget.Get()
                                                                  : m_RenderTarget.Get();
        }
        ID3D11DepthStencilView* DepthStencilView() const
        {
            return (m_MsaaSamples > 1 && m_MsaaDepthStencilView) ? m_MsaaDepthStencilView.Get()
                                                                 : m_DepthStencilView.Get();
        }
        IDXGISwapChain* Get() const
        {
            return m_SwapChain.Get();
        }

    private:
        void CreateRenderTarget(const D3D11Device& device);
        void CreateDepthStencil(const D3D11Device& device);
        void ReleaseDepthStencil();
        bool IsSampleCountSupported(UINT samples) const;
        void CreateMSAATargets();
        void DestroyMSAATargets();
        Microsoft::WRL::ComPtr<IDXGISwapChain> m_SwapChain;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_BackBuffer;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RenderTarget;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DepthStencilTexture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_DepthStencilView;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_MsaaColorTexture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_MsaaColorRenderTarget;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_MsaaDepthStencilTexture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_MsaaDepthStencilView;
        ID3D11Device* m_pDevice = nullptr;
        ID3D11DeviceContext* m_pContext = nullptr;
        UINT m_MsaaSamples = 1;
        UINT m_Width = 0;
        UINT m_Height = 0;
        bool m_TearingSupported = false;
    };
} // qci
#endif
#endif // __QUARK_D3D11_SWAP_CHAIN__
