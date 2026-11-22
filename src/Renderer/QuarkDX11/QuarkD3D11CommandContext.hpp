#ifndef __QUARK_D3D11_COMMAND_CONTEXT__
#define __QUARK_D3D11_COMMAND_CONTEXT__

#if defined(_WIN32)
#include "../QuarkRenderThreadPool.hpp"
#include "QuarkD3D11Pipeline.hpp"
#include "QuarkD3D11SwapChain.hpp"
#include <d3d11_1.h>
#include <cstdint>
#include <vector>

namespace qci
{
    class D3D11CommandContext
    {
    public:
        struct ShaderOverride
        {
            ID3D11VertexShader* m_pVertexShader = nullptr;
            ID3D11PixelShader* m_pPixelShader = nullptr;
            ID3D11InputLayout* m_pInputLayout = nullptr;
            ID3D11Buffer* m_pConstantBuffer = nullptr;
            const uint8_t* m_pConstantData = nullptr;
            uint32_t m_ConstantDataSize = 0;
            ID3D11ShaderResourceView* m_apShaderResources[10] = {};
            UINT m_StrideBytes = 0;
            UINT m_PositionOffset = 0;
            UINT m_TexCoordOffset = 0xFFFFFFFFu;
            UINT m_ColorOffset = 0xFFFFFFFFu;

            bool Active() const
            {
                return m_pVertexShader != nullptr && m_pPixelShader != nullptr &&
                       m_pInputLayout != nullptr && m_StrideBytes > 0;
            }
        };

        void Initialize(const D3D11Device& device, D3D11SwapChain& swapChain, D3D11Pipeline& pipeline,
                        D3D11Resources& resources, int width, int height,
                        QuarkRenderThreadPool* pThreadPool = nullptr);
        void Shutdown();
        void RefreshViewport(int width, int height);
        void SetDefaultViewportSize(int width, int height);
        void BeginDrawing();
        void Flush3DBatch();
        void FlushBatch();
        void FlushDebugOverlay();
        void SetDrawingDebugOverlay(bool enabled)
        {
            m_DrawingDebugOverlay = enabled;
        }
        bool IsDrawingDebugOverlay() const
        {
            return m_DrawingDebugOverlay;
        }
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
        float* Get3DVertexPtr(size_t offsetFloats)
        {
            return m_vQueued3DVertices.data() + offsetFloats;
        }
        void SubmitQueued3D(size_t vertexOffsetFloats, UINT vertexCount, D3D_PRIMITIVE_TOPOLOGY topology);
        void SubmitQueued3DTextured(size_t vertexOffsetFloats, UINT vertexCount,
                                    ID3D11ShaderResourceView* pShaderResource,
                                    ID3D11ShaderResourceView* pNormalResource);
        void SubmitQueued3DShader(size_t vertexOffsetFloats, UINT vertexCount,
                                  const ShaderOverride& shaderOverride);
        struct InstanceTransform
        {
            float m_aModel[16];
            float m_aMvp[16];
            float m_aNormalMatrix[16];
        };

        UINT Append3DInstances(const InstanceTransform* pInstances, UINT count);
        void SubmitQueued3DStaticMesh(ID3D11Buffer* pStaticVertexBuffer,
                                      ID3D11Buffer* pStaticIndexBuffer,
                                      UINT vertexCount, UINT indexCount, bool indexed,
                                      const ShaderOverride& shaderOverride,
                                      UINT instanceCount = 1,
                                      const uint8_t* pBatchedConstantData = nullptr,
                                      uint32_t singleConstantBytes = 0,
                                      bool useGpuInstancing = false);
        void Draw3D(const float* pVertices, UINT vertexCount, D3D_PRIMITIVE_TOPOLOGY topology);
        void Draw3DTextured(const float* pVertices, UINT vertexCount,
                            ID3D11ShaderResourceView* pShaderResource,
                            ID3D11ShaderResourceView* pNormalResource);
        void Draw3DShader(const float* pVertices, UINT vertexCount,
                          const ShaderOverride& shaderOverride);
        void ClearDepthStencil();
        void ReleaseRenderTargets();
        void BeginTextureMode(const IRenderTexture& target);
        void EndTextureMode(int width, int height);
        int ActiveWidth() const
        {
            return m_ActiveWidth;
        }
        int ActiveHeight() const
        {
            return m_ActiveHeight;
        }
        void BeginMode2D(const Camera2D& camera);
        void EndMode2D();
        Camera2D GetCamera2D() const
        {
            return m_Camera2D;
        }
        void SetShaderOverride(const ShaderOverride& shaderOverride)
        {
            m_ShaderOverride = shaderOverride;
        }
        void SetBlendState(ID3D11BlendState* pBlendState)
        {
            m_pActiveBlendState = pBlendState;
        }
        const ShaderOverride& GetShaderOverride() const
        {
            return m_ShaderOverride;
        }

    private:
        static constexpr UINT BATCH_MAX_VERTICES = 8192 * 4;
        static constexpr size_t MAX_3D_BUFFER_BYTES = 8 * 1024 * 1024;
        static constexpr size_t MAX_WORKER_CONSTANT_BUFFER_BYTES = 65536;
        static constexpr size_t RING_CONSTANT_BUFFER_BYTES = 4 * 1024 * 1024;
        static constexpr size_t CONSTANT_BUFFER_ALIGNMENT = 256;

        struct BatchVertex
        {
            float m_X, m_Y;
            float m_U, m_V;
            float m_R, m_G, m_B, m_A;
        };

