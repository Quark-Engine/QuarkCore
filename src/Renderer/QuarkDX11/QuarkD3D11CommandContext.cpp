#include "QuarkD3D11CommandContext.hpp"
#include "../QuarkIRenderer.hpp"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <vector>

#if defined(_WIN32)
namespace qci {
void D3D11CommandContext::Initialize(const D3D11Device &device, D3D11SwapChain &swapChain,
                                     D3D11Pipeline &pipeline, D3D11Resources &resources,
                                     int width, int height, QuarkRenderThreadPool *threadPool)
{
    m_context = device.Context();
    m_swapChain = &swapChain;
    m_pipeline = &pipeline;
    m_resources = &resources;
    m_threadPool = threadPool;
    m_activeRenderTarget = swapChain.RenderTarget();
    m_defaultWidth = width;
    m_defaultHeight = height;
    m_activeWidth = width;
    m_activeHeight = height;

    ID3D11Device *nativeDevice = device.Get();
    {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = kBatchMaxVertices * sizeof(BatchVertex);
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        d3d11::ThrowIfFailed(nativeDevice->CreateBuffer(&description, nullptr, &m_batchVertexBuffer),
                             "ID3D11Device::CreateBuffer batch vertices");
    }
    {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = kBatchMaxVertices * 3 / 2 * sizeof(uint32_t);
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_INDEX_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        d3d11::ThrowIfFailed(nativeDevice->CreateBuffer(&description, nullptr, &m_batchIndexBuffer),
                             "ID3D11Device::CreateBuffer batch indices");
    }
    {
        static constexpr uint8_t whitePixel[4] = {255, 255, 255, 255};
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
        initialData.pSysMem = whitePixel;
        initialData.SysMemPitch = sizeof(whitePixel);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> whiteTexture;
        d3d11::ThrowIfFailed(nativeDevice->CreateTexture2D(&description, &initialData, &whiteTexture),
                             "ID3D11Device::CreateTexture2D batch white");
        m_whiteTexture = whiteTexture;
        d3d11::ThrowIfFailed(
            nativeDevice->CreateShaderResourceView(m_whiteTexture.Get(), nullptr,
                                                   &m_whiteShaderResource),
            "ID3D11Device::CreateShaderResourceView batch white");
    }

    m_workers.clear();
    const uint32_t workerCount = (m_threadPool && m_threadPool->WorkerCount() > 1)
                                     ? m_threadPool->WorkerCount()
                                     : 0u;
    if (workerCount > 1 && nativeDevice)
    {
        m_workers.resize(workerCount);
        bool allSucceeded = true;
        for (uint32_t i = 0; i < workerCount; ++i)
        {
            DeferredWorkerContext &worker = m_workers[i];
            if (FAILED(nativeDevice->CreateDeferredContext(0, &worker.deferredContext)) ||
                !worker.deferredContext)
            {
                allSucceeded = false;
                break;
            }

            D3D11_BUFFER_DESC vbDesc{};
            vbDesc.ByteWidth = static_cast<UINT>(kMax3DBufferBytes);
            vbDesc.Usage = D3D11_USAGE_DYNAMIC;
            vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(nativeDevice->CreateBuffer(&vbDesc, nullptr, &worker.vertexBuffer3D)))
            {
                allSucceeded = false;
                break;
            }

            D3D11_BUFFER_DESC cbDesc{};
            cbDesc.ByteWidth = static_cast<UINT>(kMaxWorkerConstantBufferBytes);
            cbDesc.Usage = D3D11_USAGE_DYNAMIC;
            cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(nativeDevice->CreateBuffer(&cbDesc, nullptr, &worker.constantBuffer)))
            {
                allSucceeded = false;
                break;
            }
        }
        if (!allSucceeded)
        {
            m_workers.clear();
        }
    }

    m_batchVertices.reserve(kBatchMaxVertices);
    m_batchIndices.reserve(kBatchMaxVertices);
    m_batchDrawItems.reserve(kBatchMaxVertices);
    m_queued3DVertices.reserve(65536 * 22);
    m_queued3DConstants.reserve(65536);
    m_queued3DDraws.reserve(1024);

    TraceLog(LogLevel::Trace, "D3D11",
             TextFormat("Command context initialized (%zu deferred workers).", m_workers.size()));
}

void D3D11CommandContext::RefreshViewport(int width, int height)
{
    if (!m_context)
    {
        return;
    }

    D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                            0.0f, 1.0f};

    m_context->RSSetViewports(1, &viewport);
}

void D3D11CommandContext::SetDefaultViewportSize(int width, int height)
{
    m_defaultWidth = width;
    m_defaultHeight = height;
}

void D3D11CommandContext::BeginDrawing()
{
    if (!m_context || !m_swapChain)
    {
        return;
    }

    ID3D11RenderTargetView *renderTarget = m_swapChain->RenderTarget();
    m_activeRenderTarget = renderTarget;
    m_activeDepthStencil = m_swapChain->DepthStencilView();
    m_activeWidth = m_defaultWidth > 0 ? m_defaultWidth : m_activeWidth;
    m_activeHeight = m_defaultHeight > 0 ? m_defaultHeight : m_activeHeight;
    m_context->OMSetRenderTargets(1, &renderTarget, m_activeDepthStencil);
    RefreshViewport(m_activeWidth, m_activeHeight);
}

void D3D11CommandContext::Present(bool vsync)
{
    FlushBatch();
    FlushDebugOverlay();
    if (m_swapChain)
    {
        m_swapChain->Resolve();
        m_swapChain->Present(vsync);
    }
}

