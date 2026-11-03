#ifndef __QUARK_D3D11_COMMAND_CONTEXT__
#define __QUARK_D3D11_COMMAND_CONTEXT__

#if defined(_WIN32)
#include "../QuarkRenderThreadPool.hpp"
#include "QuarkD3D11Pipeline.hpp"
#include "QuarkD3D11SwapChain.hpp"
#include <cstdint>
#include <vector>
namespace qci {
class D3D11CommandContext {
public:
    struct ShaderOverride {
        ID3D11VertexShader *vertexShader = nullptr;
        ID3D11PixelShader *pixelShader = nullptr;
        ID3D11InputLayout *inputLayout = nullptr;
        ID3D11Buffer *constantBuffer = nullptr;
        const uint8_t *constantData = nullptr;
        uint32_t constantDataSize = 0;
        ID3D11ShaderResourceView *shaderResources[10] = {};
        UINT strideBytes = 0;
        UINT positionOffset = 0;
        UINT texCoordOffset = 0xFFFFFFFFu;
        UINT colorOffset = 0xFFFFFFFFu;

        bool Active() const { return vertexShader != nullptr && pixelShader != nullptr &&
                                     inputLayout != nullptr && strideBytes > 0; }
    };

    void Initialize(const D3D11Device &device, D3D11SwapChain &swapChain, D3D11Pipeline &pipeline,
                    D3D11Resources &resources, int width, int height,
                    QuarkRenderThreadPool *threadPool = nullptr);
    void Shutdown();
    void RefreshViewport(int width, int height);
    void SetDefaultViewportSize(int width, int height);
    void BeginDrawing();
    void Flush3DBatch();
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
    size_t Allocate3DVertexSlice(UINT vertexCount, UINT strideFloats);
    float *Get3DVertexPtr(size_t offsetFloats) { return m_queued3DVertices.data() + offsetFloats; }
    void SubmitQueued3D(size_t vertexOffsetFloats, UINT vertexCount, D3D_PRIMITIVE_TOPOLOGY topology);
    void SubmitQueued3DTextured(size_t vertexOffsetFloats, UINT vertexCount,
                                ID3D11ShaderResourceView *shaderResource,
                                ID3D11ShaderResourceView *normalResource);
    void SubmitQueued3DShader(size_t vertexOffsetFloats, UINT vertexCount,
                              const ShaderOverride &shaderOverride);
    void Draw3D(const float *vertices, UINT vertexCount, D3D_PRIMITIVE_TOPOLOGY topology);
    void Draw3DTextured(const float *vertices, UINT vertexCount,
                        ID3D11ShaderResourceView *shaderResource,
                        ID3D11ShaderResourceView *normalResource);
    void Draw3DShader(const float *vertices, UINT vertexCount,
                      const ShaderOverride &shaderOverride);
    void ClearDepthStencil();
    void ReleaseRenderTargets();
    void BeginTextureMode(const IRenderTexture& target);
    void EndTextureMode(int width, int height);
    int ActiveWidth() const { return m_activeWidth; }
    int ActiveHeight() const { return m_activeHeight; }
    void BeginMode2D(const Camera2D& camera);
    void EndMode2D();
    Camera2D GetCamera2D() const { return m_camera2D; }
    void SetShaderOverride(const ShaderOverride &shaderOverride) { m_shaderOverride = shaderOverride; }
    const ShaderOverride &GetShaderOverride() const { return m_shaderOverride; }

private:
    static constexpr UINT kBatchMaxVertices = 8192 * 4;
    static constexpr size_t kMax3DBufferBytes = 8 * 1024 * 1024;
    static constexpr size_t kMaxWorkerConstantBufferBytes = 65536;

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

    enum class Queued3DMode : uint8_t {
        Untextured = 0,
        Textured = 1,
        CustomShader = 2,
    };

    struct Queued3DDraw {
        Queued3DMode mode = Queued3DMode::Untextured;
        size_t vertexOffsetFloats = 0;
        UINT vertexCount = 0;
        D3D_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        ID3D11ShaderResourceView *shaderResource = nullptr;
        ID3D11ShaderResourceView *normalResource = nullptr;
        ShaderOverride shaderOverride{};
        size_t constantOffsetBytes = 0;
        uint32_t constantSizeBytes = 0;
        bool backfaceCulling = false;
    };

    struct DeferredWorkerContext {
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferredContext;
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer3D;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer;
        Microsoft::WRL::ComPtr<ID3D11CommandList> commandList;
    };

    void Flush2DBatch();
    void ExecuteQueued3DDrawOnContext(ID3D11DeviceContext *ctx, ID3D11Buffer *vertexBuffer3D,
                                      ID3D11Buffer *workerConstantBuffer, size_t &vbByteOffset,
                                      const Queued3DDraw &draw);
    void BindOverride(ID3D11ShaderResourceView *drawnResource);
    void BatchTriangle(const BatchVertex v[3]);
    void BatchQuad(const BatchVertex v[4], ID3D11ShaderResourceView *shaderResource);

    ID3D11DeviceContext *m_context = nullptr;
    D3D11SwapChain *m_swapChain = nullptr;
    D3D11Pipeline *m_pipeline = nullptr;
    D3D11Resources *m_resources = nullptr;
    QuarkRenderThreadPool *m_threadPool = nullptr;
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

    std::vector<DeferredWorkerContext> m_workers;
    std::vector<float> m_queued3DVertices;
    std::vector<uint8_t> m_queued3DConstants;
    std::vector<Queued3DDraw> m_queued3DDraws;
};
} // namespace qci
#endif
#endif // __QUARK_D3D11_COMMAND_CONTEXT__
