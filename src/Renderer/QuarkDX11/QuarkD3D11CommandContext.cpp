#include "QuarkD3D11CommandContext.hpp"
#include "../QuarkIRenderer.hpp"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <vector>

#if defined(_WIN32)
namespace qci
{
    void D3D11CommandContext::Initialize(const D3D11Device& device, D3D11SwapChain& swapChain,
                                         D3D11Pipeline& pipeline, D3D11Resources& resources,
                                         int width, int height, QuarkRenderThreadPool* pThreadPool)
    {
        m_pContext = device.Context();
        m_Context1.Reset();
        if (m_pContext)
        {
            m_pContext->QueryInterface(IID_PPV_ARGS(&m_Context1));
        }
        m_pSwapChain = &swapChain;
        m_pPipeline = &pipeline;
        m_pResources = &resources;
        m_pThreadPool = pThreadPool;
        m_pActiveRenderTarget = swapChain.RenderTarget();
        m_DefaultWidth = width;
        m_DefaultHeight = height;
        m_ActiveWidth = width;
        m_ActiveHeight = height;

        ID3D11Device* pNativeDevice = device.Get();
        {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = BATCH_MAX_VERTICES * sizeof(BatchVertex);
            description.Usage = D3D11_USAGE_DYNAMIC;
            description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            d3d11::ThrowIfFailed(pNativeDevice->CreateBuffer(&description, nullptr, &m_BatchVertexBuffer),
                                 "ID3D11Device::CreateBuffer batch vertices");
        }
        {
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = BATCH_MAX_VERTICES * 3 / 2 * sizeof(uint32_t);
            description.Usage = D3D11_USAGE_DYNAMIC;
            description.BindFlags = D3D11_BIND_INDEX_BUFFER;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            d3d11::ThrowIfFailed(pNativeDevice->CreateBuffer(&description, nullptr, &m_BatchIndexBuffer),
                                 "ID3D11Device::CreateBuffer batch indices");
        }
        {
            static constexpr uint8_t s_aWhitePixel[4] = {255, 255, 255, 255};
            D3D11_TEXTURE2D_DESC description{};
            description.Width = 1;
            description.Height = 1;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_IMMUTABLE;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initialData{};
            initialData.pSysMem = s_aWhitePixel;
            initialData.SysMemPitch = sizeof(s_aWhitePixel);
            Microsoft::WRL::ComPtr<ID3D11Texture2D> whiteTexture;
            d3d11::ThrowIfFailed(pNativeDevice->CreateTexture2D(&description, &initialData, &whiteTexture),
                                 "ID3D11Device::CreateTexture2D batch white");
            m_WhiteTexture = whiteTexture;
            d3d11::ThrowIfFailed(
                pNativeDevice->CreateShaderResourceView(m_WhiteTexture.Get(), nullptr,
                                                        &m_WhiteShaderResource),
                "ID3D11Device::CreateShaderResourceView batch white");
        }

        m_vWorkers.clear();
        const uint32_t workerCount = (m_pThreadPool && m_pThreadPool->WorkerCount() > 1)
                                         ? m_pThreadPool->WorkerCount()
                                         : 0u;
        if (workerCount > 1 && pNativeDevice)
        {
            m_vWorkers.resize(workerCount);
            bool allSucceeded = true;
            for (uint32_t i = 0; i < workerCount; ++i)
            {
                DeferredWorkerContext& worker = m_vWorkers[i];
                if (FAILED(pNativeDevice->CreateDeferredContext(0, &worker.m_DeferredContext)) ||
                    !worker.m_DeferredContext)
                {
                    allSucceeded = false;
                    break;
                }
                worker.m_DeferredContext.As(&worker.m_DeferredContext1);

                D3D11_BUFFER_DESC vbDesc{};
                vbDesc.ByteWidth = static_cast<UINT>(MAX_3D_BUFFER_BYTES);
                vbDesc.Usage = D3D11_USAGE_DYNAMIC;
                vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                if (FAILED(pNativeDevice->CreateBuffer(&vbDesc, nullptr, &worker.m_VertexBuffer3D)))
                {
                    allSucceeded = false;
                    break;
                }

                D3D11_BUFFER_DESC cbDesc{};
                cbDesc.ByteWidth = static_cast<UINT>(MAX_WORKER_CONSTANT_BUFFER_BYTES);
                cbDesc.Usage = D3D11_USAGE_DYNAMIC;
                cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                if (FAILED(pNativeDevice->CreateBuffer(&cbDesc, nullptr, &worker.m_ConstantBuffer)))
                {
                    allSucceeded = false;
                    break;
                }
            }
            if (!allSucceeded)
            {
                m_vWorkers.clear();
            }
        }

        {
            D3D11_BUFFER_DESC instDesc{};
            instDesc.ByteWidth = MAX_3D_INSTANCES * static_cast<UINT>(sizeof(InstanceTransform));
            instDesc.Usage = D3D11_USAGE_DYNAMIC;
            instDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            instDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            instDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            instDesc.StructureByteStride = static_cast<UINT>(sizeof(InstanceTransform));
            if (SUCCEEDED(pNativeDevice->CreateBuffer(&instDesc, nullptr, &m_InstanceBuffer)))
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = DXGI_FORMAT_UNKNOWN;
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
                srvDesc.Buffer.FirstElement = 0;
                srvDesc.Buffer.NumElements = MAX_3D_INSTANCES;
                pNativeDevice->CreateShaderResourceView(m_InstanceBuffer.Get(), &srvDesc, &m_InstanceSRV);
            }
        }