void D3D11CommandContext::Clear(Color color)
{
    if (!m_context || !m_swapChain)
    {
        return;
    }

    FlushBatch();

    const float clearColor[] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                                color.a / 255.0f};

    m_context->ClearRenderTargetView(m_activeRenderTarget, clearColor);
    ClearDepthStencil();
}

void D3D11CommandContext::ClearDepthStencil()
{
    if (!m_context || !m_swapChain)
    {
        return;
    }

    Flush3DBatch();

    ID3D11DepthStencilView *depthStencil = m_activeDepthStencil;
    if (depthStencil)
    {
        m_context->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                         1.0f, 0);
    }
}

void D3D11CommandContext::ReleaseRenderTargets()
{
    if (!m_context)
    {
        return;
    }

    FlushBatch();

    ID3D11RenderTargetView *nullRenderTarget = nullptr;
    ID3D11DepthStencilView *nullDepthStencil = nullptr;
    m_context->OMSetRenderTargets(1, &nullRenderTarget, nullDepthStencil);
    m_activeRenderTarget = nullptr;
    m_activeDepthStencil = nullptr;
}

size_t D3D11CommandContext::Allocate3DVertexSlice(UINT vertexCount, UINT strideFloats)
{
    const size_t offset = m_queued3DVertices.size();
    m_queued3DVertices.resize(offset + static_cast<size_t>(vertexCount) * strideFloats);
    return offset;
}

void D3D11CommandContext::SubmitQueued3D(size_t vertexOffsetFloats, UINT vertexCount,
                                         D3D_PRIMITIVE_TOPOLOGY topology)
{
    if (!m_context || !m_pipeline || !m_resources || !m_swapChain || vertexCount == 0 ||
        !m_activeDepthStencil)
    {
        return;
    }

    if (!m_batchDrawItems.empty())
    {
        Flush2DBatch();
    }

    const bool culling = m_pipeline->BackfaceCulling();
    if (!m_queued3DDraws.empty())
    {
        Queued3DDraw &prev = m_queued3DDraws.back();
        if (prev.mode == Queued3DMode::Untextured &&
            prev.topology == topology &&
            prev.backfaceCulling == culling &&
            (prev.vertexOffsetFloats + static_cast<size_t>(prev.vertexCount) * 16 == vertexOffsetFloats))
        {
            prev.vertexCount += vertexCount;
            return;
        }
    }

    Queued3DDraw draw{};
    draw.mode = Queued3DMode::Untextured;
    draw.vertexOffsetFloats = vertexOffsetFloats;
    draw.vertexCount = vertexCount;
    draw.topology = topology;
    draw.backfaceCulling = culling;
    m_queued3DDraws.push_back(draw);
}

void D3D11CommandContext::SubmitQueued3DTextured(size_t vertexOffsetFloats, UINT vertexCount,
                                                 ID3D11ShaderResourceView *shaderResource,
                                                 ID3D11ShaderResourceView *normalResource)
{
    if (!m_context || !m_pipeline || !m_resources || vertexCount == 0)
    {
        return;
    }

    if (!m_batchDrawItems.empty())
    {
        Flush2DBatch();
    }

    const bool culling = m_pipeline->BackfaceCulling();
    if (!m_queued3DDraws.empty())
    {
        Queued3DDraw &prev = m_queued3DDraws.back();
        if (prev.mode == Queued3DMode::Textured &&
            prev.shaderResource == shaderResource &&
            prev.normalResource == normalResource &&
            prev.backfaceCulling == culling &&
            (prev.vertexOffsetFloats + static_cast<size_t>(prev.vertexCount) * 22 == vertexOffsetFloats))
        {
            prev.vertexCount += vertexCount;
            return;
        }
    }

    Queued3DDraw draw{};
    draw.mode = Queued3DMode::Textured;
    draw.vertexOffsetFloats = vertexOffsetFloats;
    draw.vertexCount = vertexCount;
    draw.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    draw.shaderResource = shaderResource;
    draw.normalResource = normalResource;
    draw.backfaceCulling = culling;
    m_queued3DDraws.push_back(draw);
}

void D3D11CommandContext::SubmitQueued3DShader(size_t vertexOffsetFloats, UINT vertexCount,
                                               const ShaderOverride &shaderOverride)
{
    if (!m_context || !m_pipeline || !m_resources || !m_swapChain || !shaderOverride.Active() ||
        vertexCount == 0 || !m_activeDepthStencil)
    {
        return;
    }

    if (!m_batchDrawItems.empty())
    {
        Flush2DBatch();
    }

    Queued3DDraw draw{};
    draw.mode = Queued3DMode::CustomShader;
    draw.vertexOffsetFloats = vertexOffsetFloats;
    draw.vertexCount = vertexCount;
    draw.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    draw.shaderOverride = shaderOverride;
    draw.shaderOverride.constantData = nullptr;
    draw.shaderOverride.constantDataSize = 0;
    draw.backfaceCulling = m_pipeline->BackfaceCulling();

    if (shaderOverride.constantData && shaderOverride.constantDataSize > 0)
    {
        const uint32_t copyBytes = std::min<uint32_t>(
            shaderOverride.constantDataSize, static_cast<uint32_t>(kMaxWorkerConstantBufferBytes));
        draw.constantOffsetBytes = m_queued3DConstants.size();
        draw.constantSizeBytes = copyBytes;
        m_queued3DConstants.insert(m_queued3DConstants.end(), shaderOverride.constantData,
                                   shaderOverride.constantData + copyBytes);
    }

    m_queued3DDraws.push_back(draw);
}

