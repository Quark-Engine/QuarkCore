#ifndef __QUARK_D3D11_COMMAND_CONTEXT__
#define __QUARK_D3D11_COMMAND_CONTEXT__

#if defined(_WIN32)
#include "QuarkD3D11Pipeline.hpp"
#include "QuarkD3D11SwapChain.hpp"
#include <cstdint>
#include <vector>

namespace qc {

class D3D11CommandContext {
public:
    struct ShaderOverride {
        ID3D11VertexShader *vertexShader = nullptr;
        ID3D11PixelShader *pixelShader = nullptr;
        ID3D11InputLayout *inputLayout = nullptr;
        ID3D11Buffer *constantBuffer = nullptr;
        ID3D11ShaderResourceView *shaderResources[8] = {};
        UINT strideBytes = 0;
        UINT positionOffset = 0;
        UINT texCoordOffset = 0xFFFFFFFFu;
        UINT colorOffset = 0xFFFFFFFFu;

        bool Active() const { return vertexShader != nullptr && pixelShader != nullptr &&
                                     inputLayout != nullptr && strideBytes > 0; }
    };

    void Initialize(const D3D11Device &device, D3D11SwapChain &swapChain, D3D11Pipeline &pipeline,
                    D3D11Resources &resources, int width, int height);
    void Shutdown();
    void RefreshViewport(int width, int height);
    void SetDefaultViewportSize(int width, int height);
    void BeginDrawing();
    void FlushBatch();
    void Present(bool vsync);
    void Clear(Color color);
    void DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color, int width, int height);
    void DrawTextureQuad(const ITexture& texture,
                         Rectangle source,
                         Rectangle destination,
                         Vec2 origin,
                         float rotation,
                         Color tint,
                         int width,
                         int height);
    void Draw3D(const float *vertices, UINT vertexCount, D3D_PRIMITIVE_TOPOLOGY topology);
    void Draw3DTextured(const float *vertices, UINT vertexCount,
                        ID3D11ShaderResourceView *shaderResource);
    void ClearDepthStencil();
    void BeginTextureMode(const IRenderTexture& target);
    void EndTextureMode(int width, int height);
    void BeginMode2D(const Camera2D& camera);
    void EndMode2D();
    Camera2D GetCamera2D() const { return m_camera2D; }
    void SetShaderOverride(const ShaderOverride &shaderOverride) { m_shaderOverride = shaderOverride; }
    const ShaderOverride &GetShaderOverride() const { return m_shaderOverride; }

private:
    static constexpr UINT kBatchMaxVertices = 8192 * 4;

    struct BatchVertex {
        float x, y;
        float u, v;
        float r, g, b, a;
    };

    struct DrawItem {
        UINT indexStart = 0;
        UINT indexCount = 0;
        ID3D11ShaderResourceView *shaderResource = nullptr;
    };

    void BindOverride(ID3D11ShaderResourceView *drawnResource);
    void BatchTriangle(const BatchVertex v[3]);
    void BatchQuad(const BatchVertex v[4], ID3D11ShaderResourceView *shaderResource);

    ID3D11DeviceContext *m_context = nullptr;
    D3D11SwapChain *m_swapChain = nullptr;
    D3D11Pipeline *m_pipeline = nullptr;
    D3D11Resources *m_resources = nullptr;
    ID3D11RenderTargetView *m_activeRenderTarget = nullptr;
    ID3D11DepthStencilView *m_activeDepthStencil = nullptr;
    int m_defaultWidth = 0;
    int m_defaultHeight = 0;
    int m_activeWidth = 0;
    int m_activeHeight = 0;
    Camera2D m_camera2D{};
    bool m_camera2DActive = false;
    ShaderOverride m_shaderOverride{};

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_batchVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_batchIndexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_whiteTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_whiteShaderResource;
    std::vector<BatchVertex> m_batchVertices;
    std::vector<uint32_t> m_batchIndices;
    std::vector<DrawItem> m_batchDrawItems;
};

} // namespace qc
#endif

#endif // __QUARK_D3D11_COMMAND_CONTEXT__