        struct DrawItem
        {
            UINT m_IndexStart = 0;
            UINT m_IndexCount = 0;
            ID3D11ShaderResourceView* m_pShaderResource = nullptr;
        };

        enum class Queued3DMode : uint8_t
        {
            Untextured = 0,
            Textured = 1,
            CustomShader = 2,
        };

        struct Queued3DDraw
        {
            Queued3DMode m_Mode = Queued3DMode::Untextured;
            size_t m_VertexOffsetFloats = 0;
            UINT m_VertexCount = 0;
            D3D_PRIMITIVE_TOPOLOGY m_Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            ID3D11ShaderResourceView* m_pShaderResource = nullptr;
            ID3D11ShaderResourceView* m_pNormalResource = nullptr;
            ShaderOverride m_ShaderOverride{};
            size_t m_ConstantOffsetBytes = 0;
            uint32_t m_ConstantSizeBytes = 0;
            ID3D11Buffer* m_pStaticVertexBuffer = nullptr;
            ID3D11Buffer* m_pStaticIndexBuffer = nullptr;
            UINT m_IndexCount = 0;
            UINT m_InstanceCount = 1;
            bool m_Indexed = false;
            bool m_UseGpuInstancing = false;
            bool m_BackfaceCulling = false;
        };

        struct DeferredWorkerContext
        {
            Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_DeferredContext;
            Microsoft::WRL::ComPtr<ID3D11DeviceContext1> m_DeferredContext1;
            Microsoft::WRL::ComPtr<ID3D11Buffer> m_VertexBuffer3D;
            Microsoft::WRL::ComPtr<ID3D11Buffer> m_ConstantBuffer;
            Microsoft::WRL::ComPtr<ID3D11CommandList> m_CommandList;
        };

        struct DrawStateCache
        {
            ID3D11InputLayout* m_pInputLayout = nullptr;
            ID3D11Buffer* m_pVertexBuffer = nullptr;
            UINT m_VertexStride = 0;
            ID3D11Buffer* m_pIndexBuffer = nullptr;
            D3D_PRIMITIVE_TOPOLOGY m_Topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
            ID3D11RasterizerState* m_pRasterizer = nullptr;
            ID3D11VertexShader* m_pVertexShader = nullptr;
            ID3D11PixelShader* m_pPixelShader = nullptr;
            ID3D11Buffer* m_pVsConstantBuffer0 = nullptr;
            ID3D11Buffer* m_pPsConstantBuffer0 = nullptr;
            ID3D11Buffer* m_pPsLightConstantBuffer = nullptr;
            ID3D11SamplerState* m_pSampler0 = nullptr;
            ID3D11DepthStencilState* m_pDepthStencil = nullptr;
            ID3D11BlendState* m_pBlendState = nullptr;
            ID3D11ShaderResourceView* m_apSrvs[10] = {};
            bool m_SrvsValid = false;
        };

        void Flush2DBatch();
        void ExecuteQueued3DDrawOnContext(ID3D11DeviceContext* pCtx, ID3D11DeviceContext1* pCtx1,
                                          bool useRingConstantBuffer,
                                          ID3D11Buffer* pVertexBuffer3D,
                                          ID3D11Buffer* pWorkerConstantBuffer, size_t& vbByteOffset,
                                          const Queued3DDraw& draw, DrawStateCache& stateCache);
        void BindOverride(ID3D11ShaderResourceView* pDrawnResource);
        void BatchTriangle(const BatchVertex aVertices[3]);
        void BatchQuad(const BatchVertex aVertices[4], ID3D11ShaderResourceView* pShaderResource);

        ID3D11DeviceContext* m_pContext = nullptr;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext1> m_Context1;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_RingConstantBuffer;
        D3D11SwapChain* m_pSwapChain = nullptr;
        D3D11Pipeline* m_pPipeline = nullptr;
        D3D11Resources* m_pResources = nullptr;
        QuarkRenderThreadPool* m_pThreadPool = nullptr;
        ID3D11RenderTargetView* m_pActiveRenderTarget = nullptr;
        ID3D11BlendState* m_pActiveBlendState = nullptr;
        ID3D11DepthStencilView* m_pActiveDepthStencil = nullptr;
        int m_DefaultWidth = 0;
        int m_DefaultHeight = 0;
        int m_ActiveWidth = 0;
        int m_ActiveHeight = 0;
        Camera2D m_Camera2D{};
        bool m_Camera2DActive = false;
        ShaderOverride m_ShaderOverride{};

        Microsoft::WRL::ComPtr<ID3D11Buffer> m_BatchVertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_BatchIndexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_WhiteTexture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_WhiteShaderResource;
        std::vector<BatchVertex> m_vBatchVertices;
        std::vector<uint32_t> m_vBatchIndices;
        std::vector<DrawItem> m_vBatchDrawItems;
        bool m_DrawingDebugOverlay = false;
        std::vector<BatchVertex> m_vOverlayVertices;
        std::vector<uint32_t> m_vOverlayIndices;
        std::vector<DrawItem> m_vOverlayDrawItems;

        static constexpr UINT MAX_3D_INSTANCES = 4096;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_InstanceBuffer;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_InstanceSRV;
        std::vector<InstanceTransform> m_vQueued3DInstances;
        std::vector<DeferredWorkerContext> m_vWorkers;
        std::vector<float> m_vQueued3DVertices;
        std::vector<uint8_t> m_vQueued3DConstants;
        std::vector<Queued3DDraw> m_vQueued3DDraws;
    };
} // qci
#endif
#endif // __QUARK_D3D11_COMMAND_CONTEXT__