void D3D11CommandContext::Draw3D(const float *vertices, UINT vertexCount,
                                 D3D_PRIMITIVE_TOPOLOGY topology)
{
    if (!m_context || !m_pipeline || !m_resources || !m_swapChain ||
        vertices == nullptr || vertexCount == 0 || !m_activeDepthStencil)
    {
        return;
    }

    constexpr UINT kStrideFloats = 16;
    const size_t offsetFloats = Allocate3DVertexSlice(vertexCount, kStrideFloats);
    std::memcpy(Get3DVertexPtr(offsetFloats), vertices,
                static_cast<size_t>(vertexCount) * kStrideFloats * sizeof(float));
    SubmitQueued3D(offsetFloats, vertexCount, topology);
}

void D3D11CommandContext::Draw3DTextured(const float *vertices, UINT vertexCount,
                                         ID3D11ShaderResourceView *shaderResource,
                                         ID3D11ShaderResourceView *normalResource)
{
    if (!m_context || !m_pipeline || !m_resources || !vertices || vertexCount == 0)
    {
        return;
    }

    constexpr UINT kStrideFloats = 22;
    const size_t offsetFloats = Allocate3DVertexSlice(vertexCount, kStrideFloats);
    std::memcpy(Get3DVertexPtr(offsetFloats), vertices,
                static_cast<size_t>(vertexCount) * kStrideFloats * sizeof(float));
    SubmitQueued3DTextured(offsetFloats, vertexCount, shaderResource, normalResource);
}

void D3D11CommandContext::Draw3DShader(const float *vertices, UINT vertexCount,
                                       const ShaderOverride &shaderOverride)
{
    if (!m_context || !m_pipeline || !m_resources || !m_swapChain || !shaderOverride.Active() ||
        vertices == nullptr || vertexCount == 0 || !m_activeDepthStencil)
    {
        return;
    }

    const UINT strideFloats = shaderOverride.strideBytes / sizeof(float);
    if (strideFloats == 0)
    {
        return;
    }
    const size_t offsetFloats = Allocate3DVertexSlice(vertexCount, strideFloats);
    std::memcpy(Get3DVertexPtr(offsetFloats), vertices,
                static_cast<size_t>(vertexCount) * shaderOverride.strideBytes);
    SubmitQueued3DShader(offsetFloats, vertexCount, shaderOverride);
}