        m_RingConstantBuffer.Reset();
        if (m_Context1 && pNativeDevice)
        {
            D3D11_BUFFER_DESC ringDesc{};
            ringDesc.ByteWidth = static_cast<UINT>(RING_CONSTANT_BUFFER_BYTES);
            ringDesc.Usage = D3D11_USAGE_DYNAMIC;
            ringDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            ringDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(pNativeDevice->CreateBuffer(&ringDesc, nullptr, &m_RingConstantBuffer)))
            {
                m_RingConstantBuffer.Reset();
            }
        }

        m_vBatchVertices.reserve(BATCH_MAX_VERTICES);
        m_vBatchIndices.reserve(BATCH_MAX_VERTICES);
        m_vBatchDrawItems.reserve(BATCH_MAX_VERTICES);
        m_vQueued3DVertices.reserve(65536 * 22);
        m_vQueued3DConstants.reserve(65536);
        m_vQueued3DDraws.reserve(1024);
        m_vQueued3DInstances.reserve(MAX_3D_INSTANCES);

        TraceLog(LogLevel::Trace, "D3D11",
                 TextFormat("Command context initialized (%zu deferred workers).", m_vWorkers.size()));
    }

    void D3D11CommandContext::RefreshViewport(int width, int height)
    {
        if (!m_pContext)
        {
            return;
        }

        D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                                0.0f, 1.0f};

        m_pContext->RSSetViewports(1, &viewport);
    }

    void D3D11CommandContext::SetDefaultViewportSize(int width, int height)
    {
        m_DefaultWidth = width;
        m_DefaultHeight = height;
    }

    void D3D11CommandContext::BeginDrawing()
    {
        if (!m_pContext || !m_pSwapChain)
        {
            return;
        }

        ID3D11RenderTargetView* pRenderTarget = m_pSwapChain->RenderTarget();
        m_pActiveRenderTarget = pRenderTarget;
        m_pActiveDepthStencil = m_pSwapChain->DepthStencilView();
        m_ActiveWidth = m_DefaultWidth > 0 ? m_DefaultWidth : m_ActiveWidth;
        m_ActiveHeight = m_DefaultHeight > 0 ? m_DefaultHeight : m_ActiveHeight;
        m_pContext->OMSetRenderTargets(1, &pRenderTarget, m_pActiveDepthStencil);
        RefreshViewport(m_ActiveWidth, m_ActiveHeight);
    }

    void D3D11CommandContext::Present(bool vsync)
    {
        FlushBatch();
        FlushDebugOverlay();
        if (m_pSwapChain)
        {
            m_pSwapChain->Resolve();
            m_pSwapChain->Present(vsync);
        }
    }

    void D3D11CommandContext::Clear(Color color)
    {
        if (!m_pContext || !m_pSwapChain)
        {
            return;
        }

        FlushBatch();

        const float aClearColor[] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                                     color.a / 255.0f};

        m_pContext->ClearRenderTargetView(m_pActiveRenderTarget, aClearColor);
        ClearDepthStencil();
    }

    void D3D11CommandContext::ClearDepthStencil()
    {
        if (!m_pContext || !m_pSwapChain)
        {
            return;
        }

        Flush3DBatch();

        ID3D11DepthStencilView* pDepthStencil = m_pActiveDepthStencil;
        if (pDepthStencil)
        {
            m_pContext->ClearDepthStencilView(pDepthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                              1.0f, 0);
        }
    }

    void D3D11CommandContext::ReleaseRenderTargets()
    {
        if (!m_pContext)
        {
            return;
        }

        FlushBatch();

        ID3D11RenderTargetView* pNullRenderTarget = nullptr;
        ID3D11DepthStencilView* pNullDepthStencil = nullptr;
        m_pContext->OMSetRenderTargets(1, &pNullRenderTarget, pNullDepthStencil);
        m_pActiveRenderTarget = nullptr;
        m_pActiveDepthStencil = nullptr;
    }

    size_t D3D11CommandContext::Allocate3DVertexSlice(UINT vertexCount, UINT strideFloats)
    {
        const size_t offset = m_vQueued3DVertices.size();
        m_vQueued3DVertices.resize(offset + static_cast<size_t>(vertexCount) * strideFloats);
        return offset;
    }

    void D3D11CommandContext::SubmitQueued3D(size_t vertexOffsetFloats, UINT vertexCount,
                                             D3D_PRIMITIVE_TOPOLOGY topology)
    {
        if (!m_pContext || !m_pPipeline || !m_pResources || !m_pSwapChain || vertexCount == 0 ||
            !m_pActiveDepthStencil)
        {
            return;
        }

        if (!m_vBatchDrawItems.empty())
        {
            Flush2DBatch();
        }

        const bool culling = m_pPipeline->BackfaceCulling();
        if (!m_vQueued3DDraws.empty())
        {
            Queued3DDraw& prev = m_vQueued3DDraws.back();
            if (prev.m_Mode == Queued3DMode::Untextured &&
                prev.m_Topology == topology &&
                prev.m_BackfaceCulling == culling &&
                (prev.m_VertexOffsetFloats + static_cast<size_t>(prev.m_VertexCount) * 16 == vertexOffsetFloats))
            {
                prev.m_VertexCount += vertexCount;
                return;
            }
        }

        Queued3DDraw draw{};
        draw.m_Mode = Queued3DMode::Untextured;
        draw.m_VertexOffsetFloats = vertexOffsetFloats;
        draw.m_VertexCount = vertexCount;
        draw.m_Topology = topology;
        draw.m_BackfaceCulling = culling;
        m_vQueued3DDraws.push_back(draw);
    }

    void D3D11CommandContext::SubmitQueued3DTextured(size_t vertexOffsetFloats, UINT vertexCount,
                                                     ID3D11ShaderResourceView* pShaderResource,
                                                     ID3D11ShaderResourceView* pNormalResource)
    {
        if (!m_pContext || !m_pPipeline || !m_pResources || vertexCount == 0)
        {
            return;
        }

        if (!m_vBatchDrawItems.empty())
        {
            Flush2DBatch();
        }

        const bool culling = m_pPipeline->BackfaceCulling();
        if (!m_vQueued3DDraws.empty())
        {
            Queued3DDraw& prev = m_vQueued3DDraws.back();
            if (prev.m_Mode == Queued3DMode::Textured &&
                prev.m_pShaderResource == pShaderResource &&
                prev.m_pNormalResource == pNormalResource &&
                prev.m_BackfaceCulling == culling &&
                (prev.m_VertexOffsetFloats + static_cast<size_t>(prev.m_VertexCount) * 22 == vertexOffsetFloats))
            {
                prev.m_VertexCount += vertexCount;
                return;
            }
        }

        Queued3DDraw draw{};
        draw.m_Mode = Queued3DMode::Textured;
        draw.m_VertexOffsetFloats = vertexOffsetFloats;
        draw.m_VertexCount = vertexCount;
        draw.m_Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        draw.m_pShaderResource = pShaderResource;
        draw.m_pNormalResource = pNormalResource;
        draw.m_BackfaceCulling = culling;
        m_vQueued3DDraws.push_back(draw);
    }

    void D3D11CommandContext::SubmitQueued3DShader(size_t vertexOffsetFloats, UINT vertexCount,
                                                   const ShaderOverride& shaderOverride)
    {
        if (!m_pContext || !m_pPipeline || !m_pResources || !m_pSwapChain || !shaderOverride.Active() ||
            vertexCount == 0 || !m_pActiveDepthStencil)
        {
            return;
        }

        if (!m_vBatchDrawItems.empty())
        {
            Flush2DBatch();
        }

        Queued3DDraw draw{};
        draw.m_Mode = Queued3DMode::CustomShader;
        draw.m_VertexOffsetFloats = vertexOffsetFloats;
        draw.m_VertexCount = vertexCount;
        draw.m_Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        draw.m_ShaderOverride = shaderOverride;
        draw.m_ShaderOverride.m_pConstantData = nullptr;
        draw.m_ShaderOverride.m_ConstantDataSize = 0;
        draw.m_BackfaceCulling = m_pPipeline->BackfaceCulling();

        if (shaderOverride.m_pConstantData && shaderOverride.m_ConstantDataSize > 0)
        {
            const uint32_t copyBytes = std::min<uint32_t>(
                shaderOverride.m_ConstantDataSize, static_cast<uint32_t>(MAX_WORKER_CONSTANT_BUFFER_BYTES));
            const size_t alignedOffset =
                (m_vQueued3DConstants.size() + CONSTANT_BUFFER_ALIGNMENT - 1u) &
                ~(CONSTANT_BUFFER_ALIGNMENT - 1u);
            const size_t alignedSize =
                (static_cast<size_t>(copyBytes) + CONSTANT_BUFFER_ALIGNMENT - 1u) &
                ~(CONSTANT_BUFFER_ALIGNMENT - 1u);
            m_vQueued3DConstants.resize(alignedOffset + alignedSize, 0);
            std::memcpy(m_vQueued3DConstants.data() + alignedOffset, shaderOverride.m_pConstantData,
                        copyBytes);
            draw.m_ConstantOffsetBytes = alignedOffset;
            draw.m_ConstantSizeBytes = copyBytes;
        }

        m_vQueued3DDraws.push_back(draw);
    }

    UINT D3D11CommandContext::Append3DInstances(const InstanceTransform* pInstances, UINT count)
    {
        if (!pInstances || count == 0 || !m_InstanceBuffer || !m_InstanceSRV ||
            m_vQueued3DInstances.size() + static_cast<size_t>(count) > MAX_3D_INSTANCES)
        {
            return 0xFFFFFFFFu;
        }
        const UINT startIndex = static_cast<UINT>(m_vQueued3DInstances.size());
        m_vQueued3DInstances.insert(m_vQueued3DInstances.end(), pInstances, pInstances + count);
        return startIndex;
    }

    void D3D11CommandContext::SubmitQueued3DStaticMesh(ID3D11Buffer* pStaticVertexBuffer,
                                                       ID3D11Buffer* pStaticIndexBuffer,
                                                       UINT vertexCount, UINT indexCount, bool indexed,
                                                       const ShaderOverride& shaderOverride,
                                                       UINT instanceCount,
                                                       const uint8_t* pBatchedConstantData,
                                                       uint32_t singleConstantBytes,
                                                       bool useGpuInstancing)
    {
        if (!m_pContext || !m_pPipeline || !m_pResources || !m_pSwapChain || !shaderOverride.Active() ||
            !pStaticVertexBuffer || vertexCount == 0 || instanceCount == 0 || !m_pActiveDepthStencil)
        {
            return;
        }

        if (!m_vBatchDrawItems.empty())
        {
            Flush2DBatch();
        }

        Queued3DDraw draw{};
        draw.m_Mode = Queued3DMode::CustomShader;
        draw.m_VertexCount = vertexCount;
        draw.m_IndexCount = indexCount;
        draw.m_Indexed = indexed && pStaticIndexBuffer != nullptr && indexCount > 0;
        draw.m_pStaticVertexBuffer = pStaticVertexBuffer;
        draw.m_pStaticIndexBuffer = pStaticIndexBuffer;
        draw.m_InstanceCount = instanceCount;
        draw.m_UseGpuInstancing = useGpuInstancing;
        draw.m_Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        draw.m_ShaderOverride = shaderOverride;
        draw.m_ShaderOverride.m_pConstantData = nullptr;
        draw.m_ShaderOverride.m_ConstantDataSize = 0;
        draw.m_BackfaceCulling = m_pPipeline->BackfaceCulling();

        if (!useGpuInstancing && pBatchedConstantData && singleConstantBytes > 0)
        {
            const uint32_t clampedSingle = std::min<uint32_t>(
                singleConstantBytes, static_cast<uint32_t>(MAX_WORKER_CONSTANT_BUFFER_BYTES));
            const size_t alignedOffset =
                (m_vQueued3DConstants.size() + CONSTANT_BUFFER_ALIGNMENT - 1u) &
                ~(CONSTANT_BUFFER_ALIGNMENT - 1u);
            const size_t alignedSingle =
                (static_cast<size_t>(clampedSingle) + CONSTANT_BUFFER_ALIGNMENT - 1u) &
                ~(CONSTANT_BUFFER_ALIGNMENT - 1u);
            m_vQueued3DConstants.resize(alignedOffset + alignedSingle * static_cast<size_t>(instanceCount), 0);
            for (UINT inst = 0; inst < instanceCount; ++inst)
            {
                std::memcpy(m_vQueued3DConstants.data() + alignedOffset + static_cast<size_t>(inst) * alignedSingle,
                            pBatchedConstantData + static_cast<size_t>(inst) * static_cast<size_t>(singleConstantBytes),
                            clampedSingle);
            }
            draw.m_ConstantOffsetBytes = alignedOffset;
            draw.m_ConstantSizeBytes = clampedSingle;
        }
        else if (shaderOverride.m_pConstantData && shaderOverride.m_ConstantDataSize > 0)
        {
            const uint32_t copyBytes = std::min<uint32_t>(
                shaderOverride.m_ConstantDataSize, static_cast<uint32_t>(MAX_WORKER_CONSTANT_BUFFER_BYTES));
            const size_t alignedOffset =
                (m_vQueued3DConstants.size() + CONSTANT_BUFFER_ALIGNMENT - 1u) &
                ~(CONSTANT_BUFFER_ALIGNMENT - 1u);
            const size_t alignedSize =
                (static_cast<size_t>(copyBytes) + CONSTANT_BUFFER_ALIGNMENT - 1u) &
                ~(CONSTANT_BUFFER_ALIGNMENT - 1u);
            m_vQueued3DConstants.resize(alignedOffset + alignedSize, 0);
            std::memcpy(m_vQueued3DConstants.data() + alignedOffset, shaderOverride.m_pConstantData,
                        copyBytes);
            draw.m_ConstantOffsetBytes = alignedOffset;
            draw.m_ConstantSizeBytes = copyBytes;
        }

        m_vQueued3DDraws.push_back(draw);
    }

    void D3D11CommandContext::Draw3D(const float* pVertices, UINT vertexCount,
                                     D3D_PRIMITIVE_TOPOLOGY topology)
    {
        if (!m_pContext || !m_pPipeline || !m_pResources || !m_pSwapChain ||
            pVertices == nullptr || vertexCount == 0 || !m_pActiveDepthStencil)
        {
            return;
        }

        constexpr UINT STRIDE_FLOATS = 16;
        const size_t offsetFloats = Allocate3DVertexSlice(vertexCount, STRIDE_FLOATS);
        std::memcpy(Get3DVertexPtr(offsetFloats), pVertices,
                    static_cast<size_t>(vertexCount) * STRIDE_FLOATS * sizeof(float));
        SubmitQueued3D(offsetFloats, vertexCount, topology);
    }

    void D3D11CommandContext::Draw3DTextured(const float* pVertices, UINT vertexCount,
                                             ID3D11ShaderResourceView* pShaderResource,
                                             ID3D11ShaderResourceView* pNormalResource)
    {
        if (!m_pContext || !m_pPipeline || !m_pResources || !pVertices || vertexCount == 0)
        {
            return;
        }

        constexpr UINT STRIDE_FLOATS = 22;
        const size_t offsetFloats = Allocate3DVertexSlice(vertexCount, STRIDE_FLOATS);
        std::memcpy(Get3DVertexPtr(offsetFloats), pVertices,
                    static_cast<size_t>(vertexCount) * STRIDE_FLOATS * sizeof(float));
        SubmitQueued3DTextured(offsetFloats, vertexCount, pShaderResource, pNormalResource);
    }

    void D3D11CommandContext::Draw3DShader(const float* pVertices, UINT vertexCount,
                                           const ShaderOverride& shaderOverride)
    {
        if (!m_pContext || !m_pPipeline || !m_pResources || !m_pSwapChain || !shaderOverride.Active() ||
            pVertices == nullptr || vertexCount == 0 || !m_pActiveDepthStencil)
        {
            return;
        }

        const UINT strideFloats = shaderOverride.m_StrideBytes / sizeof(float);
        if (strideFloats == 0)
        {
            return;
        }
        const size_t offsetFloats = Allocate3DVertexSlice(vertexCount, strideFloats);
        std::memcpy(Get3DVertexPtr(offsetFloats), pVertices,
                    static_cast<size_t>(vertexCount) * shaderOverride.m_StrideBytes);
        SubmitQueued3DShader(offsetFloats, vertexCount, shaderOverride);
    }

    void D3D11CommandContext::ExecuteQueued3DDrawOnContext(ID3D11DeviceContext* pCtx,
                                                           ID3D11DeviceContext1* pCtx1,
                                                           bool useRingConstantBuffer,
                                                           ID3D11Buffer* pVertexBuffer3D,
                                                           ID3D11Buffer* pWorkerConstantBuffer,
                                                           size_t& vbByteOffset,
                                                           const Queued3DDraw& draw,
                                                           DrawStateCache& stateCache)
    {
        if (!pCtx || !pVertexBuffer3D || draw.m_VertexCount == 0)
        {
            return;
        }

        UINT strideBytes = 0;
        UINT strideFloats = 0;
        const float aBlendFactor[] = {0.0f, 0.0f, 0.0f, 0.0f};
        ID3D11RasterizerState* pRasterizer =
            draw.m_BackfaceCulling ? m_pPipeline->RasterizerCull() : m_pPipeline->Rasterizer();

        auto bindCommonState = [&](ID3D11InputLayout* pLayout, ID3D11Buffer* pVb, UINT stride,
                                   D3D_PRIMITIVE_TOPOLOGY topology, ID3D11VertexShader* pVs,
                                   ID3D11PixelShader* pPs, ID3D11Buffer* pLightCB)
        {
            if (stateCache.m_pInputLayout != pLayout)
            {
                pCtx->IASetInputLayout(pLayout);
                stateCache.m_pInputLayout = pLayout;
            }
            if (stateCache.m_pVertexBuffer != pVb || stateCache.m_VertexStride != stride)
            {
                const UINT offset = 0;
                pCtx->IASetVertexBuffers(0, 1, &pVb, &stride, &offset);
                stateCache.m_pVertexBuffer = pVb;
                stateCache.m_VertexStride = stride;
            }
            if (stateCache.m_Topology != topology)
            {
                pCtx->IASetPrimitiveTopology(topology);
                stateCache.m_Topology = topology;
            }
            if (stateCache.m_pRasterizer != pRasterizer)
            {
                pCtx->RSSetState(pRasterizer);
                stateCache.m_pRasterizer = pRasterizer;
            }
            if (stateCache.m_pVertexShader != pVs)
            {
                pCtx->VSSetShader(pVs, nullptr, 0);
                stateCache.m_pVertexShader = pVs;
            }
            if (stateCache.m_pPixelShader != pPs)
            {
                pCtx->PSSetShader(pPs, nullptr, 0);
                stateCache.m_pPixelShader = pPs;
            }
            if (stateCache.m_pPsLightConstantBuffer != pLightCB)
            {
                pCtx->PSSetConstantBuffers(1, 1, &pLightCB);
                stateCache.m_pPsLightConstantBuffer = pLightCB;
            }
            ID3D11DepthStencilState* pDsState = m_pPipeline->DepthStencilState();
            if (stateCache.m_pDepthStencil != pDsState)
            {
                pCtx->OMSetDepthStencilState(pDsState, 0);
                stateCache.m_pDepthStencil = pDsState;
            }
            ID3D11BlendState* pBlend = m_pPipeline->Blend();
            if (stateCache.m_pBlendState != pBlend)
            {
                pCtx->OMSetBlendState(pBlend, aBlendFactor, 0xFFFFFFFF);
                stateCache.m_pBlendState = pBlend;
            }
        };

        auto bindSampler0 = [&]()
        {
            ID3D11SamplerState* pSampler = m_pPipeline->Sampler();
            if (stateCache.m_pSampler0 != pSampler)
            {
                pCtx->PSSetSamplers(0, 1, &pSampler);
                stateCache.m_pSampler0 = pSampler;
            }
        };

        auto bindSRVs = [&](ID3D11ShaderResourceView* const* ppResources, size_t count)
        {
            bool changed = !stateCache.m_SrvsValid;
            if (!changed)
            {
                for (size_t i = 0; i < count; ++i)
                {
                    if (stateCache.m_apSrvs[i] != ppResources[i])
                    {
                        changed = true;
                        break;
                    }
                }
            }
            if (changed)
            {
                pCtx->PSSetShaderResources(0, static_cast<UINT>(count), ppResources);
                for (size_t i = 0; i < count; ++i)
                {
                    stateCache.m_apSrvs[i] = ppResources[i];
                }
                stateCache.m_SrvsValid = true;
            }
        };

        if (draw.m_Mode == Queued3DMode::Untextured)
        {
            strideFloats = 16;
            strideBytes = sizeof(float) * strideFloats;
            bindCommonState(m_pPipeline->InputLayout3D(), pVertexBuffer3D, strideBytes, draw.m_Topology,
                            m_pPipeline->VertexShader3D(), m_pPipeline->PixelShader3D(),
                            *m_pPipeline->LightConstantBuffer());
        }
        else if (draw.m_Mode == Queued3DMode::Textured)
        {
            strideFloats = 22;
            strideBytes = sizeof(float) * strideFloats;
            bindCommonState(m_pPipeline->TexturedInputLayout3D(), pVertexBuffer3D, strideBytes,
                            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
                            m_pPipeline->TexturedVertexShader3D(), m_pPipeline->TexturedPixelShader3D(),
                            *m_pPipeline->LightConstantBuffer());
            ID3D11ShaderResourceView* apResources[2] = {draw.m_pShaderResource, draw.m_pNormalResource};
            bindSRVs(apResources, 2);
            bindSampler0();
        }
        else
        {
            strideBytes = draw.m_ShaderOverride.m_StrideBytes;
            strideFloats = strideBytes / sizeof(float);
            if (strideBytes == 0 || strideFloats == 0)
            {
                return;
            }
            ID3D11Buffer* pBoundVB = draw.m_pStaticVertexBuffer ? draw.m_pStaticVertexBuffer : pVertexBuffer3D;
            bindCommonState(draw.m_ShaderOverride.m_pInputLayout, pBoundVB, strideBytes,
                            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
                            draw.m_ShaderOverride.m_pVertexShader, draw.m_ShaderOverride.m_pPixelShader,
                            *m_pPipeline->LightConstantBuffer());

            ID3D11Buffer* pTargetConstantBuffer =
                pWorkerConstantBuffer ? pWorkerConstantBuffer : draw.m_ShaderOverride.m_pConstantBuffer;
            if (draw.m_pStaticVertexBuffer != nullptr)
            {
                bindSRVs(draw.m_ShaderOverride.m_apShaderResources, 10);
                bindSampler0();

                if (draw.m_Indexed && draw.m_pStaticIndexBuffer && stateCache.m_pIndexBuffer != draw.m_pStaticIndexBuffer)
                {
                    pCtx->IASetIndexBuffer(draw.m_pStaticIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
                    stateCache.m_pIndexBuffer = draw.m_pStaticIndexBuffer;
                }

                if (draw.m_ConstantSizeBytes > 0 && (useRingConstantBuffer || pTargetConstantBuffer))
                {
                    const size_t alignedSingle =
                        (static_cast<size_t>(draw.m_ConstantSizeBytes) + CONSTANT_BUFFER_ALIGNMENT - 1u) &
                        ~(CONSTANT_BUFFER_ALIGNMENT - 1u);
                    const bool canBindRing = useRingConstantBuffer && pCtx1 && m_RingConstantBuffer;
                    if (!canBindRing)
                    {
                        if (stateCache.m_pVsConstantBuffer0 != pTargetConstantBuffer)
                        {
                            pCtx->VSSetConstantBuffers(0, 1, &pTargetConstantBuffer);
                            stateCache.m_pVsConstantBuffer0 = pTargetConstantBuffer;
                        }
                        if (stateCache.m_pPsConstantBuffer0 != pTargetConstantBuffer)
                        {
                            pCtx->PSSetConstantBuffers(0, 1, &pTargetConstantBuffer);
                            stateCache.m_pPsConstantBuffer0 = pTargetConstantBuffer;
                        }
                    }
                    if (draw.m_UseGpuInstancing)
                    {
                        if (canBindRing)
                        {
                            ID3D11Buffer* pRingCB = m_RingConstantBuffer.Get();
                            const UINT firstConst = static_cast<UINT>(draw.m_ConstantOffsetBytes / 16u);
                            const UINT numConsts = static_cast<UINT>(alignedSingle / 16u);
                            pCtx1->VSSetConstantBuffers1(0, 1, &pRingCB, &firstConst, &numConsts);
                            pCtx1->PSSetConstantBuffers1(0, 1, &pRingCB, &firstConst, &numConsts);
                            stateCache.m_pVsConstantBuffer0 = nullptr;
                            stateCache.m_pPsConstantBuffer0 = nullptr;
                        }
                        else
                        {
                            D3D11_MAPPED_SUBRESOURCE mappedCB{};
                            if (SUCCEEDED(pCtx->Map(pTargetConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCB)))
                            {
                                std::memcpy(mappedCB.pData,
                                            m_vQueued3DConstants.data() + draw.m_ConstantOffsetBytes,
                                            draw.m_ConstantSizeBytes);
                                pCtx->Unmap(pTargetConstantBuffer, 0);
                            }
                        }
                        if (draw.m_Indexed && draw.m_pStaticIndexBuffer)
                        {
                            pCtx->DrawIndexedInstanced(draw.m_IndexCount, draw.m_InstanceCount, 0, 0, 0);
                            RecordDebugRenderDraw(static_cast<uint64_t>(draw.m_IndexCount) * draw.m_InstanceCount);
                        }
                        else
                        {
                            pCtx->DrawInstanced(draw.m_VertexCount, draw.m_InstanceCount, 0, 0);
                            RecordDebugRenderDraw(static_cast<uint64_t>(draw.m_VertexCount) * draw.m_InstanceCount);
                        }
                    }
                    else
                    {
                        ID3D11Buffer* pRingCB = canBindRing ? m_RingConstantBuffer.Get() : nullptr;
                        const UINT numConsts = static_cast<UINT>(alignedSingle / 16u);
                        if (canBindRing)
                        {
                            stateCache.m_pVsConstantBuffer0 = nullptr;
                            stateCache.m_pPsConstantBuffer0 = nullptr;
                        }
                        for (UINT inst = 0; inst < draw.m_InstanceCount; ++inst)
                        {
                            const size_t instOffset = draw.m_ConstantOffsetBytes +
                                                      static_cast<size_t>(inst) * alignedSingle;
                            if (canBindRing)
                            {
                                const UINT firstConst = static_cast<UINT>(instOffset / 16u);
                                pCtx1->VSSetConstantBuffers1(0, 1, &pRingCB, &firstConst, &numConsts);
                                pCtx1->PSSetConstantBuffers1(0, 1, &pRingCB, &firstConst, &numConsts);
                            }
                            else
                            {
                                D3D11_MAPPED_SUBRESOURCE mappedCB{};
                                if (SUCCEEDED(pCtx->Map(pTargetConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCB)))
                                {
                                    std::memcpy(mappedCB.pData,
                                                m_vQueued3DConstants.data() + instOffset,
                                                draw.m_ConstantSizeBytes);
                                    pCtx->Unmap(pTargetConstantBuffer, 0);
                                }
                            }
                            if (draw.m_Indexed && draw.m_pStaticIndexBuffer)
                            {
                                pCtx->DrawIndexed(draw.m_IndexCount, 0, 0);
                                RecordDebugRenderDraw(draw.m_IndexCount);
                            }
                            else
                            {
                                pCtx->Draw(draw.m_VertexCount, 0);
                                RecordDebugRenderDraw(draw.m_VertexCount);
                            }
                        }
                    }
                }
                else
                {
                    if (draw.m_ShaderOverride.m_pConstantBuffer)
                    {
                        ID3D11Buffer* pConstantBuffer = draw.m_ShaderOverride.m_pConstantBuffer;
                        if (stateCache.m_pVsConstantBuffer0 != pConstantBuffer)
                        {
                            pCtx->VSSetConstantBuffers(0, 1, &pConstantBuffer);
                            stateCache.m_pVsConstantBuffer0 = pConstantBuffer;
                        }
                        if (stateCache.m_pPsConstantBuffer0 != pConstantBuffer)
                        {
                            pCtx->PSSetConstantBuffers(0, 1, &pConstantBuffer);
                            stateCache.m_pPsConstantBuffer0 = pConstantBuffer;
                        }
                    }
                    if (draw.m_Indexed && draw.m_pStaticIndexBuffer)
                    {
                        pCtx->DrawIndexed(draw.m_IndexCount, 0, 0);
                        RecordDebugRenderDraw(draw.m_IndexCount);
                    }
                    else
                    {
                        pCtx->Draw(draw.m_VertexCount, 0);
                        RecordDebugRenderDraw(draw.m_VertexCount);
                    }
                }
                return;
            }

            if (draw.m_ConstantSizeBytes > 0 && (useRingConstantBuffer || pTargetConstantBuffer))
            {
                if (useRingConstantBuffer && pCtx1 && m_RingConstantBuffer)
                {
                    ID3D11Buffer* pRingCB = m_RingConstantBuffer.Get();
                    const size_t alignedSize =
                        (static_cast<size_t>(draw.m_ConstantSizeBytes) + CONSTANT_BUFFER_ALIGNMENT - 1u) &
                        ~(CONSTANT_BUFFER_ALIGNMENT - 1u);
                    const UINT firstConst = static_cast<UINT>(draw.m_ConstantOffsetBytes / 16u);
                    const UINT numConsts = static_cast<UINT>(alignedSize / 16u);
                    pCtx1->VSSetConstantBuffers1(0, 1, &pRingCB, &firstConst, &numConsts);
                    pCtx1->PSSetConstantBuffers1(0, 1, &pRingCB, &firstConst, &numConsts);
                    stateCache.m_pVsConstantBuffer0 = nullptr;
                    stateCache.m_pPsConstantBuffer0 = nullptr;
                }
                else if (pTargetConstantBuffer)
                {
                    D3D11_MAPPED_SUBRESOURCE mappedCB{};
                    if (SUCCEEDED(pCtx->Map(pTargetConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCB)))
                    {
                        std::memcpy(mappedCB.pData,
                                    m_vQueued3DConstants.data() + draw.m_ConstantOffsetBytes,
                                    draw.m_ConstantSizeBytes);
                        pCtx->Unmap(pTargetConstantBuffer, 0);
                    }
                    if (stateCache.m_pVsConstantBuffer0 != pTargetConstantBuffer)
                    {
                        pCtx->VSSetConstantBuffers(0, 1, &pTargetConstantBuffer);
                        stateCache.m_pVsConstantBuffer0 = pTargetConstantBuffer;
                    }
                    if (stateCache.m_pPsConstantBuffer0 != pTargetConstantBuffer)
                    {
                        pCtx->PSSetConstantBuffers(0, 1, &pTargetConstantBuffer);
                        stateCache.m_pPsConstantBuffer0 = pTargetConstantBuffer;
                    }
                }
            }
            else if (draw.m_ShaderOverride.m_pConstantBuffer)
            {
                ID3D11Buffer* pConstantBuffer = draw.m_ShaderOverride.m_pConstantBuffer;
                if (stateCache.m_pVsConstantBuffer0 != pConstantBuffer)
                {
                    pCtx->VSSetConstantBuffers(0, 1, &pConstantBuffer);
                    stateCache.m_pVsConstantBuffer0 = pConstantBuffer;
                }
                if (stateCache.m_pPsConstantBuffer0 != pConstantBuffer)
                {
                    pCtx->PSSetConstantBuffers(0, 1, &pConstantBuffer);
                    stateCache.m_pPsConstantBuffer0 = pConstantBuffer;
                }
            }
            bindSRVs(draw.m_ShaderOverride.m_apShaderResources, 10);
            bindSampler0();
        }

        const UINT maxChunkVertices = static_cast<UINT>(MAX_3D_BUFFER_BYTES / strideBytes);
        const float* pCursor = m_vQueued3DVertices.data() + draw.m_VertexOffsetFloats;
        UINT remaining = draw.m_VertexCount;
        while (remaining > 0)
        {
            const UINT chunk = std::min(remaining, maxChunkVertices);
            const size_t chunkBytes = static_cast<size_t>(chunk) * strideBytes;

            size_t alignedOffset = (vbByteOffset == 0)
                                       ? 0
                                       : ((vbByteOffset + strideBytes - 1) / strideBytes) * strideBytes;
            D3D11_MAP mapType = D3D11_MAP_WRITE_DISCARD;
            if (vbByteOffset > 0 && alignedOffset + chunkBytes <= MAX_3D_BUFFER_BYTES)
            {
                mapType = D3D11_MAP_WRITE_NO_OVERWRITE;
            }
            else
            {
                alignedOffset = 0;
                mapType = D3D11_MAP_WRITE_DISCARD;
            }

            D3D11_MAPPED_SUBRESOURCE mappedVB{};
            if (SUCCEEDED(pCtx->Map(pVertexBuffer3D, 0, mapType, 0, &mappedVB)))
            {
                std::memcpy(static_cast<uint8_t*>(mappedVB.pData) + alignedOffset, pCursor, chunkBytes);
                pCtx->Unmap(pVertexBuffer3D, 0);
                const UINT startVertex = static_cast<UINT>(alignedOffset / strideBytes);
                pCtx->Draw(chunk, startVertex);
                RecordDebugRenderDraw(chunk);
                vbByteOffset = alignedOffset + chunkBytes;
            }

            remaining -= chunk;
            pCursor += static_cast<size_t>(chunk) * strideFloats;
        }
    }

    void D3D11CommandContext::Flush3DBatch()
    {
        if (m_vQueued3DDraws.empty())
        {
            return;
        }

        if (!m_pContext || !m_pPipeline)
        {
            m_vQueued3DDraws.clear();
            m_vQueued3DVertices.clear();
            m_vQueued3DConstants.clear();
            return;
        }

        if (!m_vQueued3DInstances.empty() && m_InstanceBuffer && m_InstanceSRV)
        {
            D3D11_MAPPED_SUBRESOURCE mappedInst{};
            if (SUCCEEDED(m_pContext->Map(m_InstanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedInst)))
            {
                const size_t count = std::min<size_t>(m_vQueued3DInstances.size(), MAX_3D_INSTANCES);
                std::memcpy(mappedInst.pData, m_vQueued3DInstances.data(), count * sizeof(InstanceTransform));
                m_pContext->Unmap(m_InstanceBuffer.Get(), 0);
            }
            ID3D11ShaderResourceView* pInstSrv = m_InstanceSRV.Get();
            m_pContext->VSSetShaderResources(10, 1, &pInstSrv);
        }

        bool useRingConstantBuffer = false;
        if (!m_vQueued3DConstants.empty() && m_Context1 && m_RingConstantBuffer &&
            m_vQueued3DConstants.size() <= RING_CONSTANT_BUFFER_BYTES)
        {
            D3D11_MAPPED_SUBRESOURCE mappedRing{};
            if (SUCCEEDED(m_pContext->Map(m_RingConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                                          &mappedRing)))
            {
                std::memcpy(mappedRing.pData, m_vQueued3DConstants.data(), m_vQueued3DConstants.size());
                m_pContext->Unmap(m_RingConstantBuffer.Get(), 0);
                useRingConstantBuffer = true;
            }
        }

        const size_t drawCount = m_vQueued3DDraws.size();
        const bool useDeferredWorkers =
            drawCount >= 256 && m_pThreadPool && m_pThreadPool->IsMultithreaded() && !m_vWorkers.empty();

        if (!useDeferredWorkers)
        {
            size_t vbByteOffset = 0;
            DrawStateCache stateCache{};
            ID3D11Buffer* pWorkerCB = !m_vWorkers.empty() ? m_vWorkers[0].m_ConstantBuffer.Get() : nullptr;
            for (const Queued3DDraw& draw : m_vQueued3DDraws)
            {
                ExecuteQueued3DDrawOnContext(m_pContext, m_Context1.Get(), useRingConstantBuffer,
                                             m_pPipeline->VertexBuffer3D(), pWorkerCB,
                                             vbByteOffset, draw, stateCache);
            }
        }
        else
        {
            const uint32_t chunkCount =
                static_cast<uint32_t>(std::min(m_vWorkers.size(), drawCount));
            const D3D11_VIEWPORT viewport{0.0f,
                                          0.0f,
                                          static_cast<float>(m_ActiveWidth),
                                          static_cast<float>(m_ActiveHeight),
                                          0.0f,
                                          1.0f};
            ID3D11RenderTargetView* pActiveRTV = m_pActiveRenderTarget;
            ID3D11DepthStencilView* pActiveDSV = m_pActiveDepthStencil;

            m_pThreadPool->ParallelFor(
                chunkCount, 1, [&](uint32_t, size_t chunkBegin, size_t chunkEnd)
                {
                    for (size_t c = chunkBegin; c < chunkEnd; ++c)
                    {
                        DeferredWorkerContext& worker = m_vWorkers[c];
                        ID3D11DeviceContext* pCtx = worker.m_DeferredContext.Get();
                        if (!pCtx)
                        {
                            continue;
                        }

                        const size_t firstDraw = (c * drawCount) / chunkCount;
                        const size_t lastDraw = ((c + 1) * drawCount) / chunkCount;
                        if (firstDraw >= lastDraw)
                        {
                            continue;
                        }

                        pCtx->OMSetRenderTargets(1, &pActiveRTV, pActiveDSV);
                        pCtx->RSSetViewports(1, &viewport);
                        if (!m_vQueued3DInstances.empty() && m_InstanceSRV)
                        {
                            ID3D11ShaderResourceView* pInstSrv = m_InstanceSRV.Get();
                            pCtx->VSSetShaderResources(10, 1, &pInstSrv);
                        }

                        size_t vbByteOffset = 0;
                        DrawStateCache stateCache{};
                        for (size_t d = firstDraw; d < lastDraw; ++d)
                        {
                            ExecuteQueued3DDrawOnContext(pCtx, worker.m_DeferredContext1.Get(),
                                                         useRingConstantBuffer,
                                                         worker.m_VertexBuffer3D.Get(),
                                                         worker.m_ConstantBuffer.Get(), vbByteOffset,
                                                         m_vQueued3DDraws[d], stateCache);
                        }

                        pCtx->FinishCommandList(FALSE, &worker.m_CommandList);
                    }
                });

            for (uint32_t c = 0; c < chunkCount; ++c)
            {
                if (m_vWorkers[c].m_CommandList)
                {
                    m_pContext->ExecuteCommandList(m_vWorkers[c].m_CommandList.Get(), FALSE);
                    m_vWorkers[c].m_CommandList.Reset();
                }
            }

            if (m_pActiveRenderTarget || m_pActiveDepthStencil)
            {
                m_pContext->OMSetRenderTargets(1, &m_pActiveRenderTarget, m_pActiveDepthStencil);
            }
            RefreshViewport(m_ActiveWidth, m_ActiveHeight);
        }

        ID3D11ShaderResourceView* apNullResources[10] = {};
        m_pContext->PSSetShaderResources(0, 10, apNullResources);

        m_vQueued3DDraws.clear();
        m_vQueued3DVertices.clear();
        m_vQueued3DConstants.clear();
        m_vQueued3DInstances.clear();
    }

    void D3D11CommandContext::DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color, int, int)
    {
        if (!m_pContext || !m_pPipeline || !m_pResources || m_ActiveWidth <= 0 || m_ActiveHeight <= 0)
        {
            return;
        }

        if (!m_vQueued3DDraws.empty())
        {
            Flush3DBatch();
        }

        const Vec2 aPoints[3] =
            {
                m_Camera2DActive ? GetWorldToScreen2D(v1, m_Camera2D) : v1,
                m_Camera2DActive ? GetWorldToScreen2D(v2, m_Camera2D) : v2,
                m_Camera2DActive ? GetWorldToScreen2D(v3, m_Camera2D) : v3};

        if (m_ShaderOverride.Active())
        {
            Flush2DBatch();

            const float aColorValues[] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                                          color.a / 255.0f};
            const UINT floatsPerVertex = m_ShaderOverride.m_StrideBytes / sizeof(float);
            std::vector<float> vVertices(static_cast<size_t>(floatsPerVertex) * 3, 0.0f);

            for (int index = 0; index < 3; ++index)
            {
                float* pVertex = vVertices.data() + static_cast<size_t>(index) * floatsPerVertex;
                pVertex[m_ShaderOverride.m_PositionOffset / sizeof(float)] =
                    aPoints[index].x / m_ActiveWidth * 2.0f - 1.0f;
                pVertex[m_ShaderOverride.m_PositionOffset / sizeof(float) + 1] =
                    1.0f - aPoints[index].y / m_ActiveHeight * 2.0f;
                if (m_ShaderOverride.m_ColorOffset != 0xFFFFFFFFu)
                {
                    std::memcpy(pVertex + m_ShaderOverride.m_ColorOffset / sizeof(float), aColorValues,
                                sizeof(aColorValues));
                }
                if (m_ShaderOverride.m_TexCoordOffset != 0xFFFFFFFFu)
                {
                    pVertex[m_ShaderOverride.m_TexCoordOffset / sizeof(float)] =
                        aPoints[index].x / m_ActiveWidth;
                    pVertex[m_ShaderOverride.m_TexCoordOffset / sizeof(float)] =
                        aPoints[index].x / m_ActiveWidth;
                    pVertex[m_ShaderOverride.m_TexCoordOffset / sizeof(float) + 1] =
                        aPoints[index].y / m_ActiveHeight;
                }
            }

            m_pResources->UpdateDynamicBuffer(m_pContext, m_pPipeline->VertexBuffer(), vVertices.data(),
                                              static_cast<UINT>(vVertices.size() * sizeof(float)));
            BindOverride(nullptr);
            m_pContext->Draw(3, 0);
            RecordDebugRenderDraw(3);
            return;
        }

        if (m_vBatchVertices.size() + 3 > BATCH_MAX_VERTICES)
        {
            Flush2DBatch();
        }

        const float aColorValues[] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                                      color.a / 255.0f};
        BatchVertex aBatch[3]{};
        for (int index = 0; index < 3; ++index)
        {
            aBatch[index].m_X = aPoints[index].x;
            aBatch[index].m_Y = aPoints[index].y;
            std::memcpy(&aBatch[index].m_R, aColorValues, sizeof(aColorValues));
        }
        BatchTriangle(aBatch);
    }

    void D3D11CommandContext::DrawTextureQuad(const ITexture& texture,
                                              Rectangle source,
                                              Rectangle destination,
                                              Vec2 origin,
                                              float rotation,
                                              Color tint,
                                              int,
                                              int)
    {
        if (!m_pContext || !m_pPipeline || !m_pResources || !texture.IsValid() ||
            texture.m_Width <= 0 || texture.m_Height <= 0 || m_ActiveWidth <= 0 || m_ActiveHeight <= 0)
        {
            return;
        }

        ID3D11ShaderResourceView* pShaderResource = m_pResources->ShaderResource(texture.m_Id);
        if (!pShaderResource)
        {
            return;
        }

        if (!m_vQueued3DDraws.empty())
        {
            Flush3DBatch();
        }

        const float radians = rotation * 3.14159265359f / 180.0f;
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        if (m_pResources->IsRenderTexture(texture.m_Id) && source.height < 0.0f)
        {
            source.y += source.height;
            source.height = -source.height;
        }

        const float u0 = source.x / texture.m_Width;
        const float v0 = source.y / texture.m_Height;
        const float u1 = (source.x + source.width) / texture.m_Width;
        const float v1 = (source.y + source.height) / texture.m_Height;
        const Vec2 aLocal[] =
            {
                {-origin.x, -origin.y},
                {destination.width - origin.x, -origin.y},
                {destination.width - origin.x, destination.height - origin.y},
                {-origin.x, destination.height - origin.y}};
        const Vec2 aUv[] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        const float aColor[] =
            {
                tint.r / 255.0f,
                tint.g / 255.0f,
                tint.b / 255.0f,
                tint.a / 255.0f};

        if (m_ShaderOverride.Active())
        {
            Flush2DBatch();

            const UINT floatsPerVertex = m_ShaderOverride.m_StrideBytes / sizeof(float);
            std::vector<float> vVertices(static_cast<size_t>(floatsPerVertex) * 6, 0.0f);

            const int aIndices[] = {0, 1, 2, 0, 2, 3};
            for (int index = 0; index < 6; ++index)
            {
                const int vertexIndex = aIndices[index];
                const float rotatedX = aLocal[vertexIndex].x * cosine - aLocal[vertexIndex].y * sine;
                const float rotatedY = aLocal[vertexIndex].x * sine + aLocal[vertexIndex].y * cosine;
                Vec2 position{destination.x + rotatedX, destination.y + rotatedY};
                if (m_Camera2DActive)
                {
                    position = GetWorldToScreen2D(position, m_Camera2D);
                }

                float* pVertex = vVertices.data() + static_cast<size_t>(index) * floatsPerVertex;
                pVertex[m_ShaderOverride.m_PositionOffset / sizeof(float)] =
                    position.x / m_ActiveWidth * 2.0f - 1.0f;
                pVertex[m_ShaderOverride.m_PositionOffset / sizeof(float) + 1] =
                    1.0f - position.y / m_ActiveHeight * 2.0f;
                if (m_ShaderOverride.m_TexCoordOffset != 0xFFFFFFFFu)
                {
                    pVertex[m_ShaderOverride.m_TexCoordOffset / sizeof(float)] = aUv[vertexIndex].x;
                    pVertex[m_ShaderOverride.m_TexCoordOffset / sizeof(float) + 1] = aUv[vertexIndex].y;
                }
                if (m_ShaderOverride.m_ColorOffset != 0xFFFFFFFFu)
                {
                    std::memcpy(pVertex + m_ShaderOverride.m_ColorOffset / sizeof(float), aColor,
                                sizeof(aColor));
                }
            }

            m_pResources->UpdateDynamicBuffer(
                m_pContext,
                m_pPipeline->VertexBuffer(),
                vVertices.data(),
                static_cast<UINT>(vVertices.size() * sizeof(float)));
            BindOverride(pShaderResource);
            m_pContext->Draw(6, 0);
            RecordDebugRenderDraw(6);
            return;
        }

        if (!m_DrawingDebugOverlay && m_vBatchVertices.size() + 4 > BATCH_MAX_VERTICES)
        {
            Flush2DBatch();
        }

        BatchVertex aBatch[4]{};
        for (int index = 0; index < 4; ++index)
        {
            const float rotatedX = aLocal[index].x * cosine - aLocal[index].y * sine;
            const float rotatedY = aLocal[index].x * sine + aLocal[index].y * cosine;
            Vec2 position{destination.x + rotatedX, destination.y + rotatedY};
            if (m_Camera2DActive && !m_DrawingDebugOverlay)
            {
                position = GetWorldToScreen2D(position, m_Camera2D);
            }

            aBatch[index].m_X = position.x;
            aBatch[index].m_Y = position.y;
            aBatch[index].m_U = aUv[index].x;
            aBatch[index].m_V = aUv[index].y;
            std::memcpy(&aBatch[index].m_R, aColor, sizeof(aColor));
        }
        BatchQuad(aBatch, pShaderResource);
    }

    void D3D11CommandContext::BatchTriangle(const BatchVertex aVertices[3])
    {
        if (m_DrawingDebugOverlay)
        {
            const uint32_t baseVertex = static_cast<uint32_t>(m_vOverlayVertices.size());
            const UINT indexStart = static_cast<UINT>(m_vOverlayIndices.size());

            m_vOverlayVertices.insert(m_vOverlayVertices.end(), aVertices, aVertices + 3);
            m_vOverlayIndices.push_back(baseVertex);
            m_vOverlayIndices.push_back(baseVertex + 1);
            m_vOverlayIndices.push_back(baseVertex + 2);
            if (!m_vOverlayDrawItems.empty() && m_vOverlayDrawItems.back().m_pShaderResource == nullptr)
            {
                m_vOverlayDrawItems.back().m_IndexCount += 3;
            }
            else
            {
                m_vOverlayDrawItems.push_back(DrawItem{indexStart, 3, nullptr});
            }
            return;
        }

        const uint32_t baseVertex = static_cast<uint32_t>(m_vBatchVertices.size());
        const UINT indexStart = static_cast<UINT>(m_vBatchIndices.size());

        m_vBatchVertices.insert(m_vBatchVertices.end(), aVertices, aVertices + 3);
        m_vBatchIndices.push_back(baseVertex);
        m_vBatchIndices.push_back(baseVertex + 1);
        m_vBatchIndices.push_back(baseVertex + 2);
        if (!m_vBatchDrawItems.empty() && m_vBatchDrawItems.back().m_pShaderResource == nullptr)
        {
            m_vBatchDrawItems.back().m_IndexCount += 3;
        }
        else
        {
            m_vBatchDrawItems.push_back(DrawItem{indexStart, 3, nullptr});
        }
    }

    void D3D11CommandContext::BatchQuad(const BatchVertex aVertices[4], ID3D11ShaderResourceView* pShaderResource)
    {
        if (m_DrawingDebugOverlay)
        {
            const uint32_t baseVertex = static_cast<uint32_t>(m_vOverlayVertices.size());
            const UINT indexStart = static_cast<UINT>(m_vOverlayIndices.size());

            m_vOverlayVertices.insert(m_vOverlayVertices.end(), aVertices, aVertices + 4);
            m_vOverlayIndices.push_back(baseVertex);
            m_vOverlayIndices.push_back(baseVertex + 1);
            m_vOverlayIndices.push_back(baseVertex + 2);
            m_vOverlayIndices.push_back(baseVertex);
            m_vOverlayIndices.push_back(baseVertex + 2);
            m_vOverlayIndices.push_back(baseVertex + 3);
            if (!m_vOverlayDrawItems.empty() && m_vOverlayDrawItems.back().m_pShaderResource == pShaderResource)
            {
                m_vOverlayDrawItems.back().m_IndexCount += 6;
            }
            else
            {
                m_vOverlayDrawItems.push_back(DrawItem{indexStart, 6, pShaderResource});
            }
            return;
        }

        const uint32_t baseVertex = static_cast<uint32_t>(m_vBatchVertices.size());
        const UINT indexStart = static_cast<UINT>(m_vBatchIndices.size());

        m_vBatchVertices.insert(m_vBatchVertices.end(), aVertices, aVertices + 4);
        m_vBatchIndices.push_back(baseVertex);
        m_vBatchIndices.push_back(baseVertex + 1);
        m_vBatchIndices.push_back(baseVertex + 2);
        m_vBatchIndices.push_back(baseVertex);
        m_vBatchIndices.push_back(baseVertex + 2);
        m_vBatchIndices.push_back(baseVertex + 3);
        if (!m_vBatchDrawItems.empty() && m_vBatchDrawItems.back().m_pShaderResource == pShaderResource)
        {
            m_vBatchDrawItems.back().m_IndexCount += 6;
        }
        else
        {
            m_vBatchDrawItems.push_back(DrawItem{indexStart, 6, pShaderResource});
        }
    }

    void D3D11CommandContext::Flush2DBatch()
    {
        if (!m_pContext || !m_pPipeline || !m_BatchVertexBuffer || !m_BatchIndexBuffer ||
            m_vBatchVertices.empty())
        {
            return;
        }

        if (m_ActiveWidth <= 0 || m_ActiveHeight <= 0)
        {
            m_vBatchVertices.clear();
            m_vBatchIndices.clear();
            m_vBatchDrawItems.clear();
            return;
        }

        for (BatchVertex& vertex : m_vBatchVertices)
        {
            vertex.m_X = vertex.m_X / m_ActiveWidth * 2.0f - 1.0f;
            vertex.m_Y = 1.0f - vertex.m_Y / m_ActiveHeight * 2.0f;
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT result = m_pContext->Map(m_BatchVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                                         &mapped);
        if (FAILED(result))
        {
            m_vBatchVertices.clear();
            m_vBatchIndices.clear();
            m_vBatchDrawItems.clear();
            return;
        }
        std::memcpy(mapped.pData, m_vBatchVertices.data(), m_vBatchVertices.size() * sizeof(BatchVertex));
        m_pContext->Unmap(m_BatchVertexBuffer.Get(), 0);

        result = m_pContext->Map(m_BatchIndexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(result))
        {
            m_vBatchVertices.clear();
            m_vBatchIndices.clear();
            m_vBatchDrawItems.clear();
            return;
        }
        std::memcpy(mapped.pData, m_vBatchIndices.data(), m_vBatchIndices.size() * sizeof(uint32_t));
        m_pContext->Unmap(m_BatchIndexBuffer.Get(), 0);

        m_pPipeline->BindBatch(m_pContext, m_BatchVertexBuffer.Get(), m_BatchIndexBuffer.Get());

        ID3D11ShaderResourceView* pBoundSRV = nullptr;
        for (const DrawItem& item : m_vBatchDrawItems)
        {
            ID3D11ShaderResourceView* pSrv = item.m_pShaderResource ? item.m_pShaderResource
                                                                  : m_WhiteShaderResource.Get();
            if (pSrv != pBoundSRV)
            {
                m_pContext->PSSetShaderResources(0, 1, &pSrv);
                pBoundSRV = pSrv;
            }
            m_pContext->DrawIndexed(item.m_IndexCount, item.m_IndexStart, 0);
            RecordDebugRenderDraw(item.m_IndexCount);
        }

        m_vBatchVertices.clear();
        m_vBatchIndices.clear();
        m_vBatchDrawItems.clear();
    }

    void D3D11CommandContext::FlushBatch()
    {
        Flush3DBatch();
        Flush2DBatch();
    }

    void D3D11CommandContext::FlushDebugOverlay()
    {
        if (!m_pContext || !m_pPipeline || !m_BatchVertexBuffer || !m_BatchIndexBuffer ||
            m_vOverlayVertices.empty())
        {
            m_vOverlayVertices.clear();
            m_vOverlayIndices.clear();
            m_vOverlayDrawItems.clear();
            return;
        }

        const int targetWidth = m_DefaultWidth > 0 ? m_DefaultWidth : m_ActiveWidth;
        const int targetHeight = m_DefaultHeight > 0 ? m_DefaultHeight : m_ActiveHeight;
        if (targetWidth <= 0 || targetHeight <= 0)
        {
            m_vOverlayVertices.clear();
            m_vOverlayIndices.clear();
            m_vOverlayDrawItems.clear();
            return;
        }

        ID3D11RenderTargetView* pRenderTarget = m_pSwapChain ? m_pSwapChain->RenderTarget() : nullptr;
        if (!pRenderTarget)
        {
            m_vOverlayVertices.clear();
            m_vOverlayIndices.clear();
            m_vOverlayDrawItems.clear();
            return;
        }
        m_pContext->OMSetRenderTargets(1, &pRenderTarget, nullptr);

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(targetWidth);
        viewport.Height = static_cast<float>(targetHeight);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        m_pContext->RSSetViewports(1, &viewport);

        D3D11_RECT scissorRect{0, 0, targetWidth, targetHeight};
        m_pContext->RSSetScissorRects(1, &scissorRect);

        for (BatchVertex& vertex : m_vOverlayVertices)
        {
            vertex.m_X = vertex.m_X / targetWidth * 2.0f - 1.0f;
            vertex.m_Y = 1.0f - vertex.m_Y / targetHeight * 2.0f;
        }

        const size_t vertexCount = std::min<size_t>(m_vOverlayVertices.size(), BATCH_MAX_VERTICES);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT result = m_pContext->Map(m_BatchVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(result))
        {
            m_vOverlayVertices.clear();
            m_vOverlayIndices.clear();
            m_vOverlayDrawItems.clear();
            return;
        }
        std::memcpy(mapped.pData, m_vOverlayVertices.data(), vertexCount * sizeof(BatchVertex));
        m_pContext->Unmap(m_BatchVertexBuffer.Get(), 0);

        const size_t indexCount = std::min<size_t>(m_vOverlayIndices.size(), BATCH_MAX_VERTICES * 3);
        result = m_pContext->Map(m_BatchIndexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(result))
        {
            m_vOverlayVertices.clear();
            m_vOverlayIndices.clear();
            m_vOverlayDrawItems.clear();
            return;
        }
        std::memcpy(mapped.pData, m_vOverlayIndices.data(), indexCount * sizeof(uint32_t));
        m_pContext->Unmap(m_BatchIndexBuffer.Get(), 0);

        m_pPipeline->BindBatch(m_pContext, m_BatchVertexBuffer.Get(), m_BatchIndexBuffer.Get());

        ID3D11ShaderResourceView* pBoundSRV = nullptr;
        for (const DrawItem& item : m_vOverlayDrawItems)
        {
            ID3D11ShaderResourceView* pSrv = item.m_pShaderResource ? item.m_pShaderResource
                                                                  : m_WhiteShaderResource.Get();
            if (pSrv != pBoundSRV)
            {
                m_pContext->PSSetShaderResources(0, 1, &pSrv);
                pBoundSRV = pSrv;
            }
            m_pContext->DrawIndexed(item.m_IndexCount, item.m_IndexStart, 0);
            RecordDebugRenderDraw(item.m_IndexCount);
        }

        m_vOverlayVertices.clear();
        m_vOverlayIndices.clear();
        m_vOverlayDrawItems.clear();
    }

    void D3D11CommandContext::BindOverride(ID3D11ShaderResourceView* pDrawnResource)
    {
        if (!m_pContext || !m_pPipeline)
        {
            return;
        }

        const ShaderOverride& shaderOverride = m_ShaderOverride;
        const UINT stride = shaderOverride.m_StrideBytes;
        const UINT offset = 0;
        ID3D11Buffer* pVertexBuffer = m_pPipeline->VertexBuffer();

        m_pContext->IASetInputLayout(shaderOverride.m_pInputLayout);
        m_pContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &stride, &offset);
        m_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_pContext->RSSetState(m_pPipeline->Rasterizer());
        m_pContext->VSSetShader(shaderOverride.m_pVertexShader, nullptr, 0);
        m_pContext->PSSetShader(shaderOverride.m_pPixelShader, nullptr, 0);

        if (shaderOverride.m_pConstantBuffer)
        {
            ID3D11Buffer* pConstantBuffer = shaderOverride.m_pConstantBuffer;
            m_pContext->VSSetConstantBuffers(0, 1, &pConstantBuffer);
            m_pContext->PSSetConstantBuffers(0, 1, &pConstantBuffer);
        }

        ID3D11ShaderResourceView* apResources[10] = {};
        for (size_t index = 0; index < 10; ++index)
        {
            apResources[index] = shaderOverride.m_apShaderResources[index];
        }
        if (pDrawnResource)
        {
            apResources[0] = pDrawnResource;
        }
        m_pContext->PSSetShaderResources(0, 10, apResources);

        ID3D11SamplerState* pSampler = m_pPipeline->Sampler();
        m_pContext->PSSetSamplers(0, 1, &pSampler);

        const float aBlendFactor[] = {0.0f, 0.0f, 0.0f, 0.0f};
        m_pContext->OMSetDepthStencilState(m_pPipeline->DepthStencilDisabledState(), 0);
        m_pContext->OMSetBlendState(m_pPipeline->Blend(), aBlendFactor, 0xFFFFFFFF);
    }

    void D3D11CommandContext::BeginTextureMode(const IRenderTexture& target)
    {
        if (!m_pContext || !m_pResources || target.m_Id == 0)
        {
            return;
        }

        ID3D11RenderTargetView* pRenderTarget = m_pResources->RenderTarget(target.m_Id);
        if (!pRenderTarget)
        {
            return;
        }

        FlushBatch();

        ID3D11ShaderResourceView* apNullResources[10] = {};
        m_pContext->PSSetShaderResources(0, 10, apNullResources);

        ID3D11DepthStencilView* pDepthStencil = m_pResources->DepthStencil(target.m_Id);
        m_pContext->OMSetRenderTargets(1, &pRenderTarget, pDepthStencil);
        m_pActiveRenderTarget = pRenderTarget;
        m_pActiveDepthStencil = pDepthStencil;
        m_ActiveWidth = target.m_Texture.m_Width;
        m_ActiveHeight = target.m_Texture.m_Height;
        RefreshViewport(target.m_Texture.m_Width, target.m_Texture.m_Height);

        if (pDepthStencil)
        {
            m_pContext->ClearDepthStencilView(pDepthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                              1.0f, 0);
        }
    }

    void D3D11CommandContext::EndTextureMode(int width, int height)
    {
        FlushBatch();
        BeginDrawing();
        const int restoreWidth = width > 0 ? width : m_DefaultWidth;
        const int restoreHeight = height > 0 ? height : m_DefaultHeight;
        m_ActiveWidth = restoreWidth;
        m_ActiveHeight = restoreHeight;
        RefreshViewport(restoreWidth, restoreHeight);
    }

    void D3D11CommandContext::BeginMode2D(const Camera2D& camera)
    {
        m_Camera2D = camera;
        m_Camera2DActive = true;
    }

    void D3D11CommandContext::EndMode2D()
    {
        m_Camera2DActive = false;
    }

    void D3D11CommandContext::Shutdown()
    {
        TraceLog(LogLevel::Trace, "D3D11", "Command context shut down.");

        m_vWorkers.clear();
        m_vQueued3DDraws.clear();
        m_vQueued3DVertices.clear();
        m_vQueued3DConstants.clear();
        m_vQueued3DInstances.clear();
        m_InstanceSRV.Reset();
        m_InstanceBuffer.Reset();
        m_RingConstantBuffer.Reset();
        m_Context1.Reset();
        m_pThreadPool = nullptr;
        m_pContext = nullptr;
        m_pSwapChain = nullptr;
        m_pPipeline = nullptr;
        m_pResources = nullptr;
        m_pActiveRenderTarget = nullptr;
        m_pActiveDepthStencil = nullptr;
        m_DefaultWidth = 0;
        m_DefaultHeight = 0;
        m_ActiveWidth = 0;
        m_ActiveHeight = 0;
        m_Camera2D = {};
        m_Camera2DActive = false;
        m_ShaderOverride = {};
        m_BatchVertexBuffer.Reset();
        m_BatchIndexBuffer.Reset();
        m_WhiteTexture.Reset();
        m_WhiteShaderResource.Reset();
        m_vBatchVertices.clear();
        m_vBatchIndices.clear();
        m_vBatchDrawItems.clear();
        m_DrawingDebugOverlay = false;
        m_vOverlayVertices.clear();
        m_vOverlayIndices.clear();
        m_vOverlayDrawItems.clear();
    }
} // qci
#endif
