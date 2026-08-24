#pragma once

#if defined(_WIN32)
#include "QuarkD3D11Pipeline.hpp"
#include "QuarkD3D11SwapChain.hpp"

namespace qc {

class D3D11CommandContext {
public:
    void Initialize(const D3D11Device &device, D3D11SwapChain &swapChain, D3D11Pipeline &pipeline,
                    D3D11Resources &resources, int width, int height);
    void Shutdown();
    void RefreshViewport(int width, int height);
    void BeginDrawing();
    void EndDrawing(bool vsync);
    void Clear(Color color);
    void DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color, int width, int height);

private:
    ID3D11DeviceContext *m_context = nullptr;
    D3D11SwapChain *m_swapChain = nullptr;
    D3D11Pipeline *m_pipeline = nullptr;
    D3D11Resources *m_resources = nullptr;
};

} // namespace qc
#endif