void D3D11CommandContext::ExecuteQueued3DDrawOnContext(ID3D11DeviceContext *ctx,
                                                       ID3D11Buffer *vertexBuffer3D,
                                                       ID3D11Buffer *workerConstantBuffer,
                                                       size_t &vbByteOffset,
                                                       const Queued3DDraw &draw)
{
    if (!ctx || !vertexBuffer3D || draw.vertexCount == 0)
    {
        return;
    }

    UINT strideBytes = 0;
    UINT strideFloats = 0;
    const float blendFactor[] = {0.0f, 0.0f, 0.0f, 0.0f};
    ID3D11RasterizerState *rasterizer =
        draw.backfaceCulling ? m_pipeline->RasterizerCull() : m_pipeline->Rasterizer();

    if (draw.mode == Queued3DMode::Untextured)
    {
        strideFloats = 16;
        strideBytes = sizeof(float) * strideFloats;
        const UINT offset = 0;
        ctx->IASetInputLayout(m_pipeline->InputLayout3D());
        ctx->IASetVertexBuffers(0, 1, &vertexBuffer3D, &strideBytes, &offset);
        ctx->IASetPrimitiveTopology(draw.topology);
        ctx->RSSetState(rasterizer);
        ctx->VSSetShader(m_pipeline->VertexShader3D(), nullptr, 0);
        ctx->PSSetShader(m_pipeline->PixelShader3D(), nullptr, 0);
        ctx->PSSetConstantBuffers(1, 1, m_pipeline->LightConstantBuffer());
        ctx->OMSetDepthStencilState(m_pipeline->DepthStencilState(), 0);
        ctx->OMSetBlendState(m_pipeline->Blend(), blendFactor, 0xFFFFFFFF);
    }
    else if (draw.mode == Queued3DMode::Textured)
    {
        strideFloats = 22;
        strideBytes = sizeof(float) * strideFloats;
        const UINT offset = 0;
        ctx->IASetInputLayout(m_pipeline->TexturedInputLayout3D());
        ctx->IASetVertexBuffers(0, 1, &vertexBuffer3D, &strideBytes, &offset);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->RSSetState(rasterizer);
        ctx->VSSetShader(m_pipeline->TexturedVertexShader3D(), nullptr, 0);
        ctx->PSSetShader(m_pipeline->TexturedPixelShader3D(), nullptr, 0);
        ctx->PSSetConstantBuffers(1, 1, m_pipeline->LightConstantBuffer());
        ID3D11ShaderResourceView *resources[2] = {draw.shaderResource, draw.normalResource};
        ctx->PSSetShaderResources(0, 2, resources);
        ID3D11SamplerState *sampler = m_pipeline->Sampler();
        ctx->PSSetSamplers(0, 1, &sampler);
        ctx->OMSetDepthStencilState(m_pipeline->DepthStencilState(), 0);
        ctx->OMSetBlendState(m_pipeline->Blend(), blendFactor, 0xFFFFFFFF);
    }
    else
    {
        strideBytes = draw.shaderOverride.strideBytes;
        strideFloats = strideBytes / sizeof(float);
        if (strideBytes == 0 || strideFloats == 0)
        {
            return;
        }
        const UINT offset = 0;
        ctx->IASetInputLayout(draw.shaderOverride.inputLayout);
        ctx->IASetVertexBuffers(0, 1, &vertexBuffer3D, &strideBytes, &offset);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->RSSetState(rasterizer);
        ctx->VSSetShader(draw.shaderOverride.vertexShader, nullptr, 0);
        ctx->PSSetShader(draw.shaderOverride.pixelShader, nullptr, 0);

        ID3D11Buffer *targetConstantBuffer =
            workerConstantBuffer ? workerConstantBuffer : draw.shaderOverride.constantBuffer;
        if (draw.constantSizeBytes > 0 && targetConstantBuffer)
        {
            D3D11_MAPPED_SUBRESOURCE mappedCB{};
            if (SUCCEEDED(ctx->Map(targetConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCB)))
            {
                std::memcpy(mappedCB.pData,
                            m_queued3DConstants.data() + draw.constantOffsetBytes,
                            draw.constantSizeBytes);
                ctx->Unmap(targetConstantBuffer, 0);
            }
            ctx->VSSetConstantBuffers(0, 1, &targetConstantBuffer);
            ctx->PSSetConstantBuffers(0, 1, &targetConstantBuffer);
        }
        else if (draw.shaderOverride.constantBuffer)
        {
            ID3D11Buffer *constantBuffer = draw.shaderOverride.constantBuffer;
            ctx->VSSetConstantBuffers(0, 1, &constantBuffer);
            ctx->PSSetConstantBuffers(0, 1, &constantBuffer);
        }
        ctx->PSSetConstantBuffers(1, 1, m_pipeline->LightConstantBuffer());

        ID3D11ShaderResourceView *resources[10] = {};
        for (size_t index = 0; index < 10; ++index)
        {
            resources[index] = draw.shaderOverride.shaderResources[index];
        }
        ctx->PSSetShaderResources(0, 10, resources);

        ID3D11SamplerState *sampler = m_pipeline->Sampler();
        ctx->PSSetSamplers(0, 1, &sampler);
        ctx->OMSetDepthStencilState(m_pipeline->DepthStencilState(), 0);
        ctx->OMSetBlendState(m_pipeline->Blend(), blendFactor, 0xFFFFFFFF);
    }

    const UINT maxChunkVertices = static_cast<UINT>(kMax3DBufferBytes / strideBytes);
    const float *cursor = m_queued3DVertices.data() + draw.vertexOffsetFloats;
    UINT remaining = draw.vertexCount;
    while (remaining > 0)
    {
        const UINT chunk = std::min(remaining, maxChunkVertices);
        const size_t chunkBytes = static_cast<size_t>(chunk) * strideBytes;

        size_t alignedOffset = (vbByteOffset == 0)
                                   ? 0
                                   : ((vbByteOffset + strideBytes - 1) / strideBytes) * strideBytes;
        D3D11_MAP mapType = D3D11_MAP_WRITE_DISCARD;
        if (vbByteOffset > 0 && alignedOffset + chunkBytes <= kMax3DBufferBytes)
        {
            mapType = D3D11_MAP_WRITE_NO_OVERWRITE;
        }
        else
        {
            alignedOffset = 0;
            mapType = D3D11_MAP_WRITE_DISCARD;
        }

        D3D11_MAPPED_SUBRESOURCE mappedVB{};
        if (SUCCEEDED(ctx->Map(vertexBuffer3D, 0, mapType, 0, &mappedVB)))
        {
            std::memcpy(static_cast<uint8_t *>(mappedVB.pData) + alignedOffset, cursor, chunkBytes);
            ctx->Unmap(vertexBuffer3D, 0);
            const UINT startVertex = static_cast<UINT>(alignedOffset / strideBytes);
            ctx->Draw(chunk, startVertex);
            RecordDebugRenderDraw(chunk);
            vbByteOffset = alignedOffset + chunkBytes;
        }

        remaining -= chunk;
        cursor += static_cast<size_t>(chunk) * strideFloats;
    }
}

void D3D11CommandContext::Flush3DBatch()
{
    if (m_queued3DDraws.empty())
    {
        return;
    }

    if (!m_context || !m_pipeline)
    {
        m_queued3DDraws.clear();
        m_queued3DVertices.clear();
        m_queued3DConstants.clear();
        return;
    }

    const size_t drawCount = m_queued3DDraws.size();
    const bool useDeferredWorkers =
        drawCount >= 64 && m_threadPool && m_threadPool->IsMultithreaded() && !m_workers.empty();

    if (!useDeferredWorkers)
    {
        size_t vbByteOffset = 0;
        ID3D11Buffer *workerCB = !m_workers.empty() ? m_workers[0].constantBuffer.Get() : nullptr;
        for (const Queued3DDraw &draw : m_queued3DDraws)
        {
            ExecuteQueued3DDrawOnContext(m_context, m_pipeline->VertexBuffer3D(), workerCB,
                                         vbByteOffset, draw);
        }
    }
    else
    {
        const uint32_t chunkCount =
            static_cast<uint32_t>(std::min(m_workers.size(), drawCount));
        const D3D11_VIEWPORT viewport{0.0f,
                                      0.0f,
                                      static_cast<float>(m_activeWidth),
                                      static_cast<float>(m_activeHeight),
                                      0.0f,
                                      1.0f};
        ID3D11RenderTargetView *activeRTV = m_activeRenderTarget;
        ID3D11DepthStencilView *activeDSV = m_activeDepthStencil;

        m_threadPool->ParallelFor(
            chunkCount, 1, [&](uint32_t, size_t chunkBegin, size_t chunkEnd) {
                for (size_t c = chunkBegin; c < chunkEnd; ++c)
                {
                    DeferredWorkerContext &worker = m_workers[c];
                    ID3D11DeviceContext *ctx = worker.deferredContext.Get();
                    if (!ctx)
                    {
                        continue;
                    }

                    const size_t firstDraw = (c * drawCount) / chunkCount;
                    const size_t lastDraw = ((c + 1) * drawCount) / chunkCount;
                    if (firstDraw >= lastDraw)
                    {
                        continue;
                    }

                    ctx->OMSetRenderTargets(1, &activeRTV, activeDSV);
                    ctx->RSSetViewports(1, &viewport);

                    size_t vbByteOffset = 0;
                    for (size_t d = firstDraw; d < lastDraw; ++d)
                    {
                        ExecuteQueued3DDrawOnContext(ctx, worker.vertexBuffer3D.Get(),
                                                     worker.constantBuffer.Get(), vbByteOffset,
                                                     m_queued3DDraws[d]);
                    }

                    ctx->FinishCommandList(FALSE, &worker.commandList);
                }
            });

        for (uint32_t c = 0; c < chunkCount; ++c)
        {
            if (m_workers[c].commandList)
            {
                m_context->ExecuteCommandList(m_workers[c].commandList.Get(), FALSE);
                m_workers[c].commandList.Reset();
            }
        }

        if (m_activeRenderTarget || m_activeDepthStencil)
        {
            m_context->OMSetRenderTargets(1, &m_activeRenderTarget, m_activeDepthStencil);
        }
        RefreshViewport(m_activeWidth, m_activeHeight);
    }

    ID3D11ShaderResourceView *nullResources[10] = {};
    m_context->PSSetShaderResources(0, 10, nullResources);

    m_queued3DDraws.clear();
    m_queued3DVertices.clear();
    m_queued3DConstants.clear();
}

void D3D11CommandContext::DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color, int, int)
{
    if (!m_context || !m_pipeline || !m_resources || m_activeWidth <= 0 || m_activeHeight <= 0)
    {
        return;
    }

    if (!m_queued3DDraws.empty())
    {
        Flush3DBatch();
    }

    const Vec2 points[3] = {
        m_camera2DActive ? GetWorldToScreen2D(v1, m_camera2D) : v1,
        m_camera2DActive ? GetWorldToScreen2D(v2, m_camera2D) : v2,
        m_camera2DActive ? GetWorldToScreen2D(v3, m_camera2D) : v3
    };

    if (m_shaderOverride.Active())
    {
        Flush2DBatch();

        const float colorValues[] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                                     color.a / 255.0f};
        const UINT floatsPerVertex = m_shaderOverride.strideBytes / sizeof(float);
        std::vector<float> vertices(static_cast<size_t>(floatsPerVertex) * 3, 0.0f);

        for (int index = 0; index < 3; ++index)
        {
            float *vertex = vertices.data() + static_cast<size_t>(index) * floatsPerVertex;
            vertex[m_shaderOverride.positionOffset / sizeof(float)] =
                points[index].x / m_activeWidth * 2.0f - 1.0f;
            vertex[m_shaderOverride.positionOffset / sizeof(float) + 1] =
                1.0f - points[index].y / m_activeHeight * 2.0f;
            if (m_shaderOverride.colorOffset != 0xFFFFFFFFu)
            {
                std::memcpy(vertex + m_shaderOverride.colorOffset / sizeof(float), colorValues,
                            sizeof(colorValues));
            }
            if (m_shaderOverride.texCoordOffset != 0xFFFFFFFFu)
            {
                vertex[m_shaderOverride.texCoordOffset / sizeof(float)] =
                    points[index].x / m_activeWidth;
                vertex[m_shaderOverride.texCoordOffset / sizeof(float)] =
                    points[index].x / m_activeWidth;
                vertex[m_shaderOverride.texCoordOffset / sizeof(float) + 1] =
                    points[index].y / m_activeHeight;
            }
        }

        m_resources->UpdateDynamicBuffer(m_context, m_pipeline->VertexBuffer(), vertices.data(),
                                         static_cast<UINT>(vertices.size() * sizeof(float)));
        BindOverride(nullptr);
        m_context->Draw(3, 0);
        RecordDebugRenderDraw(3);
        return;
    }

    if (m_batchVertices.size() + 3 > kBatchMaxVertices)
    {
        Flush2DBatch();
    }

    const float colorValues[] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                                 color.a / 255.0f};
    BatchVertex batch[3]{};
    for (int index = 0; index < 3; ++index)
    {
        batch[index].x = points[index].x;
        batch[index].y = points[index].y;
        std::memcpy(&batch[index].r, colorValues, sizeof(colorValues));
    }
    BatchTriangle(batch);
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
    if (!m_context || !m_pipeline || !m_resources || !texture.IsValid() ||
        texture.width <= 0 || texture.height <= 0 || m_activeWidth <= 0 || m_activeHeight <= 0) {
        return;
    }

    ID3D11ShaderResourceView* shaderResource = m_resources->ShaderResource(texture.id);
    if (!shaderResource) {
        return;
    }

    if (!m_queued3DDraws.empty())
    {
        Flush3DBatch();
    }

    const float radians = rotation * 3.14159265359f / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    if (m_resources->IsRenderTexture(texture.id) && source.height < 0.0f) {
        source.y += source.height;
        source.height = -source.height;
    }

    const float u0 = source.x / texture.width;
    const float v0 = source.y / texture.height;
    const float u1 = (source.x + source.width) / texture.width;
    const float v1 = (source.y + source.height) / texture.height;
    const Vec2 local[] = {
        {-origin.x, -origin.y},
        {destination.width - origin.x, -origin.y},
        {destination.width - origin.x, destination.height - origin.y},
        {-origin.x, destination.height - origin.y}
    };
    const Vec2 uv[] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    const float color[] = {
        tint.r / 255.0f,
        tint.g / 255.0f,
        tint.b / 255.0f,
        tint.a / 255.0f
    };

    if (m_shaderOverride.Active())
    {
        Flush2DBatch();

        const UINT floatsPerVertex = m_shaderOverride.strideBytes / sizeof(float);
        std::vector<float> vertices(static_cast<size_t>(floatsPerVertex) * 6, 0.0f);

        const int indices[] = {0, 1, 2, 0, 2, 3};
        for (int index = 0; index < 6; ++index)
        {
            const int vertexIndex = indices[index];
            const float rotatedX = local[vertexIndex].x * cosine - local[vertexIndex].y * sine;
            const float rotatedY = local[vertexIndex].x * sine + local[vertexIndex].y * cosine;
            Vec2 position{destination.x + rotatedX, destination.y + rotatedY};
            if (m_camera2DActive) {
                position = GetWorldToScreen2D(position, m_camera2D);
            }

            float *vertex = vertices.data() + static_cast<size_t>(index) * floatsPerVertex;
            vertex[m_shaderOverride.positionOffset / sizeof(float)] =
                position.x / m_activeWidth * 2.0f - 1.0f;
            vertex[m_shaderOverride.positionOffset / sizeof(float) + 1] =
                1.0f - position.y / m_activeHeight * 2.0f;
            if (m_shaderOverride.texCoordOffset != 0xFFFFFFFFu)
            {
                vertex[m_shaderOverride.texCoordOffset / sizeof(float)] = uv[vertexIndex].x;
                vertex[m_shaderOverride.texCoordOffset / sizeof(float) + 1] = uv[vertexIndex].y;
            }
            if (m_shaderOverride.colorOffset != 0xFFFFFFFFu)
            {
                std::memcpy(vertex + m_shaderOverride.colorOffset / sizeof(float), color,
                            sizeof(color));
            }
        }

        m_resources->UpdateDynamicBuffer(
            m_context,
            m_pipeline->VertexBuffer(),
            vertices.data(),
            static_cast<UINT>(vertices.size() * sizeof(float)));
        BindOverride(shaderResource);
        m_context->Draw(6, 0);
        RecordDebugRenderDraw(6);
        return;
    }

    if (!m_drawingDebugOverlay && m_batchVertices.size() + 4 > kBatchMaxVertices)
    {
        Flush2DBatch();
    }

    BatchVertex batch[4]{};
    for (int index = 0; index < 4; ++index)
    {
        const float rotatedX = local[index].x * cosine - local[index].y * sine;
        const float rotatedY = local[index].x * sine + local[index].y * cosine;
        Vec2 position{destination.x + rotatedX, destination.y + rotatedY};
        if (m_camera2DActive && !m_drawingDebugOverlay) {
            position = GetWorldToScreen2D(position, m_camera2D);
        }

        batch[index].x = position.x;
        batch[index].y = position.y;
        batch[index].u = uv[index].x;
        batch[index].v = uv[index].y;
        std::memcpy(&batch[index].r, color, sizeof(color));
    }
    BatchQuad(batch, shaderResource);
}

void D3D11CommandContext::BatchTriangle(const BatchVertex v[3])
{
    if (m_drawingDebugOverlay)
    {
        const uint32_t baseVertex = static_cast<uint32_t>(m_overlayVertices.size());
        const UINT indexStart = static_cast<UINT>(m_overlayIndices.size());

        m_overlayVertices.insert(m_overlayVertices.end(), v, v + 3);
        m_overlayIndices.push_back(baseVertex);
        m_overlayIndices.push_back(baseVertex + 1);
        m_overlayIndices.push_back(baseVertex + 2);
        if (!m_overlayDrawItems.empty() && m_overlayDrawItems.back().shaderResource == nullptr)
        {
            m_overlayDrawItems.back().indexCount += 3;
        }
        else
        {
            m_overlayDrawItems.push_back(DrawItem{indexStart, 3, nullptr});
        }
        return;
    }

    const uint32_t baseVertex = static_cast<uint32_t>(m_batchVertices.size());
    const UINT indexStart = static_cast<UINT>(m_batchIndices.size());

    m_batchVertices.insert(m_batchVertices.end(), v, v + 3);
    m_batchIndices.push_back(baseVertex);
    m_batchIndices.push_back(baseVertex + 1);
    m_batchIndices.push_back(baseVertex + 2);
    if (!m_batchDrawItems.empty() && m_batchDrawItems.back().shaderResource == nullptr)
    {
        m_batchDrawItems.back().indexCount += 3;
    }
    else
    {
        m_batchDrawItems.push_back(DrawItem{indexStart, 3, nullptr});
    }
}

void D3D11CommandContext::BatchQuad(const BatchVertex v[4], ID3D11ShaderResourceView *shaderResource)
{
    if (m_drawingDebugOverlay)
    {
        const uint32_t baseVertex = static_cast<uint32_t>(m_overlayVertices.size());
        const UINT indexStart = static_cast<UINT>(m_overlayIndices.size());

        m_overlayVertices.insert(m_overlayVertices.end(), v, v + 4);
        m_overlayIndices.push_back(baseVertex);
        m_overlayIndices.push_back(baseVertex + 1);
        m_overlayIndices.push_back(baseVertex + 2);
        m_overlayIndices.push_back(baseVertex);
        m_overlayIndices.push_back(baseVertex + 2);
        m_overlayIndices.push_back(baseVertex + 3);
        if (!m_overlayDrawItems.empty() && m_overlayDrawItems.back().shaderResource == shaderResource)
        {
            m_overlayDrawItems.back().indexCount += 6;
        }
        else
        {
            m_overlayDrawItems.push_back(DrawItem{indexStart, 6, shaderResource});
        }
        return;
    }

    const uint32_t baseVertex = static_cast<uint32_t>(m_batchVertices.size());
    const UINT indexStart = static_cast<UINT>(m_batchIndices.size());

    m_batchVertices.insert(m_batchVertices.end(), v, v + 4);
    m_batchIndices.push_back(baseVertex);
    m_batchIndices.push_back(baseVertex + 1);
    m_batchIndices.push_back(baseVertex + 2);
    m_batchIndices.push_back(baseVertex);
    m_batchIndices.push_back(baseVertex + 2);
    m_batchIndices.push_back(baseVertex + 3);
    if (!m_batchDrawItems.empty() && m_batchDrawItems.back().shaderResource == shaderResource)
    {
        m_batchDrawItems.back().indexCount += 6;
    }
    else
    {
        m_batchDrawItems.push_back(DrawItem{indexStart, 6, shaderResource});
    }
}

void D3D11CommandContext::Flush2DBatch()
{
    if (!m_context || !m_pipeline || !m_batchVertexBuffer || !m_batchIndexBuffer ||
        m_batchVertices.empty())
    {
        return;
    }

    if (m_activeWidth <= 0 || m_activeHeight <= 0)
    {
        m_batchVertices.clear();
        m_batchIndices.clear();
        m_batchDrawItems.clear();
        return;
    }

    for (BatchVertex &vertex : m_batchVertices)
    {
        vertex.x = vertex.x / m_activeWidth * 2.0f - 1.0f;
        vertex.y = 1.0f - vertex.y / m_activeHeight * 2.0f;
    }

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT result = m_context->Map(m_batchVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                                    &mapped);
    if (FAILED(result))
    {
        m_batchVertices.clear();
        m_batchIndices.clear();
        m_batchDrawItems.clear();
        return;
    }
    std::memcpy(mapped.pData, m_batchVertices.data(), m_batchVertices.size() * sizeof(BatchVertex));
    m_context->Unmap(m_batchVertexBuffer.Get(), 0);

    result = m_context->Map(m_batchIndexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(result))
    {
        m_batchVertices.clear();
        m_batchIndices.clear();
        m_batchDrawItems.clear();
        return;
    }
    std::memcpy(mapped.pData, m_batchIndices.data(), m_batchIndices.size() * sizeof(uint32_t));
    m_context->Unmap(m_batchIndexBuffer.Get(), 0);

    m_pipeline->BindBatch(m_context, m_batchVertexBuffer.Get(), m_batchIndexBuffer.Get());

    ID3D11ShaderResourceView *boundSRV = nullptr;
    for (const DrawItem &item : m_batchDrawItems)
    {
        ID3D11ShaderResourceView *srv = item.shaderResource ? item.shaderResource
                                                            : m_whiteShaderResource.Get();
        if (srv != boundSRV)
        {
            m_context->PSSetShaderResources(0, 1, &srv);
            boundSRV = srv;
        }
        m_context->DrawIndexed(item.indexCount, item.indexStart, 0);
        RecordDebugRenderDraw(item.indexCount);
    }

    m_batchVertices.clear();
    m_batchIndices.clear();
    m_batchDrawItems.clear();
}

void D3D11CommandContext::FlushBatch()
{
    Flush3DBatch();
    Flush2DBatch();
}

void D3D11CommandContext::FlushDebugOverlay()
{
    if (!m_context || !m_pipeline || !m_batchVertexBuffer || !m_batchIndexBuffer ||
        m_overlayVertices.empty())
    {
        m_overlayVertices.clear();
        m_overlayIndices.clear();
        m_overlayDrawItems.clear();
        return;
    }

    const int targetWidth = m_defaultWidth > 0 ? m_defaultWidth : m_activeWidth;
    const int targetHeight = m_defaultHeight > 0 ? m_defaultHeight : m_activeHeight;
    if (targetWidth <= 0 || targetHeight <= 0)
    {
        m_overlayVertices.clear();
        m_overlayIndices.clear();
        m_overlayDrawItems.clear();
        return;
    }

    ID3D11RenderTargetView *renderTarget = m_swapChain ? m_swapChain->RenderTarget() : nullptr;
    if (!renderTarget)
    {
        m_overlayVertices.clear();
        m_overlayIndices.clear();
        m_overlayDrawItems.clear();
        return;
    }
    m_context->OMSetRenderTargets(1, &renderTarget, nullptr);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(targetWidth);
    viewport.Height = static_cast<float>(targetHeight);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    D3D11_RECT scissorRect{ 0, 0, targetWidth, targetHeight };
    m_context->RSSetScissorRects(1, &scissorRect);

    for (BatchVertex &vertex : m_overlayVertices)
    {
        vertex.x = vertex.x / targetWidth * 2.0f - 1.0f;
        vertex.y = 1.0f - vertex.y / targetHeight * 2.0f;
    }

    const size_t vertexCount = std::min<size_t>(m_overlayVertices.size(), kBatchMaxVertices);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT result = m_context->Map(m_batchVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(result))
    {
        m_overlayVertices.clear();
        m_overlayIndices.clear();
        m_overlayDrawItems.clear();
        return;
    }
    std::memcpy(mapped.pData, m_overlayVertices.data(), vertexCount * sizeof(BatchVertex));
    m_context->Unmap(m_batchVertexBuffer.Get(), 0);

    const size_t indexCount = std::min<size_t>(m_overlayIndices.size(), kBatchMaxVertices * 3);
    result = m_context->Map(m_batchIndexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(result))
    {
        m_overlayVertices.clear();
        m_overlayIndices.clear();
        m_overlayDrawItems.clear();
        return;
    }
    std::memcpy(mapped.pData, m_overlayIndices.data(), indexCount * sizeof(uint32_t));
    m_context->Unmap(m_batchIndexBuffer.Get(), 0);

    m_pipeline->BindBatch(m_context, m_batchVertexBuffer.Get(), m_batchIndexBuffer.Get());

    ID3D11ShaderResourceView *boundSRV = nullptr;
    for (const DrawItem &item : m_overlayDrawItems)
    {
        ID3D11ShaderResourceView *srv = item.shaderResource ? item.shaderResource
                                                            : m_whiteShaderResource.Get();
        if (srv != boundSRV)
        {
            m_context->PSSetShaderResources(0, 1, &srv);
            boundSRV = srv;
        }
        m_context->DrawIndexed(item.indexCount, item.indexStart, 0);
        RecordDebugRenderDraw(item.indexCount);
    }

    m_overlayVertices.clear();
    m_overlayIndices.clear();
    m_overlayDrawItems.clear();
}

void D3D11CommandContext::BindOverride(ID3D11ShaderResourceView *drawnResource)
{
    if (!m_context || !m_pipeline)
    {
        return;
    }

    const ShaderOverride &shaderOverride = m_shaderOverride;
    const UINT stride = shaderOverride.strideBytes;
    const UINT offset = 0;
    ID3D11Buffer *vertexBuffer = m_pipeline->VertexBuffer();

    m_context->IASetInputLayout(shaderOverride.inputLayout);
    m_context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->RSSetState(m_pipeline->Rasterizer());
    m_context->VSSetShader(shaderOverride.vertexShader, nullptr, 0);
    m_context->PSSetShader(shaderOverride.pixelShader, nullptr, 0);

    if (shaderOverride.constantBuffer)
    {
        ID3D11Buffer *constantBuffer = shaderOverride.constantBuffer;
        m_context->VSSetConstantBuffers(0, 1, &constantBuffer);
        m_context->PSSetConstantBuffers(0, 1, &constantBuffer);
    }

    ID3D11ShaderResourceView *resources[10] = {};
    for (size_t index = 0; index < 10; ++index)
    {
        resources[index] = shaderOverride.shaderResources[index];
    }
    if (drawnResource)
    {
        resources[0] = drawnResource;
    }
    m_context->PSSetShaderResources(0, 10, resources);

    ID3D11SamplerState *sampler = m_pipeline->Sampler();
    m_context->PSSetSamplers(0, 1, &sampler);

    const float blendFactor[] = {0.0f, 0.0f, 0.0f, 0.0f};
    m_context->OMSetDepthStencilState(m_pipeline->DepthStencilDisabledState(), 0);
    m_context->OMSetBlendState(m_pipeline->Blend(), blendFactor, 0xFFFFFFFF);
}

void D3D11CommandContext::BeginTextureMode(const IRenderTexture& target)
{
    if (!m_context || !m_resources || target.id == 0) {
        return;
    }

    ID3D11RenderTargetView* renderTarget = m_resources->RenderTarget(target.id);
    if (!renderTarget) {
        return;
    }

    FlushBatch();

    ID3D11ShaderResourceView* nullResources[10] = {};
    m_context->PSSetShaderResources(0, 10, nullResources);

    ID3D11DepthStencilView* depthStencil = m_resources->DepthStencil(target.id);
    m_context->OMSetRenderTargets(1, &renderTarget, depthStencil);
    m_activeRenderTarget = renderTarget;
    m_activeDepthStencil = depthStencil;
    m_activeWidth = target.texture.width;
    m_activeHeight = target.texture.height;
    RefreshViewport(target.texture.width, target.texture.height);

    if (depthStencil) {
        m_context->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                         1.0f, 0);
    }
}

void D3D11CommandContext::EndTextureMode(int width, int height)
{
    FlushBatch();
    BeginDrawing();
    const int restoreWidth = width > 0 ? width : m_defaultWidth;
    const int restoreHeight = height > 0 ? height : m_defaultHeight;
    m_activeWidth = restoreWidth;
    m_activeHeight = restoreHeight;
    RefreshViewport(restoreWidth, restoreHeight);
}

void D3D11CommandContext::BeginMode2D(const Camera2D& camera)
{
    m_camera2D = camera;
    m_camera2DActive = true;
}

void D3D11CommandContext::EndMode2D()
{
    m_camera2DActive = false;
}

void D3D11CommandContext::Shutdown()
{
    TraceLog(LogLevel::Trace, "D3D11", "Command context shut down.");

    m_workers.clear();
    m_queued3DDraws.clear();
    m_queued3DVertices.clear();
    m_queued3DConstants.clear();
    m_threadPool = nullptr;
    m_context = nullptr;
    m_swapChain = nullptr;
    m_pipeline = nullptr;
    m_resources = nullptr;
    m_activeRenderTarget = nullptr;
    m_activeDepthStencil = nullptr;
    m_defaultWidth = 0;
    m_defaultHeight = 0;
    m_activeWidth = 0;
    m_activeHeight = 0;
    m_camera2D = {};
    m_camera2DActive = false;
    m_shaderOverride = {};
    m_batchVertexBuffer.Reset();
    m_batchIndexBuffer.Reset();
    m_whiteTexture.Reset();
    m_whiteShaderResource.Reset();
    m_batchVertices.clear();
    m_batchIndices.clear();
    m_batchDrawItems.clear();
    m_drawingDebugOverlay = false;
    m_overlayVertices.clear();
    m_overlayIndices.clear();
    m_overlayDrawItems.clear();
}
} // namespace qci
#endif
