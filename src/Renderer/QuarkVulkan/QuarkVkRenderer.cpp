#include "QuarkVkRenderer.hpp"
#include "../../QuarkInternal.hpp"

#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <shaderc/shaderc.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif
namespace qci
{
    static const std::vector<const char*> DEVICE_EXTENSIONS = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    static float NormalizeColorComponent(std::uint8_t value)
    {
        return static_cast<float>(value) / 255.0f;
    }

    const char* GetVulkanVendorName(uint32_t vendorID)
    {
        switch (vendorID)
        {
        case 0x10DE:
            return "NVIDIA";
        case 0x1002:
            return "AMD";
        case 0x8086:
            return "Intel";
        case 0x13B5:
            return "ARM";
        case 0x5143:
            return "Qualcomm";
        case 0x1010:
            return "ImgTec";
        case 0x106B:
            return "Apple";
        case 0x1414:
            return "Microsoft";
        case 0x10005:
            return "Mesa";
        default:
            return "Unknown";
        }
    }

    const char* GetVulkanDeviceTypeString(VkPhysicalDeviceType type)
    {
        switch (type)
        {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return "Integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return "Discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return "Virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return "CPU (Software)";
        default:
            return "Other";
        }
    }

    std::string FormatVulkanDriverVersion(uint32_t vendorID, uint32_t driverVersion)
    {
        if (vendorID == 0x10DE)
        { // NVIDIA
            return TextFormat("%u.%u.%u.%u",
                              (driverVersion >> 22) & 0x3FF,
                              (driverVersion >> 14) & 0x0FF,
                              (driverVersion >> 6) & 0x0FF,
                              (driverVersion) & 0x03F);
        }
#if defined(_WIN32)
        if (vendorID == 0x8086)
        { // Intel
            return TextFormat("%u.%u", (driverVersion >> 14), (driverVersion & 0x3FFF));
        }
#endif
        return TextFormat("%u.%u.%u",
                          VK_VERSION_MAJOR(driverVersion),
                          VK_VERSION_MINOR(driverVersion),
                          VK_VERSION_PATCH(driverVersion));
    }

    const char* GetPresentModeString(VkPresentModeKHR mode)
    {
        switch (mode)
        {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:
            return "IMMEDIATE (VSync OFF, uncapped)";
        case VK_PRESENT_MODE_MAILBOX_KHR:
            return "MAILBOX (Triple Buffering)";
        case VK_PRESENT_MODE_FIFO_KHR:
            return "FIFO (VSync ON)";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
            return "FIFO_RELAXED (Adaptive VSync)";
        default:
            return "Unknown";
        }
    }

    const char* GetVkFormatString(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_B8G8R8A8_UNORM:
            return "B8G8R8A8_UNORM";
        case VK_FORMAT_B8G8R8A8_SRGB:
            return "B8G8R8A8_SRGB";
        case VK_FORMAT_R8G8B8A8_UNORM:
            return "R8G8B8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_SRGB:
            return "R8G8B8A8_SRGB";
        case VK_FORMAT_D32_SFLOAT:
            return "D32_SFLOAT";
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return "D32_SFLOAT_S8_UINT";
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return "D24_UNORM_S8_UINT";
        default:
            return "VkFormat(Other)";
        }
    }

    QuarkVkRenderer::~QuarkVkRenderer()
    {
        Shutdown();
    }

    void QuarkVkRenderer::Init(SDL_Window* pWindow, int width, int height)
    {
        uint32_t version = 0;
        vkEnumerateInstanceVersion(&version);

        TraceLog(LogLevel::Info, "VULKAN", TextFormat("Initializing Vulkan renderer... (Supported Instance API: %d.%d.%d, Window: %dx%d)", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version), width, height));

#ifdef __APPLE__
        TraceLog(LogLevel::Info, "VULKAN", "Apple platform detected, using MoltenVK...");
#endif

        m_pWindow = pWindow;
        m_Width = width;
        m_Height = height;
        m_FramebufferResized = false;

        TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateInstance");
        CreateInstance();
        TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateSurface");
        CreateSurface();
        TraceLog(LogLevel::Info, "VULKAN", "Init step: PickPhysicalDevice");
        PickPhysicalDevice();
        m_MsaaSamples = GetSampleCountForSamples(m_RequestedMsaaSamples);
        TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateLogicalDevice");
        CreateLogicalDevice();
        m_VkShaderCompiler.Initialize(m_Device);
        TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateSwapChain");
        CreateSwapChain();
        TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateRenderPass");
        CreateRenderPass();
        TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateOffscreenRenderPass");
        m_VkRenderTarget.Initialize(m_Device, m_GpuAllocator, m_SwapChainImageFormat, m_DepthFormat);
        m_VkRenderTarget.CreateRenderPass();
        TraceLog(LogLevel::Info, "VULKAN", "Init step: DescriptorSetManager");
        m_VkDescriptorSetManager.Initialize(m_Device);
        m_VkPipeline.Initialize(m_Device, m_VkDescriptorSetManager.TextureSetLayout(), m_VkDescriptorSetManager.MaterialSetLayout());
        Create3DUniformBuffer();
        m_VkPipeline.CreatePipelines(m_VkRenderPass.Get(), m_VkRenderTarget.RenderPass(), m_MsaaSamples);
        TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateShaderPipelines");
        CreateShaderPipelines();
        CreateFramebuffers();
        m_ThreadPool.Initialize();
        m_VkCommandContext.Initialize(m_Device, FindQueueFamilies(m_PhysicalDevice).m_GraphicsFamily.value(), m_ThreadPool.WorkerCount());
        CreateFrameVertexIndexBuffers();
        m_vBatchVertices.reserve(VK_MAX_VERTICES_PER_FRAME);
        m_vBatchIndices.reserve(VK_MAX_INDICES_PER_FRAME);
        m_vBatchDrawItems.reserve(256);
        m_vFrameVertices.reserve(VK_MAX_VERTICES_PER_FRAME);
        m_vFrameIndices.reserve(VK_MAX_INDICES_PER_FRAME);
        m_vFrameDrawItems.reserve(256);
        m_Main3DBatch.m_vTriangleVertices.reserve(VK_MAX_VERTICES_PER_FRAME / 2);
        m_Main3DBatch.m_vLineVertices.reserve(VK_MAX_VERTICES_PER_FRAME / 2);
        m_vFrameTriangleVertices3D.reserve(VK_MAX_VERTICES_PER_FRAME);
        m_vFrameLineVertices3D.reserve(VK_MAX_VERTICES_PER_FRAME);
        m_VkFrameManager.Initialize(m_Device, m_VkCommandContext.Pool(), VK_MAX_FRAMES_IN_FLIGHT);
        m_VkFrameManager.SetSwapchainImageCount(m_VkSwapChain.GetImageCount());
        m_VkResources.Initialize(m_Device, m_GpuAllocator, m_VkCommandContext.Pool(), m_GraphicsQueue,
                                 [this](VkDescriptorSet& set)
                                 {
                                     return AllocateTextureDescriptorSet(set);
                                 });
        CreateWhiteTexture();

        TraceLog(LogLevel::Info, "VULKAN", "Vulkan renderer initialized successfully.");
    }

    void QuarkVkRenderer::Shutdown()
    {
        if (m_Instance == VK_NULL_HANDLE)
        {
            return;
        }

        TraceLog(LogLevel::Info, "VULKAN", "Shutting down Vulkan renderer...");
        m_ThreadPool.Shutdown();

        if (m_Device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_Device);
        }

        for (auto& [vertices, mesh] : m_GpuMeshes)
        {
            (void)vertices;
            if (mesh.m_VertexBuffer != VK_NULL_HANDLE)
                m_GpuAllocator.DestroyBuffer(mesh.m_VertexBuffer, mesh.m_VertexAllocation);
            if (mesh.m_IndexBuffer != VK_NULL_HANDLE)
                m_GpuAllocator.DestroyBuffer(mesh.m_IndexBuffer, mesh.m_IndexAllocation);
        }
        m_GpuMeshes.clear();
        m_DynamicMeshes.clear();

        {
            std::vector<uint32_t> vIds;
            vIds.reserve(m_RenderTargets.size());
            for (const auto& [id, _] : m_RenderTargets)
            {
                vIds.push_back(id);
            }
            for (uint32_t id : vIds)
            {
                DestroyRenderTargetInternal(id);
            }
        }

        m_VkResources.Shutdown();
        m_TextureCache.clear();
        m_TextureCacheKeys.clear();

        CleanupSwapChain();
        m_VkRenderTarget.Shutdown();
        m_VkFrameManager.Shutdown();
        m_VkShaderCompiler.Shutdown();

        for (auto& frame : m_aFrames)
        {
            if (frame.m_pVertexMapped && frame.m_VertexAllocation != VK_NULL_HANDLE)
            {
                vmaUnmapMemory(m_GpuAllocator.GetAllocator(), frame.m_VertexAllocation);
                frame.m_pVertexMapped = nullptr;
            }
            if (frame.m_VertexBuffer != VK_NULL_HANDLE)
            {
                m_GpuAllocator.DestroyBuffer(frame.m_VertexBuffer, frame.m_VertexAllocation);
                frame.m_VertexBuffer = VK_NULL_HANDLE;
            }
            if (frame.m_VertexAllocation != VK_NULL_HANDLE)
            {
                frame.m_VertexAllocation = VK_NULL_HANDLE;
            }
            if (frame.m_VertexMemory != VK_NULL_HANDLE)
            {
                frame.m_VertexMemory = VK_NULL_HANDLE;
            }
            frame.m_VertexCapacity = 0;
            if (frame.m_pIndexMapped && frame.m_IndexAllocation != VK_NULL_HANDLE)
            {
                vmaUnmapMemory(m_GpuAllocator.GetAllocator(), frame.m_IndexAllocation);
                frame.m_pIndexMapped = nullptr;
            }
            if (frame.m_IndexBuffer != VK_NULL_HANDLE)
            {
                m_GpuAllocator.DestroyBuffer(frame.m_IndexBuffer, frame.m_IndexAllocation);
                frame.m_IndexBuffer = VK_NULL_HANDLE;
            }
            if (frame.m_IndexAllocation != VK_NULL_HANDLE)
            {
                frame.m_IndexAllocation = VK_NULL_HANDLE;
            }
            if (frame.m_IndexMemory != VK_NULL_HANDLE)
            {
                frame.m_IndexMemory = VK_NULL_HANDLE;
            }
            frame.m_IndexCapacity = 0;
            if (frame.m_pVertexMapped3D && frame.m_Vertex3DAllocation != VK_NULL_HANDLE)
            {
                vmaUnmapMemory(m_GpuAllocator.GetAllocator(), frame.m_Vertex3DAllocation);
                frame.m_pVertexMapped3D = nullptr;
            }
            if (frame.m_VertexBuffer3D != VK_NULL_HANDLE)
            {
                m_GpuAllocator.DestroyBuffer(frame.m_VertexBuffer3D, frame.m_Vertex3DAllocation);
                frame.m_VertexBuffer3D = VK_NULL_HANDLE;
            }
            if (frame.m_Vertex3DAllocation != VK_NULL_HANDLE)
            {
                frame.m_Vertex3DAllocation = VK_NULL_HANDLE;
            }
            if (frame.m_VertexMemory3D != VK_NULL_HANDLE)
            {
                frame.m_VertexMemory3D = VK_NULL_HANDLE;
            }
            frame.m_VertexCapacity3D = 0;
        }

        ClearMaterialDescriptorCache();

        m_VkPipeline.Shutdown();
        m_VkDescriptorSetManager.Shutdown(m_Device);

        if (m_3DInstanceBuffer != VK_NULL_HANDLE)
        {
            if (m_p3DInstanceMapped != nullptr)
            {
                vmaUnmapMemory(m_GpuAllocator.GetAllocator(), m_3DInstanceAllocation);
                m_p3DInstanceMapped = nullptr;
            }
            m_GpuAllocator.DestroyBuffer(m_3DInstanceBuffer, m_3DInstanceAllocation);
            m_3DInstanceBuffer = VK_NULL_HANDLE;
            m_3DInstanceAllocation = VK_NULL_HANDLE;
        }
        if (m_3DDummyBuffer != VK_NULL_HANDLE)
        {
            if (m_p3DDummyBaseMapped != nullptr)
            {
                vmaUnmapMemory(m_GpuAllocator.GetAllocator(), m_3DDummyAllocation);
                m_p3DDummyBaseMapped = nullptr;
                m_p3DDummyMapped = nullptr;
            }
            m_GpuAllocator.DestroyBuffer(m_3DDummyBuffer, m_3DDummyAllocation);
            m_3DDummyBuffer = VK_NULL_HANDLE;
        }
        if (m_3DDummyAllocation != VK_NULL_HANDLE)
        {
            m_3DDummyAllocation = VK_NULL_HANDLE;
        }
        if (m_3DDummyMemory != VK_NULL_HANDLE)
        {
            m_3DDummyMemory = VK_NULL_HANDLE;
        }

        if (m_VkCommandContext.IsInitialized())
        {
            m_VkCommandContext.Shutdown();
        }

        m_GpuAllocator.Shutdown();

        if (m_Device != VK_NULL_HANDLE)
        {
            vkDestroyDevice(m_Device, nullptr);
            m_Device = VK_NULL_HANDLE;
        }

        if (m_Surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
            m_Surface = VK_NULL_HANDLE;
        }

        if (m_Instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(m_Instance, nullptr);
            m_Instance = VK_NULL_HANDLE;
        }

        m_pWindow = nullptr;
        m_Width = 0;
        m_Height = 0;
        m_Drawing = false;
        m_CurrentFrame = 0;
        m_ImageIndex = 0;
        m_WhiteTextureId = 0;
        m_NextRenderTargetId = 1;
        m_ActiveRenderTargetId = 0;
        m_GraphicsQueueFamily = UINT32_MAX;
        m_MsaaSamples = VK_SAMPLE_COUNT_1_BIT;
        m_RequestedMsaaSamples = 1;
        m_Main3DBatch.m_vTriangleVertices.clear();
        m_Main3DBatch.m_vLineVertices.clear();
        m_vFrameTriangleVertices3D.clear();
        m_vFrameLineVertices3D.clear();
        m_vFrame3DDrawItems.clear();
        TraceLog(LogLevel::Info, "VULKAN", "Vulkan renderer shut down successfully.");
    }

    void QuarkVkRenderer::BeginDrawing()
    {
        if (!m_Device || m_VkSwapChain.Get() == VK_NULL_HANDLE || m_Drawing)
        {
            return;
        }

        m_ActiveRenderTargetId = 0;
        if (m_LastFrameCounter == 0)
        {
            m_LastFrameCounter = SDL_GetPerformanceCounter();
        }

        m_VkFrameManager.BeginFrame(m_CurrentFrame);
        if (m_p3DDummyBaseMapped != nullptr)
        {
            m_p3DDummyMapped = static_cast<char*>(m_p3DDummyBaseMapped) +
                              static_cast<size_t>(m_CurrentFrame) * VK_3D_UNIFORM_FRAME_STRIDE;
        }

        VkResult result = m_VkFrameManager.AcquireNextImage(m_VkSwapChain.Get(), m_CurrentFrame, m_ImageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            RecreateSwapChain();
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            throw std::runtime_error("Failed to acquire next Vulkan swapchain image.");
        }

        m_Drawing = true;
    }

    void QuarkVkRenderer::EndDrawing()
    {
        if (!m_Drawing || !m_Device || m_VkSwapChain.Get() == VK_NULL_HANDLE)
            return;

        VkCommandBuffer cmd = m_VkFrameManager.GetCommandBuffer(m_CurrentFrame);

        m_VkFrameManager.ResetFrame(m_CurrentFrame);

        BuildCombinedFrameGeometry();

        m_vBatchVertices.clear();
        m_vBatchIndices.clear();
        m_vBatchDrawItems.clear();
        for (auto& [id, rt] : m_RenderTargets)
        {
            (void)id;
            rt.m_vVertices.clear();
            rt.m_vIndices.clear();
            rt.m_vDrawItems.clear();
            rt.m_vTriangleVertices3D.clear();
            rt.m_vTriangleIndices3D.clear();
            rt.m_vLineVertices3D.clear();
            rt.m_vDrawItems3D.clear();
        }
        m_Main3DBatch.m_vTriangleVertices.clear();
        m_Main3DBatch.m_vTriangleIndices.clear();
        m_Main3DBatch.m_vLineVertices.clear();
        m_Main3DBatch.m_vDrawItems.clear();

        if (!UploadFrameGeometry(m_CurrentFrame))
        {
            m_vFrameInstanceTransforms.clear();
            m_Drawing = false;
            return;
        }
        m_vFrameInstanceTransforms.clear();
        if (false)
        {
            m_Drawing = false;
            return;
        }
        if (!RecordCommandBuffer(cmd, m_ImageIndex))
        {
            m_Drawing = false;
            return;
        }

        if (!m_VkFrameManager.Submit(m_CurrentFrame, m_ImageIndex, m_GraphicsQueue))
        {
            throw std::runtime_error("Failed to submit Vulkan command buffer.");
        }

        const std::uint64_t freq = SDL_GetPerformanceFrequency();
        if (m_TargetFps > 0 && !m_Vsync)
        {
            const std::uint64_t targetTicks = freq / static_cast<std::uint64_t>(m_TargetFps);
            while (true)
            {
                const std::uint64_t now = SDL_GetPerformanceCounter();
                const std::uint64_t elapsed = now - m_LastFrameCounter;
                if (elapsed >= targetTicks)
                {
                    break;
                }
                const std::uint64_t remaining = targetTicks - elapsed;
                if (remaining > freq / 500)
                {
                    SDL_Delay(1);
                }
            }
        }

        VkResult presentResult = m_VkFrameManager.Present(m_VkSwapChain.Get(), m_CurrentFrame, m_ImageIndex, m_PresentQueue);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
            presentResult == VK_SUBOPTIMAL_KHR ||
            m_FramebufferResized)
        {
            m_FramebufferResized = false;
            RecreateSwapChain();
        }
        else if (presentResult != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to present Vulkan swapchain image.");
        }

        const std::uint64_t frameEnd = SDL_GetPerformanceCounter();
        m_FrameTime = static_cast<float>(frameEnd - m_LastFrameCounter) / static_cast<float>(freq);
        m_LastFrameCounter = frameEnd;

        m_CurrentFrame = (m_CurrentFrame + 1) % VK_MAX_FRAMES_IN_FLIGHT;
        m_Drawing = false;
        m_FrameGeometryPending = false;
    }

    void QuarkVkRenderer::ClearBackground(Color color)
    {
        if (m_ActiveRenderTargetId != 0)
        {
            auto itRt = m_RenderTargets.find(m_ActiveRenderTargetId);
            if (itRt != m_RenderTargets.end())
            {
                itRt->second.m_ClearColor = color;
                return;
            }
        }
        m_ClearColor = color;
    }

    void QuarkVkRenderer::SetTextureFilterMode(TextureFilter mode)
    {
        g_TextureFilter = mode;
        m_TextureFilterMode = mode;
        m_VkResources.SetTextureSamplingMode(mode, m_TextureWrapMode);
    }

    void QuarkVkRenderer::SetTextureFilter(TextureFilter filter)
    {
        g_TextureFilter = filter;
        m_TextureFilterMode = g_TextureFilter;
        m_VkResources.SetTextureSamplingMode(m_TextureFilterMode, m_TextureWrapMode);
    }

    void QuarkVkRenderer::SetTextureWrap(int wrap)
    {
        m_TextureWrapMode = wrap;
        m_VkResources.SetTextureSamplingMode(m_TextureFilterMode, m_TextureWrapMode);
    }

    void QuarkVkRenderer::BeginScissorMode(int x, int y, int width, int height)
    {
        m_ScissorEnabled = true;
        m_ScissorRect = VkRect2D{{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}};
    }

    void QuarkVkRenderer::EndScissorMode()
    {
        m_ScissorEnabled = false;
        m_ScissorRect = VkRect2D{{0, 0}, {m_SwapChainExtent.width, m_SwapChainExtent.height}};
    }

    void QuarkVkRenderer::SetBlendMode(int mode)
    {
        if (m_Device == VK_NULL_HANDLE)
        {
            return;
        }

        m_VkPipeline.SetBlendMode(mode);
    }

    void QuarkVkRenderer::PushMatrix()
    {
        m_vMatrixStack.push_back(m_CurrentMatrix);
    }

    void QuarkVkRenderer::PopMatrix()
    {
        if (!m_vMatrixStack.empty())
        {
            m_CurrentMatrix = m_vMatrixStack.back();
            m_vMatrixStack.pop_back();
        }
    }

    void QuarkVkRenderer::Translate(const Vec3& t)
    {
        m_CurrentMatrix = m_CurrentMatrix * Mat4::translation(t.x, t.y, t.z);
    }

    void QuarkVkRenderer::Rotate(float angle, const Vec3& axis)
    {
        if (axis.x == 1.f && axis.y == 0.f && axis.z == 0.f)
            m_CurrentMatrix = m_CurrentMatrix * Mat4::rotationX(angle);
        else if (axis.y == 1.f && axis.x == 0.f && axis.z == 0.f)
            m_CurrentMatrix = m_CurrentMatrix * Mat4::rotationY(angle);
        else if (axis.z == 1.f && axis.x == 0.f && axis.y == 0.f)
            m_CurrentMatrix = m_CurrentMatrix * Mat4::rotationZ(angle);
    }

    void QuarkVkRenderer::Scale(const Vec3& s)
    {
        m_CurrentMatrix = m_CurrentMatrix * Mat4::scale(s.x, s.y, s.z);
    }

    void QuarkVkRenderer::MultMatrix(const Mat4& matrix)
    {
        m_CurrentMatrix = m_CurrentMatrix * matrix;
    }

    const float* QuarkVkRenderer::GetMatrixModelview()
    {
        return m_ViewMatrix.m;
    }

    const float* QuarkVkRenderer::GetMatrixProjection()
    {
        return m_ProjectionMatrix.m;
    }

    void QuarkVkRenderer::EnableBackfaceCulling()
    {
        m_VkPipeline.SetBackfaceCulling(true);
    }

    void QuarkVkRenderer::DisableBackfaceCulling()
    {
        m_VkPipeline.SetBackfaceCulling(false);
    }

    void QuarkVkRenderer::RefreshViewport()
    {
        if (m_pWindow != nullptr)
        {
            int width = 0;
            int height = 0;
            SDL_GetWindowSizeInPixels(m_pWindow, &width, &height);
            if (width > 0 && height > 0)
            {
                m_Width = width;
                m_Height = height;
            }
        }
        m_FramebufferResized = true;
    }

    VkDescriptorSet QuarkVkRenderer::GetTextureDescriptorSet(uint32_t textureId) const
    {
        return m_VkResources.DescriptorSet(textureId);
    }

    void QuarkVkRenderer::CreateInstance()
    {
        if (m_Instance != VK_NULL_HANDLE)
            return;

        unsigned int extensionCount = 0;
        const char* const* ppExtensionsData = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
        if (!ppExtensionsData)
        {
            throw std::runtime_error("SDL_Vulkan_GetInstanceExtensions failed.");
        }
        std::vector<const char*> vExtensions(ppExtensionsData, ppExtensionsData + extensionCount);

#ifdef __APPLE__
        vExtensions.push_back(
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

        vExtensions.push_back(
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif

#ifdef _DEBUG
        const char* pValidationLayer = "VK_LAYER_KHRONOS_validation";
        bool enableValidationLayer = false;
        const char* pValidationEnv = std::getenv("QUARK_VK_VALIDATION");
        if (pValidationEnv && pValidationEnv[0] == '1')
        {
            uint32_t layerCount = 0;
            VkResult layerResult = vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
            if (layerResult == VK_SUCCESS && layerCount > 0)
            {
                std::vector<VkLayerProperties> vLayers(layerCount);
                layerResult = vkEnumerateInstanceLayerProperties(&layerCount, vLayers.data());
                if (layerResult == VK_SUCCESS)
                {
                    enableValidationLayer = std::any_of(vLayers.begin(), vLayers.end(),
                                                        [pValidationLayer](const VkLayerProperties& layer)
                                                        {
                                                            return std::strcmp(layer.layerName, pValidationLayer) == 0;
                                                        });
                }
            }
            if (!enableValidationLayer)
            {
                TraceLog(LogLevel::Warn, "VULKAN",
                         "VK_LAYER_KHRONOS_validation is unavailable; continuing without validation.");
            }
        }
#endif

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "QuarkCore Vulkan";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "QuarkCore";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(vExtensions.size());
        createInfo.ppEnabledExtensionNames = vExtensions.data();

#ifdef _DEBUG
        createInfo.enabledLayerCount = enableValidationLayer ? 1u : 0u;
        createInfo.ppEnabledLayerNames = enableValidationLayer ? &pValidationLayer : nullptr;
#else
        createInfo.enabledLayerCount = 0;
#endif

#ifdef __APPLE__
        createInfo.flags |=
            VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

        const VkResult instanceResult = vkCreateInstance(&createInfo, nullptr, &m_Instance);
        if (instanceResult != VK_SUCCESS)
        {
            TraceLog(LogLevel::Error, "VULKAN", TextFormat("vkCreateInstance failed with VkResult %d (SDL error: %s).", static_cast<int>(instanceResult), SDL_GetError()));
            throw std::runtime_error(TextFormat(
                "Failed to create Vulkan instance (VkResult %d).",
                static_cast<int>(instanceResult)));
        }
        TraceLog(LogLevel::Info, "VULKAN", TextFormat("Vulkan %d.%d.%d instance created.", VK_VERSION_MAJOR(appInfo.apiVersion), VK_VERSION_MINOR(appInfo.apiVersion), VK_VERSION_PATCH(appInfo.apiVersion)));

#ifdef __APPLE__
        TraceLog(
            LogLevel::Info,
            "VULKAN",
            "MoltenVK compatibility enabled.");
#endif
    }

    void QuarkVkRenderer::CreateSurface()
    {
        if (m_Surface != VK_NULL_HANDLE)
            return;
        if (!SDL_Vulkan_CreateSurface(m_pWindow, m_Instance, nullptr, &m_Surface))
        {
            throw std::runtime_error("Failed to create Vulkan surface from SDL window.");
        }
        TraceLog(LogLevel::Info, "VULKAN", "Vulkan surface created.");
    }

    void QuarkVkRenderer::PickPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
        if (deviceCount == 0)
        {
            TraceLog(LogLevel::Error, "VULKAN", "Failed to find GPUs with Vulkan support.");
            throw std::runtime_error("Failed to find GPUs with Vulkan support.");
        }
        std::vector<VkPhysicalDevice> vDevices(deviceCount);
        vkEnumeratePhysicalDevices(m_Instance, &deviceCount, vDevices.data());

        TraceLog(LogLevel::Info, "VULKAN", TextFormat("Found %u physical device(s) with Vulkan support:", deviceCount));

        uint64_t bestScore = 0;
        for (uint32_t i = 0; i < deviceCount; ++i)
        {
            VkPhysicalDevice device = vDevices[i];
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);

            VkPhysicalDeviceMemoryProperties memProps;
            vkGetPhysicalDeviceMemoryProperties(device, &memProps);
            uint64_t vramBytes = 0;
            uint64_t hostBytes = 0;
            for (uint32_t h = 0; h < memProps.memoryHeapCount; ++h)
            {
                if (memProps.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                {
                    vramBytes += memProps.memoryHeaps[h].size;
                }
                else
                {
                    hostBytes += memProps.memoryHeaps[h].size;
                }
            }
            double vramGb = static_cast<double>(vramBytes) / (1024.0 * 1024.0 * 1024.0);
            double hostGb = static_cast<double>(hostBytes) / (1024.0 * 1024.0 * 1024.0);

            const char* pDevType = GetVulkanDeviceTypeString(props.deviceType);
            const char* pVendorName = GetVulkanVendorName(props.vendorID);
            std::string driverVer = FormatVulkanDriverVersion(props.vendorID, props.driverVersion);
            bool suitable = IsDeviceSuitable(device);

            TraceLog(LogLevel::Info, "VULKAN", TextFormat("  [%u] GPU: %s (%s)", i, props.deviceName, pDevType));
            TraceLog(LogLevel::Info, "VULKAN", TextFormat("      Vendor: %s (0x%04X), Device ID: 0x%04X, Driver: %s", pVendorName, props.vendorID, props.deviceID, driverVer.c_str()));
            TraceLog(LogLevel::Info, "VULKAN", TextFormat("      Vulkan API: %d.%d.%d, VRAM: %.2f GB, Shared RAM: %.2f GB (Suitable: %s)", VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion), vramGb, hostGb, suitable ? "Yes" : "No"));

            if (suitable)
            {
                uint64_t score = 1;
                if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                {
                    score += 1000000ull;
                }
                else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
                {
                    score += 10000ull;
                }
                score += vramBytes / (64ull * 1024ull * 1024ull);
                if (m_PhysicalDevice == VK_NULL_HANDLE || score > bestScore)
                {
                    m_PhysicalDevice = device;
                    bestScore = score;
                }
            }
        }

        if (m_PhysicalDevice == VK_NULL_HANDLE)
        {
            TraceLog(LogLevel::Error, "VULKAN", "Failed to find a suitable Vulkan physical device.");
            throw std::runtime_error("Failed to find a suitable Vulkan physical device.");
        }

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(m_PhysicalDevice, &props);
        TraceLog(LogLevel::Info, "VULKAN", TextFormat("Selected physical device: %s (%s, Vendor: %s)", props.deviceName, GetVulkanDeviceTypeString(props.deviceType), GetVulkanVendorName(props.vendorID)));

        const VkPhysicalDeviceLimits& lim = props.limits;
        TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Limits: Max 2D Image: %ux%u, Max 3D Image: %u, Max Cube: %u, Max Layers: %u", lim.maxImageDimension2D, lim.maxImageDimension3D, lim.maxImageDimensionCube, lim.maxImageArrayLayers));
        TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Limits: Max UBO Range: %u bytes, Max SSBO Range: %u bytes, Max Push Constants: %u bytes", lim.maxUniformBufferRange, lim.maxStorageBufferRange, lim.maxPushConstantsSize));
        TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Limits: Max Sampler Anisotropy: %.1fx, Max Color Attachments: %u, Max Bound Sets: %u", lim.maxSamplerAnisotropy, lim.maxColorAttachments, lim.maxBoundDescriptorSets));
        TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Limits: Max Vertex Input Attribs: %u, Max Vertex Bindings: %u, Max Memory Allocations: %u", lim.maxVertexInputAttributes, lim.maxVertexInputBindings, lim.maxMemoryAllocationCount));
    }

    void QuarkVkRenderer::CreateLogicalDevice()
    {
        VkQueueFamilyIndices indices = FindQueueFamilies(m_PhysicalDevice);
        m_VkDevice.Initialize(m_Instance, m_PhysicalDevice, m_Surface);
        m_VkDevice.CreateLogicalDevice(indices, DEVICE_EXTENSIONS);
        m_Device = m_VkDevice.Get();
        m_GraphicsQueue = m_VkDevice.GraphicsQueue();
        m_PresentQueue = m_VkDevice.PresentQueue();
        m_GraphicsQueueFamily = m_VkDevice.GraphicsQueueFamily();

        if (!m_GpuAllocator.Initialize(m_Instance, m_PhysicalDevice, m_Device))
        {
            throw std::runtime_error("Failed to initialize Vulkan GPU allocator.");
        }

        TraceLog(LogLevel::Info, "VULKAN", TextFormat("Logical device created successfully (Graphics Queue: #%u, Present Queue: #%u, Extensions: %zu).", indices.m_GraphicsFamily.value(), indices.m_PresentFamily.value(), DEVICE_EXTENSIONS.size()));
    }

    void QuarkVkRenderer::CreateSwapChain()
    {
        VkSwapChainSupportDetails details = QuerySwapChainSupport(m_PhysicalDevice);

        VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(details.m_vFormats);
        VkPresentModeKHR presentMode = ChooseSwapPresentMode(details.m_vPresentModes);
        VkQueueFamilyIndices indices = FindQueueFamilies(m_PhysicalDevice);

        m_VkSwapChain.Initialize(m_Device, m_PhysicalDevice, m_Surface,
                                 m_Width, m_Height, details, indices, surfaceFormat, presentMode);
        if (m_VkFrameManager.IsInitialized())
        {
            m_VkFrameManager.SetSwapchainImageCount(m_VkSwapChain.GetImageCount());
        }

        m_SwapChainImageFormat = m_VkSwapChain.GetImageFormat();
        m_SwapChainExtent = m_VkSwapChain.GetExtent();
        m_Width = static_cast<int>(m_SwapChainExtent.width);
        m_Height = static_cast<int>(m_SwapChainExtent.height);

        TraceLog(LogLevel::Info, "VULKAN", TextFormat("Swapchain created: %ux%u, Images: %u, Format: %s (%d), PresentMode: %s, MSAA: %dx", m_SwapChainExtent.width, m_SwapChainExtent.height, m_VkSwapChain.GetImageCount(), GetVkFormatString(m_SwapChainImageFormat), m_SwapChainImageFormat, GetPresentModeString(presentMode), m_RequestedMsaaSamples));
    }

    void QuarkVkRenderer::CreateRenderPass()
    {
        m_DepthFormat = FindDepthFormat();
        m_VkRenderPass.Initialize(m_Device, m_SwapChainImageFormat, m_DepthFormat, m_MsaaSamples > VK_SAMPLE_COUNT_1_BIT, false);
        TraceLog(LogLevel::Trace, "VULKAN", "Swapchain render pass created.");
    }

    void QuarkVkRenderer::Create3DUniformBuffer()
    {
        {
            VkDeviceMemory dummyMemory = VK_NULL_HANDLE;
            if (!CreateBuffer(VK_3D_UNIFORM_FRAME_STRIDE * VK_MAX_FRAMES_IN_FLIGHT,
                              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              VMA_MEMORY_USAGE_AUTO,
                              m_3DDummyBuffer, m_3DDummyAllocation, dummyMemory,
                              VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                  VMA_ALLOCATION_CREATE_MAPPED_BIT))
            {
                throw std::runtime_error("Failed to create Vulkan 3D uniform buffer.");
            }
        }
        VmaAllocationInfo allocInfo{};
        vmaGetAllocationInfo(m_GpuAllocator.GetAllocator(), m_3DDummyAllocation, &allocInfo);
        m_3DDummyMemory = allocInfo.deviceMemory;
        if (vmaMapMemory(m_GpuAllocator.GetAllocator(), m_3DDummyAllocation, &m_p3DDummyBaseMapped) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to map Vulkan 3D uniform buffer.");
        }
        m_p3DDummyMapped = m_p3DDummyBaseMapped;
        std::memset(m_p3DDummyBaseMapped, 0,
                    static_cast<size_t>(VK_3D_UNIFORM_FRAME_STRIDE * VK_MAX_FRAMES_IN_FLIGHT));
        const Mat4 identity = Mat4::identity();
        for (uint32_t frame = 0; frame < VK_MAX_FRAMES_IN_FLIGHT; ++frame)
        {
            char* pFrameData = static_cast<char*>(m_p3DDummyBaseMapped) +
                              static_cast<size_t>(frame) * VK_3D_UNIFORM_FRAME_STRIDE;
            std::memcpy(pFrameData + 0, identity.m, sizeof(identity.m));
            std::memcpy(pFrameData + 64, identity.m, sizeof(identity.m));
            std::memcpy(pFrameData + 128, identity.m, sizeof(identity.m));
        }
        if (m_3DInstanceBuffer == VK_NULL_HANDLE)
        {
            VkDeviceMemory instanceMemory = VK_NULL_HANDLE;
            const VkDeviceSize totalInstanceBytes =
                static_cast<VkDeviceSize>(VK_MAX_INSTANCES_PER_FRAME) * sizeof(VkInstanceTransformData) * VK_MAX_FRAMES_IN_FLIGHT;
            if (!CreateBuffer(totalInstanceBytes,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              VMA_MEMORY_USAGE_AUTO,
                              m_3DInstanceBuffer, m_3DInstanceAllocation, instanceMemory,
                              VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                  VMA_ALLOCATION_CREATE_MAPPED_BIT))
            {
                throw std::runtime_error("Failed to create Vulkan 3D instance storage buffer.");
            }
            if (vmaMapMemory(m_GpuAllocator.GetAllocator(), m_3DInstanceAllocation, &m_p3DInstanceMapped) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to map Vulkan 3D instance storage buffer.");
            }
            std::memset(m_p3DInstanceMapped, 0, static_cast<size_t>(totalInstanceBytes));
        }
        if (false)
        {
        }
    }

    bool QuarkVkRenderer::Allocate3DDescriptorSet(VkDescriptorSet& outSet, VkDescriptorPool& outPool)
    {
        return m_VkDescriptorSetManager.Allocate3DDescriptorSet(m_Device, outSet, outPool);
    }

    uint64_t QuarkVkRenderer::ComputeMaterialCacheKey(const Material& material) const
    {
        uint64_t hash = 14695981039346656037ull;
        const auto mix = [&](uint64_t value)
        {
            hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        };

        const auto mixColor = [&](const Color& color)
        {
            mix(static_cast<uint64_t>(color.r));
            mix(static_cast<uint64_t>(color.g));
            mix(static_cast<uint64_t>(color.b));
            mix(static_cast<uint64_t>(color.a));
        };

        const auto mixTextureId = [&](uint32_t textureId)
        {
            mix(static_cast<uint64_t>(textureId));
        };

        if (material.maps)
        {
            for (int i = 0; i <= MATERIAL_MAP_BRDF; ++i)
            {
                const MaterialMap& map = material.maps[i];
                mixColor(map.color);
                mix(static_cast<uint64_t>(map.texture.valid ? 1u : 0u));
                mixTextureId(map.texture.id);
                mix(static_cast<uint64_t>(std::lround(map.value * 1000.0f)));
            }
        }
        else
        {
            mix(0ull);
        }

        for (int i = 0; i < 4; ++i)
        {
            mix(static_cast<uint64_t>(std::lround(material.params[i] * 1000.0f)));
        }

        return hash;
    }

    VkDescriptorSet QuarkVkRenderer::CreateMaterialDescriptorSet(const Material& material)
    {
        const uint64_t cacheKey = ComputeMaterialCacheKey(material);
        auto it = m_MaterialCache.find(cacheKey);
        if (it != m_MaterialCache.end() && it->second.m_DescriptorSet != VK_NULL_HANDLE)
        {
            return it->second.m_DescriptorSet;
        }

        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        if (!Allocate3DDescriptorSet(descriptorSet, descriptorPool))
            return VK_NULL_HANDLE;

        const VkTextureData* pWhiteTex = m_VkResources.Get(m_WhiteTextureId);
        if (pWhiteTex == nullptr)
        {
            vkFreeDescriptorSets(m_Device, descriptorPool, 1, &descriptorSet);
            return VK_NULL_HANDLE;
        }

        VkDescriptorImageInfo whiteImage{};
        whiteImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        whiteImage.imageView = pWhiteTex->m_View;
        whiteImage.sampler = pWhiteTex->m_Sampler;

        std::array<VkDescriptorImageInfo, 7> images{};
        images.fill(whiteImage);
        const auto setFallbackImage = [&](size_t index, uint32_t textureId)
        {
            const VkTextureData* pTexture = m_VkResources.Get(textureId);
            if (pTexture != nullptr)
            {
                images[index].imageView = pTexture->m_View;
                images[index].sampler = pTexture->m_Sampler;
            }
        };
        setFallbackImage(1, m_BlackTextureId);
        setFallbackImage(2, m_FlatNormalTextureId);
        setFallbackImage(5, m_BlackTextureId);
        const std::array<int, 6> mapIndices = {
            MATERIAL_MAP_ALBEDO, MATERIAL_MAP_METALNESS, MATERIAL_MAP_NORMAL,
            MATERIAL_MAP_ROUGHNESS, MATERIAL_MAP_OCCLUSION, MATERIAL_MAP_EMISSION};
        for (size_t i = 0; i < mapIndices.size(); ++i)
        {
            if (!material.maps)
                continue;
            const MaterialMap& map = material.maps[mapIndices[i]];
            const VkTextureData* pTexture = m_VkResources.Get(map.texture.id);
            if (map.texture.valid && pTexture != nullptr)
            {
                images[i].imageView = pTexture->m_View;
                images[i].sampler = pTexture->m_Sampler;
            }
        }

        VkDescriptorBufferInfo matrices{};
        matrices.buffer = m_3DDummyBuffer;
        matrices.offset = 0;
        matrices.range = 192;
        VkDescriptorBufferInfo shadowBuffer = matrices;
        shadowBuffer.offset = 512;
        shadowBuffer.range = 512;
        VkDescriptorBufferInfo lightBuffer = matrices;
        lightBuffer.offset = 1024;
        lightBuffer.range = 320;
        std::array<VkDescriptorImageInfo, 4> shadowImages{};
        shadowImages.fill(whiteImage);
        if (material.maps)
        {
            for (uint32_t i = 0; i < 4; ++i)
            {
                const MaterialMap& map = material.maps[MATERIAL_MAP_HEIGHT + i];
                const VkTextureData* pTexture = m_VkResources.Get(map.texture.id);
                if (map.texture.valid && pTexture != nullptr)
                {
                    shadowImages[i].imageView = pTexture->m_View;
                    shadowImages[i].sampler = pTexture->m_Sampler;
                }
            }
        }

        VkDescriptorBufferInfo instanceBufferInfo{};
        instanceBufferInfo.buffer = m_3DInstanceBuffer;
        instanceBufferInfo.offset = 0;
        instanceBufferInfo.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, 12> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1,
                     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &matrices, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &images[0], nullptr, nullptr};
        writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 2, 0, 4,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, shadowImages.data(), nullptr, nullptr};
        writes[3] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 3, 0, 1,
                     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &shadowBuffer, nullptr};
        writes[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 4, 0, 1,
                     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &lightBuffer, nullptr};
        for (uint32_t i = 0; i < 6; ++i)
        {
            writes[5 + i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 5 + i, 0, 1,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &images[i + 1], nullptr, nullptr};
        }
        writes[11] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 11, 0, 1,
                      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &instanceBufferInfo, nullptr};
        vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        VkMaterialCacheEntry entry{};
        entry.m_DescriptorSet = descriptorSet;
        entry.m_DescriptorPool = descriptorPool;
        entry.m_Key = cacheKey;
        if (material.maps)
        {
            for (int i = 0; i <= MATERIAL_MAP_BRDF; ++i)
            {
                entry.m_vTextureIds.push_back(material.maps[i].texture.id);
            }
        }
        m_MaterialCache[cacheKey] = entry;
        return descriptorSet;
    }

    void QuarkVkRenderer::ClearMaterialDescriptorCache()
    {
        for (auto& [key, entry] : m_MaterialCache)
        {
            if (entry.m_DescriptorSet != VK_NULL_HANDLE && m_Device != VK_NULL_HANDLE)
            {
                vkFreeDescriptorSets(m_Device, entry.m_DescriptorPool, 1, &entry.m_DescriptorSet);
            }
        }
        m_MaterialCache.clear();
    }

    void QuarkVkRenderer::ReplaceMaterialTextureReferences(uint32_t textureId)
    {
        if (textureId == 0 || m_Device == VK_NULL_HANDLE)
            return;

        const std::array<uint32_t, MATERIAL_MAP_BRDF + 1> fallbackTextureIds = {
            m_WhiteTextureId, m_BlackTextureId, m_FlatNormalTextureId,
            m_WhiteTextureId, m_WhiteTextureId, m_BlackTextureId,
            m_WhiteTextureId, m_WhiteTextureId, m_WhiteTextureId,
            m_WhiteTextureId, m_WhiteTextureId};
        const std::array<uint32_t, MATERIAL_MAP_BRDF + 1> descriptorBindings = {
            1, 5, 6, 7, 8, 9, 2, 2, 2, 2, 10};

        for (auto& [key, entry] : m_MaterialCache)
        {
            (void)key;
            if (entry.m_DescriptorSet == VK_NULL_HANDLE)
                continue;

            std::array<VkDescriptorImageInfo, MATERIAL_MAP_BRDF + 1> imageInfos{};
            std::array<VkWriteDescriptorSet, MATERIAL_MAP_BRDF + 1> writes{};
            uint32_t writeCount = 0;
            const size_t mapCount = std::min(entry.m_vTextureIds.size(), fallbackTextureIds.size());
            for (size_t mapIndex = 0; mapIndex < mapCount; ++mapIndex)
            {
                if (entry.m_vTextureIds[mapIndex] != textureId)
                    continue;

                const VkTextureData* pFallback = m_VkResources.Get(fallbackTextureIds[mapIndex]);
                if (pFallback == nullptr)
                    continue;
                VkDescriptorImageInfo& image = imageInfos[writeCount];
                image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                image.imageView = pFallback->m_View;
                image.sampler = pFallback->m_Sampler;

                VkWriteDescriptorSet& write = writes[writeCount++];
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = entry.m_DescriptorSet;
                write.dstBinding = descriptorBindings[mapIndex];
                write.dstArrayElement = mapIndex >= MATERIAL_MAP_HEIGHT && mapIndex < MATERIAL_MAP_BRDF
                                            ? static_cast<uint32_t>(mapIndex - MATERIAL_MAP_HEIGHT)
                                            : 0u;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.pImageInfo = &image;
                entry.m_vTextureIds[mapIndex] = fallbackTextureIds[mapIndex];
            }
            if (writeCount > 0)
            {
                vkUpdateDescriptorSets(m_Device, writeCount, writes.data(), 0, nullptr);
            }
        }
    }

    bool QuarkVkRenderer::AllocateTextureDescriptorSet(VkDescriptorSet& outSet)
    {
        return m_VkDescriptorSetManager.AllocateTextureDescriptorSet(m_Device, outSet);
    }

    void QuarkVkRenderer::CreateFramebuffers()
    {
        m_VkSwapChain.CreateAttachmentResources(m_Device, m_GpuAllocator, m_DepthFormat, m_MsaaSamples);

        m_VkFramebufferManager.Initialize(m_Device);
        m_VkFramebufferManager.CreateSwapChainFramebuffers(m_Device,
                                                           m_VkRenderPass.Get(),
                                                           m_VkSwapChain.ImageViews(),
                                                           m_VkSwapChain.GetExtent(),
                                                           m_VkSwapChain.MsaaColorImageView(),
                                                           m_MsaaSamples,
                                                           m_VkSwapChain.DepthImageViews());
        m_vSwapChainFramebuffers = m_VkFramebufferManager.Framebuffers();
        TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Created %zu framebuffers.", m_vSwapChainFramebuffers.size()));
    }

    bool QuarkVkRenderer::RecreateRenderTargetFramebuffers()
    {
        for (const auto& [id, rt] : m_RenderTargets)
        {
            const VkTextureData* pTex = m_VkResources.Get(rt.m_TextureId);
            if (pTex == nullptr)
                continue;

            if (!m_VkRenderTarget.CreateTarget(id, pTex->m_View, rt.m_Width, rt.m_Height))
            {
                return false;
            }
        }
        return true;
    }

    void QuarkVkRenderer::CreateFrameVertexIndexBuffers()
    {
        const VkDeviceSize vertexBufSize = sizeof(VkBatchVertex) * VK_MAX_VERTICES_PER_FRAME;
        const VkDeviceSize indexBufSize = sizeof(uint32_t) * VK_MAX_INDICES_PER_FRAME;
        const VkDeviceSize vertexBufSize3D = sizeof(Vk3DVertex) * VK_MAX_VERTICES_PER_FRAME;

        for (auto& frame : m_aFrames)
        {
            {
                VkDeviceMemory dummyMemory = VK_NULL_HANDLE;
                if (!CreateBuffer(vertexBufSize,
                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  VMA_MEMORY_USAGE_AUTO,
                                  frame.m_VertexBuffer, frame.m_VertexAllocation, dummyMemory,
                                  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                      VMA_ALLOCATION_CREATE_MAPPED_BIT))
                {
                    throw std::runtime_error("Failed to create per-frame Vulkan vertex buffer.");
                }
            }
            VmaAllocationInfo vertexInfo{};
            vmaGetAllocationInfo(m_GpuAllocator.GetAllocator(), frame.m_VertexAllocation, &vertexInfo);
            frame.m_VertexMemory = vertexInfo.deviceMemory;
            if (vmaMapMemory(m_GpuAllocator.GetAllocator(), frame.m_VertexAllocation, &frame.m_pVertexMapped) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to map per-frame Vulkan vertex buffer.");
            }
            frame.m_VertexCapacity = vertexBufSize;

            {
                VkDeviceMemory dummyMemory = VK_NULL_HANDLE;
                if (!CreateBuffer(indexBufSize,
                                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  VMA_MEMORY_USAGE_AUTO,
                                  frame.m_IndexBuffer, frame.m_IndexAllocation, dummyMemory,
                                  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                      VMA_ALLOCATION_CREATE_MAPPED_BIT))
                {
                    throw std::runtime_error("Failed to create per-frame Vulkan index buffer.");
                }
            }
            VmaAllocationInfo indexInfo{};
            vmaGetAllocationInfo(m_GpuAllocator.GetAllocator(), frame.m_IndexAllocation, &indexInfo);
            frame.m_IndexMemory = indexInfo.deviceMemory;
            if (vmaMapMemory(m_GpuAllocator.GetAllocator(), frame.m_IndexAllocation, &frame.m_pIndexMapped) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to map per-frame Vulkan index buffer.");
            }
            frame.m_IndexCapacity = indexBufSize;

            {
                VkDeviceMemory dummyMemory = VK_NULL_HANDLE;
                if (!CreateBuffer(vertexBufSize3D,
                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  VMA_MEMORY_USAGE_AUTO,
                                  frame.m_VertexBuffer3D, frame.m_Vertex3DAllocation, dummyMemory,
                                  VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                      VMA_ALLOCATION_CREATE_MAPPED_BIT))
                {
                    throw std::runtime_error("Failed to create per-frame Vulkan 3D vertex buffer.");
                }
            }
            VmaAllocationInfo vertex3DInfo{};
            vmaGetAllocationInfo(m_GpuAllocator.GetAllocator(), frame.m_Vertex3DAllocation, &vertex3DInfo);
            frame.m_VertexMemory3D = vertex3DInfo.deviceMemory;
            if (vmaMapMemory(m_GpuAllocator.GetAllocator(), frame.m_Vertex3DAllocation, &frame.m_pVertexMapped3D) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to map per-frame Vulkan 3D vertex buffer.");
            }
            frame.m_VertexCapacity3D = vertexBufSize3D;
        }
        TraceLog(LogLevel::Trace, "VULKAN", "Per-frame vertex/index buffers created.");
    }

    void QuarkVkRenderer::CreateWhiteTexture()
    {
        const unsigned char white[4] = {255, 255, 255, 255};
        m_WhiteTextureId = m_VkResources.CreateTextureFromRGBA(white, 1, 1);
        if (m_WhiteTextureId == 0)
        {
            throw std::runtime_error("Failed to create Vulkan white fallback texture.");
        }
        const unsigned char black[4] = {0, 0, 0, 255};
        m_BlackTextureId = m_VkResources.CreateTextureFromRGBA(black, 1, 1);
        if (m_BlackTextureId == 0)
        {
            throw std::runtime_error("Failed to create Vulkan black fallback texture.");
        }
        const unsigned char flatNormal[4] = {128, 128, 255, 255};
        m_FlatNormalTextureId = m_VkResources.CreateTextureFromRGBA(flatNormal, 1, 1);
        if (m_FlatNormalTextureId == 0)
        {
            throw std::runtime_error("Failed to create Vulkan normal fallback texture.");
        }
        Material whiteMaterial{};
        m_White3DDescriptorSet = CreateMaterialDescriptorSet(whiteMaterial);
        if (m_White3DDescriptorSet == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Failed to create Vulkan white 3D descriptor set.");
        }
        TraceLog(LogLevel::Trace, "VULKAN", "White fallback texture created.");
    }

    void QuarkVkRenderer::CreateShaderPipelines()
    {
        m_VkShaderCompiler.FreePipelines();
        for (auto& [id, program] : m_VkShaderCompiler.Programs())
        {
            (void)id;
            if (!program.m_Supports3D)
            {
                program.m_Pipeline = m_VkPipeline.Create2DPipeline(m_VkRenderPass.Get(),
                                                                 program.m_VertexModule,
                                                                 program.m_FragmentModule);
            }
            else
            {
                program.m_Pipeline3D = m_VkPipeline.Create3DPipeline(m_VkRenderPass.Get(),
                                                                   VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                                   program.m_VertexModule,
                                                                   program.m_FragmentModule,
                                                                   program.m_SupportsGpuMeshTransform,
                                                                   false);
                program.m_Pipeline3DCulled = m_VkPipeline.Create3DPipeline(m_VkRenderPass.Get(),
                                                                         VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                                         program.m_VertexModule,
                                                                         program.m_FragmentModule,
                                                                         program.m_SupportsGpuMeshTransform,
                                                                         true);
                if (m_VkRenderTarget.RenderPass() != VK_NULL_HANDLE)
                {
                    program.m_Pipeline3DOffscreen = m_VkPipeline.Create3DPipeline(m_VkRenderTarget.RenderPass(),
                                                                                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                                                program.m_VertexModule,
                                                                                program.m_FragmentModule,
                                                                                program.m_SupportsGpuMeshTransform,
                                                                                false);
                    program.m_Pipeline3DOffscreenCulled = m_VkPipeline.Create3DPipeline(m_VkRenderTarget.RenderPass(),
                                                                                      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                                                      program.m_VertexModule,
                                                                                      program.m_FragmentModule,
                                                                                      program.m_SupportsGpuMeshTransform,
                                                                                      true);
                }
            }
        }
    }
    void QuarkVkRenderer::RecreateSwapChain()
    {
        TraceLog(LogLevel::Info, "VULKAN", "Recreating swapchain...");
        vkDeviceWaitIdle(m_Device);

        CleanupSwapChain();

        CreateSwapChain();
        CreateRenderPass();
        m_VkRenderTarget.Initialize(m_Device, m_GpuAllocator, m_SwapChainImageFormat, m_DepthFormat);
        m_VkRenderTarget.CreateRenderPass();
        m_VkPipeline.CreatePipelines(m_VkRenderPass.Get(), m_VkRenderTarget.RenderPass(), m_MsaaSamples);
        CreateShaderPipelines();
        CreateFramebuffers();
        RecreateRenderTargetFramebuffers();
    }

    void QuarkVkRenderer::CleanupSwapChain()
    {
        TraceLog(LogLevel::Trace, "VULKAN", "Cleaning up swapchain resources...");

        m_VkRenderTarget.DestroyFramebuffers();

        m_VkFramebufferManager.Shutdown(m_Device);
        m_vSwapChainFramebuffers.clear();

        m_VkPipeline.DestroyPipelines();
        m_VkShaderCompiler.FreePipelines();
        m_VkRenderTarget.DestroyRenderPass();
        m_VkRenderPass.Shutdown();

        m_VkSwapChain.Shutdown(m_Device);
    }

    VkQueueFamilyIndices QuarkVkRenderer::FindQueueFamilies(VkPhysicalDevice device) const
    {
        VkQueueFamilyIndices indices;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> vQueueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, vQueueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i)
        {
            if (vQueueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                indices.m_GraphicsFamily = i;
            }
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport);
            if (presentSupport)
            {
                indices.m_PresentFamily = i;
            }
            if (indices.isComplete())
                break;
        }
        return indices;
    }

    VkSwapChainSupportDetails QuarkVkRenderer::QuerySwapChainSupport(VkPhysicalDevice device) const
    {
        VkSwapChainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_Surface, &details.m_Capabilities);

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);
        if (formatCount != 0)
        {
            details.m_vFormats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, details.m_vFormats.data());
        }

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, nullptr);
        if (presentModeCount != 0)
        {
            details.m_vPresentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, details.m_vPresentModes.data());
        }
        return details;
    }

    bool QuarkVkRenderer::IsDeviceSuitable(VkPhysicalDevice device) const
    {
        VkQueueFamilyIndices indices = FindQueueFamilies(device);
        if (!indices.isComplete())
            return false;

        VkSwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device);
        return !swapChainSupport.m_vFormats.empty() && !swapChainSupport.m_vPresentModes.empty();
    }

    VkSurfaceFormatKHR QuarkVkRenderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& vFormats) const
    {
        for (const auto& fmt : vFormats)
        {
            if ((fmt.format == VK_FORMAT_B8G8R8A8_SRGB || fmt.format == VK_FORMAT_B8G8R8A8_UNORM) &&
                fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return fmt;
            }
        }
        return vFormats.empty() ? VkSurfaceFormatKHR{} : vFormats[0];
    }

    VkPresentModeKHR QuarkVkRenderer::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& vModes) const
    {
        const bool vsync = m_VsyncExplicitlySet ? m_Vsync : (m_TargetFps != 0);
        if (!vsync)
        {
            for (const auto& mode : vModes)
            {
                if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
                    return mode;
            }
            for (const auto& mode : vModes)
            {
                if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
                    return mode;
            }
            return VK_PRESENT_MODE_FIFO_KHR;
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    void QuarkVkRenderer::SetTargetFPS(int fps)
    {
        m_TargetFps = fps;
        if (!m_VsyncExplicitlySet && m_VkSwapChain.Get() != VK_NULL_HANDLE)
        {
            RecreateSwapChain();
        }
    }

    bool QuarkVkRenderer::SetVSync(bool enabled)
    {
        if (m_Vsync == enabled && m_VsyncExplicitlySet)
        {
            return true;
        }
        m_Vsync = enabled;
        m_VsyncExplicitlySet = true;
        if (m_VkSwapChain.Get() != VK_NULL_HANDLE)
        {
            RecreateSwapChain();
        }
        return true;
    }

    VkExtent2D QuarkVkRenderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps) const
    {
        if (caps.currentExtent.width != UINT32_MAX)
            return caps.currentExtent;

        VkExtent2D actual = {static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height)};
        actual.width = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, actual.width));
        actual.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, actual.height));
        return actual;
    }

    uint32_t QuarkVkRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i)
        {
            if ((typeFilter & (1u << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & props) == props)
            {
                return i;
            }
        }
        throw std::runtime_error("Failed to find suitable Vulkan memory type.");
    }

    VkFormat QuarkVkRenderer::FindSupportedFormat(const std::vector<VkFormat>& vCandidates,
                                                  VkImageTiling tiling,
                                                  VkFormatFeatureFlags features) const
    {
        for (VkFormat format : vCandidates)
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR &&
                (props.linearTilingFeatures & features) == features)
            {
                return format;
            }
            if (tiling == VK_IMAGE_TILING_OPTIMAL &&
                (props.optimalTilingFeatures & features) == features)
            {
                return format;
            }
        }

        throw std::runtime_error("Failed to find supported Vulkan format.");
    }

    VkFormat QuarkVkRenderer::FindDepthFormat() const
    {
        return FindSupportedFormat(
            {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    }

    bool QuarkVkRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                       VkMemoryPropertyFlags props,
                                       VkBuffer& outBuffer, VkDeviceMemory& outMemory)
    {
        VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO;
        VmaAllocationCreateFlags flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                         VMA_ALLOCATION_CREATE_MAPPED_BIT;

        if ((props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
            flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        }

        VmaAllocation allocation = VK_NULL_HANDLE;
        if (!CreateBuffer(size, usage, memoryUsage, outBuffer, allocation, outMemory, flags))
        {
            return false;
        }
        return true;
    }

    bool QuarkVkRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                       VmaMemoryUsage memoryUsage,
                                       VkBuffer& outBuffer, VmaAllocation& outAllocation,
                                       VkDeviceMemory& outMemory,
                                       VmaAllocationCreateFlags flags)
    {
        outBuffer = VK_NULL_HANDLE;
        outAllocation = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;

        if (!m_GpuAllocator.CreateBuffer(size, usage, memoryUsage, outBuffer, outAllocation, flags))
        {
            return false;
        }

        VmaAllocationInfo allocationInfo{};
        vmaGetAllocationInfo(m_GpuAllocator.GetAllocator(), outAllocation, &allocationInfo);
        outMemory = allocationInfo.deviceMemory;
        return true;
    }

    bool QuarkVkRenderer::EnsureMappedBufferCapacity(VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped,
                                                     VkDeviceSize& capacity, VkDeviceSize required,
                                                     VkBufferUsageFlags usage)
    {
        VmaAllocation allocation = VK_NULL_HANDLE;
        return EnsureMappedBufferCapacity(buffer, memory, allocation, mapped, capacity, required, usage);
    }

    bool QuarkVkRenderer::EnsureMappedBufferCapacity(VkBuffer& buffer, VkDeviceMemory& memory, VmaAllocation& allocation, void*& mapped,
                                                     VkDeviceSize& capacity, VkDeviceSize required,
                                                     VkBufferUsageFlags usage)
    {
        if (required <= capacity)
        {
            return true;
        }

        VkDeviceSize newCapacity = capacity > 0 ? capacity : 4096;
        while (newCapacity < required)
        {
            newCapacity *= 2;
        }

        VkBuffer newBuffer = VK_NULL_HANDLE;
        VmaAllocation newAllocation = VK_NULL_HANDLE;
        VkDeviceMemory newMemory = VK_NULL_HANDLE;
        if (!CreateBuffer(newCapacity,
                          usage,
                          VMA_MEMORY_USAGE_AUTO,
                          newBuffer,
                          newAllocation,
                          newMemory,
                          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                              VMA_ALLOCATION_CREATE_MAPPED_BIT))
        {
            return false;
        }

        void* pNewMapped = nullptr;
        if (vmaMapMemory(m_GpuAllocator.GetAllocator(), newAllocation, &pNewMapped) != VK_SUCCESS)
        {
            m_GpuAllocator.DestroyBuffer(newBuffer, newAllocation);
            return false;
        }

        vkDeviceWaitIdle(m_Device);
        if (mapped != nullptr && allocation != VK_NULL_HANDLE && memory != VK_NULL_HANDLE)
        {
            vmaUnmapMemory(m_GpuAllocator.GetAllocator(), allocation);
        }
        if (buffer != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE)
        {
            m_GpuAllocator.DestroyBuffer(buffer, allocation);
        }

        buffer = newBuffer;
        allocation = newAllocation;
        memory = newMemory;
        mapped = pNewMapped;
        capacity = newCapacity;
        return true;
    }

    VkSampleCountFlagBits QuarkVkRenderer::GetSampleCountForSamples(int samples) const
    {
        if (samples <= 1 || m_PhysicalDevice == VK_NULL_HANDLE)
            return VK_SAMPLE_COUNT_1_BIT;
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(m_PhysicalDevice, &props);
        VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts &
                                    props.limits.framebufferDepthSampleCounts;

        VkSampleCountFlagBits requested = VK_SAMPLE_COUNT_1_BIT;
        if (samples >= 8)
            requested = VK_SAMPLE_COUNT_8_BIT;
        else if (samples >= 4)
            requested = VK_SAMPLE_COUNT_4_BIT;
        else if (samples >= 2)
            requested = VK_SAMPLE_COUNT_2_BIT;

        if (counts & requested)
        {
            return requested;
        }
        if (samples >= 8 && (counts & VK_SAMPLE_COUNT_4_BIT))
            return VK_SAMPLE_COUNT_4_BIT;
        if (samples >= 4 && (counts & VK_SAMPLE_COUNT_2_BIT))
            return VK_SAMPLE_COUNT_2_BIT;
        return VK_SAMPLE_COUNT_1_BIT;
    }

    void QuarkVkRenderer::SetMSAASamples(int samples)
    {
        m_RequestedMsaaSamples = (samples == 2 || samples == 4 || samples == 8) ? samples : 1;
        if (m_PhysicalDevice != VK_NULL_HANDLE)
        {
            VkSampleCountFlagBits newSamples = GetSampleCountForSamples(m_RequestedMsaaSamples);
            if (newSamples != m_MsaaSamples)
            {
                m_MsaaSamples = newSamples;
                if (m_VkSwapChain.Get() != VK_NULL_HANDLE)
                {
                    RecreateSwapChain();
                }
            }
        }
    }

    IRenderTexture QuarkVkRenderer::CreateRenderTargetInternal(int width, int height)
    {
        if (width <= 0 || height <= 0 ||
            m_Device == VK_NULL_HANDLE ||
            m_VkRenderTarget.RenderPass() == VK_NULL_HANDLE)
        {
            return IRenderTexture{};
        }

        VkTextureData tex{};
        tex.m_Width = static_cast<uint32_t>(width);
        tex.m_Height = static_cast<uint32_t>(height);
        tex.m_IsRenderTarget = true;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {tex.m_Width, tex.m_Height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_SwapChainImageFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        tex.m_Allocation = VK_NULL_HANDLE;
        if (!m_GpuAllocator.CreateImage(imageInfo,
                                        VMA_MEMORY_USAGE_AUTO,
                                        tex.m_Image,
                                        tex.m_Allocation,
                                        VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT))
        {
            return IRenderTexture{};
        }

        VmaAllocationInfo rtAllocInfo{};
        vmaGetAllocationInfo(m_GpuAllocator.GetAllocator(), tex.m_Allocation, &rtAllocInfo);
        tex.m_Memory = rtAllocInfo.deviceMemory;

        if (!m_VkResources.TransitionImageLayout(tex.m_Image, imageInfo.format,
                                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
        {
            m_GpuAllocator.DestroyImage(tex.m_Image, tex.m_Allocation);
            tex.m_Image = VK_NULL_HANDLE;
            tex.m_Allocation = VK_NULL_HANDLE;
            tex.m_Memory = VK_NULL_HANDLE;
            return IRenderTexture{};
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = tex.m_Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = imageInfo.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_Device, &viewInfo, nullptr, &tex.m_View) != VK_SUCCESS)
        {
            m_GpuAllocator.DestroyImage(tex.m_Image, tex.m_Allocation);
            tex.m_Image = VK_NULL_HANDLE;
            tex.m_Allocation = VK_NULL_HANDLE;
            tex.m_Memory = VK_NULL_HANDLE;
            return IRenderTexture{};
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 1.0f;
        if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &tex.m_Sampler) != VK_SUCCESS)
        {
            vkDestroyImageView(m_Device, tex.m_View, nullptr);
            m_GpuAllocator.DestroyImage(tex.m_Image, tex.m_Allocation);
            tex.m_Image = VK_NULL_HANDLE;
            tex.m_Allocation = VK_NULL_HANDLE;
            tex.m_Memory = VK_NULL_HANDLE;
            return IRenderTexture{};
        }

        if (!AllocateTextureDescriptorSet(tex.m_DescriptorSet))
        {
            vkDestroySampler(m_Device, tex.m_Sampler, nullptr);
            vkDestroyImageView(m_Device, tex.m_View, nullptr);
            m_GpuAllocator.DestroyImage(tex.m_Image, tex.m_Allocation);
            tex.m_Image = VK_NULL_HANDLE;
            tex.m_Allocation = VK_NULL_HANDLE;
            tex.m_Memory = VK_NULL_HANDLE;
            return IRenderTexture{};
        }

        VkDescriptorImageInfo imageDescriptor{};
        imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageDescriptor.imageView = tex.m_View;
        imageDescriptor.sampler = tex.m_Sampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = tex.m_DescriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageDescriptor;
        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);

        const uint32_t textureId = m_VkResources.Import(tex);

        const uint32_t rtId = m_NextRenderTargetId++;
        if (!m_VkRenderTarget.CreateTarget(rtId, tex.m_View, tex.m_Width, tex.m_Height))
        {
            m_VkResources.DestroyTexture(textureId);
            return IRenderTexture{};
        }

        VkRenderTargetData rt{};
        rt.m_TextureId = textureId;
        rt.m_Width = tex.m_Width;
        rt.m_Height = tex.m_Height;
        rt.m_ImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        m_RenderTargets[rtId] = rt;

        TraceLog(LogLevel::Info, "RENDER_TARGET", TextFormat("[Vulkan] Render target created: %ux%u (Target ID: %u, Color Tex ID: %u, Depth Buffer: yes)", tex.m_Width, tex.m_Height, rtId, textureId));
        return IRenderTexture{rtId, {}, 0};
    }

    void QuarkVkRenderer::DestroyRenderTargetInternal(uint32_t renderTargetId)
    {
        auto it = m_RenderTargets.find(renderTargetId);
        if (it == m_RenderTargets.end())
            return;

        if (m_Device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_Device);
        }

        TraceLog(LogLevel::Info, "RENDER_TARGET", TextFormat("[Vulkan] Render target destroyed (Target ID: %u, %ux%u)", renderTargetId, it->second.m_Width, it->second.m_Height));

        const uint32_t textureId = it->second.m_TextureId;
        ReplaceMaterialTextureReferences(textureId);
        m_VkRenderTarget.DestroyTarget(renderTargetId);
        m_RenderTargets.erase(it);

        if (m_ActiveRenderTargetId == renderTargetId)
        {
            m_ActiveRenderTargetId = 0;
        }
        m_VkResources.DestroyTexture(textureId);
    }

    bool QuarkVkRenderer::ReadBinaryFile(const char* pPath, std::vector<char>& vOutData) const
    {
        std::ifstream file(pPath, std::ios::ate | std::ios::binary);
        if (!file.is_open())
            return false;
        const std::streamsize fileSize = file.tellg();
        vOutData.resize(static_cast<size_t>(fileSize));
        file.seekg(0);
        file.read(vOutData.data(), fileSize);
        return true;
    }

    void QuarkVkRenderer::BuildCombinedFrameGeometry()
    {
        m_vFrameVertices.clear();
        m_vFrameIndices.clear();
        m_vFrameDrawItems.clear();
        m_vFramePasses.clear();
        m_vFrameTriangleVertices3D.clear();
        m_vFrameLineVertices3D.clear();
        m_Frame3DShaderProgramId = 0;

        for (auto& [id, rt] : m_RenderTargets)
        {
            const bool has2D = !rt.m_vDrawItems.empty();
            const bool has3D = !rt.m_vTriangleVertices3D.empty() || !rt.m_vTriangleIndices3D.empty() ||
                               !rt.m_vLineVertices3D.empty() || !rt.m_vDrawItems3D.empty();
            if (!has2D && !has3D)
                continue;

            const uint32_t baseVertex = static_cast<uint32_t>(m_vFrameVertices.size());
            const uint32_t baseIndex = static_cast<uint32_t>(m_vFrameIndices.size());
            const uint32_t firstDraw = static_cast<uint32_t>(m_vFrameDrawItems.size());
            const uint32_t triFirst = static_cast<uint32_t>(m_vFrameTriangleVertices3D.size());
            const uint32_t lineFirst = static_cast<uint32_t>(m_vFrameLineVertices3D.size());
            const uint32_t first3DDrawItem = static_cast<uint32_t>(m_vFrame3DDrawItems.size());

            m_vFrameVertices.insert(m_vFrameVertices.end(), rt.m_vVertices.begin(), rt.m_vVertices.end());
            for (uint32_t idx : rt.m_vIndices)
            {
                m_vFrameIndices.push_back(baseVertex + idx);
            }
            for (VkDrawItem item : rt.m_vDrawItems)
            {
                item.m_FirstIndex += baseIndex;
                m_vFrameDrawItems.push_back(item);
            }
            const uint32_t base3DIndex = static_cast<uint32_t>(m_vFrameIndices.size());
            m_vFrameIndices.insert(m_vFrameIndices.end(),
                                   rt.m_vTriangleIndices3D.begin(), rt.m_vTriangleIndices3D.end());

            m_vFrameTriangleVertices3D.insert(m_vFrameTriangleVertices3D.end(),
                                              rt.m_vTriangleVertices3D.begin(), rt.m_vTriangleVertices3D.end());
            m_vFrameLineVertices3D.insert(m_vFrameLineVertices3D.end(),
                                          rt.m_vLineVertices3D.begin(), rt.m_vLineVertices3D.end());
            for (Vk3DDrawItem item : rt.m_vDrawItems3D)
            {
                item.m_FirstVertex += triFirst;
                if (!item.m_GpuMesh)
                    item.m_FirstIndex += base3DIndex;
                m_vFrame3DDrawItems.push_back(item);
            }

            m_vFramePasses.push_back(VkFramePass{
                id,
                firstDraw,
                static_cast<uint32_t>(m_vFrameDrawItems.size() - firstDraw),
                rt.m_Width,
                rt.m_Height,
                triFirst,
                static_cast<uint32_t>(rt.m_vTriangleVertices3D.size()),
                lineFirst,
                static_cast<uint32_t>(rt.m_vLineVertices3D.size()),
                first3DDrawItem,
                static_cast<uint32_t>(m_vFrame3DDrawItems.size() - first3DDrawItem)});
        }

        {
            const uint32_t baseVertex = static_cast<uint32_t>(m_vFrameVertices.size());
            const uint32_t baseIndex = static_cast<uint32_t>(m_vFrameIndices.size());
            const uint32_t firstDraw = static_cast<uint32_t>(m_vFrameDrawItems.size());
            const uint32_t triFirst = static_cast<uint32_t>(m_vFrameTriangleVertices3D.size());
            const uint32_t lineFirst = static_cast<uint32_t>(m_vFrameLineVertices3D.size());
            const uint32_t first3DDrawItem = static_cast<uint32_t>(m_vFrame3DDrawItems.size());

            m_vFrameVertices.insert(m_vFrameVertices.end(), m_vBatchVertices.begin(), m_vBatchVertices.end());
            for (uint32_t idx : m_vBatchIndices)
            {
                m_vFrameIndices.push_back(baseVertex + idx);
            }
            for (VkDrawItem item : m_vBatchDrawItems)
            {
                item.m_FirstIndex += baseIndex;
                m_vFrameDrawItems.push_back(item);
            }
            const uint32_t base3DIndex = static_cast<uint32_t>(m_vFrameIndices.size());
            m_vFrameIndices.insert(m_vFrameIndices.end(),
                                   m_Main3DBatch.m_vTriangleIndices.begin(), m_Main3DBatch.m_vTriangleIndices.end());

            m_vFrameTriangleVertices3D.insert(m_vFrameTriangleVertices3D.end(),
                                              m_Main3DBatch.m_vTriangleVertices.begin(),
                                              m_Main3DBatch.m_vTriangleVertices.end());
            m_vFrameLineVertices3D.insert(m_vFrameLineVertices3D.end(),
                                          m_Main3DBatch.m_vLineVertices.begin(),
                                          m_Main3DBatch.m_vLineVertices.end());
            for (Vk3DDrawItem item : m_Main3DBatch.m_vDrawItems)
            {
                item.m_FirstVertex += triFirst;
                if (!item.m_GpuMesh)
                    item.m_FirstIndex += base3DIndex;
                m_vFrame3DDrawItems.push_back(item);
            }
            m_Frame3DShaderProgramId = m_Main3DBatch.m_ShaderProgramId;

            m_vFramePasses.push_back(VkFramePass{
                0,
                firstDraw,
                static_cast<uint32_t>(m_vFrameDrawItems.size() - firstDraw),
                static_cast<uint32_t>(m_SwapChainExtent.width),
                static_cast<uint32_t>(m_SwapChainExtent.height),
                triFirst,
                static_cast<uint32_t>(m_Main3DBatch.m_vTriangleVertices.size()),
                lineFirst,
                static_cast<uint32_t>(m_Main3DBatch.m_vLineVertices.size()),
                first3DDrawItem,
                static_cast<uint32_t>(m_vFrame3DDrawItems.size() - first3DDrawItem)});
        }
    }

    bool QuarkVkRenderer::UploadFrameGeometry(uint32_t frameIndex)
    {
        VkFrameData& frame = m_aFrames[frameIndex];
        const VkDeviceSize vertexBytes = m_vFrameVertices.size() * sizeof(VkBatchVertex);
        const VkDeviceSize indexBytes = m_vFrameIndices.size() * sizeof(uint32_t);
        const VkDeviceSize vertexBytes3D =
            (m_vFrameTriangleVertices3D.size() + m_vFrameLineVertices3D.size()) * sizeof(Vk3DVertex);

        if (!EnsureMappedBufferCapacity(frame.m_VertexBuffer, frame.m_VertexMemory, frame.m_pVertexMapped,
                                        frame.m_VertexCapacity, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
            !EnsureMappedBufferCapacity(frame.m_IndexBuffer, frame.m_IndexMemory, frame.m_pIndexMapped,
                                        frame.m_IndexCapacity, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT) ||
            !EnsureMappedBufferCapacity(frame.m_VertexBuffer3D, frame.m_VertexMemory3D, frame.m_pVertexMapped3D,
                                        frame.m_VertexCapacity3D, vertexBytes3D, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
        {
            TraceLog(LogLevel::Error, "VULKAN", "Failed to grow per-frame geometry buffer.");
            return false;
        }

        auto parallelCopy = [this](void* dst, const void* src, size_t totalBytes)
        {
            if (totalBytes == 0 || !dst || !src)
                return;
            constexpr size_t MIN_PARALLEL_COPY_BYTES = 128 * 1024;
            if (totalBytes < MIN_PARALLEL_COPY_BYTES || !m_ThreadPool.IsMultithreaded())
            {
                std::memcpy(dst, src, totalBytes);
                return;
            }
            const size_t chunkSize = std::max<size_t>(64 * 1024,
                                                      (totalBytes + m_ThreadPool.WorkerCount() - 1) / m_ThreadPool.WorkerCount());
            m_ThreadPool.ParallelFor(totalBytes, chunkSize, [dst, src](uint32_t, size_t begin, size_t end)
                                     {
                                         std::memcpy(static_cast<char*>(dst) + begin, static_cast<const char*>(src) + begin, end - begin);
                                     });
        };

        if (!m_vFrameVertices.empty() && frame.m_pVertexMapped)
        {
            parallelCopy(frame.m_pVertexMapped, m_vFrameVertices.data(),
                         m_vFrameVertices.size() * sizeof(VkBatchVertex));
        }
        if (!m_vFrameIndices.empty() && frame.m_pIndexMapped)
        {
            parallelCopy(frame.m_pIndexMapped, m_vFrameIndices.data(),
                         m_vFrameIndices.size() * sizeof(uint32_t));
        }
        if (frame.m_pVertexMapped3D)
        {
            const size_t triBytes = m_vFrameTriangleVertices3D.size() * sizeof(Vk3DVertex);
            const size_t lineBytes = m_vFrameLineVertices3D.size() * sizeof(Vk3DVertex);
            if (triBytes + lineBytes > 0)
            {
                parallelCopy(frame.m_pVertexMapped3D, m_vFrameTriangleVertices3D.data(), triBytes);
                if (lineBytes > 0)
                {
                    parallelCopy(static_cast<char*>(frame.m_pVertexMapped3D) + triBytes,
                                 m_vFrameLineVertices3D.data(), lineBytes);
                }
            }
        }
        if (!m_vFrameInstanceTransforms.empty() && m_p3DInstanceMapped)
        {
            const size_t clampedCount = std::min(m_vFrameInstanceTransforms.size(), VK_MAX_INSTANCES_PER_FRAME);
            auto* pDstBase = static_cast<VkInstanceTransformData*>(m_p3DInstanceMapped) +
                            static_cast<size_t>(frameIndex) * VK_MAX_INSTANCES_PER_FRAME;
            std::memcpy(pDstBase, m_vFrameInstanceTransforms.data(),
                        clampedCount * sizeof(VkInstanceTransformData));
            vmaFlushAllocation(m_GpuAllocator.GetAllocator(), m_3DInstanceAllocation,
                               static_cast<VkDeviceSize>(frameIndex) * VK_MAX_INSTANCES_PER_FRAME * sizeof(VkInstanceTransformData),
                               static_cast<VkDeviceSize>(clampedCount) * sizeof(VkInstanceTransformData));
        }
        return true;
    }

    void QuarkVkRenderer::AppendQuadToBatch(
        std::vector<VkBatchVertex>& vVertices,
        std::vector<uint32_t>& vIndices,
        std::vector<VkDrawItem>& vDrawItems,
        VkDescriptorSet ds,
        float x0, float y0,
        float x1, float y1,
        float x2, float y2,
        float x3, float y3,
        float r, float g, float b, float a,
        float u0, float v0,
        float u1, float v1)
    {
        const Vec2 p0 = ApplyCameraTransform(Vec2{x0, y0});
        const Vec2 p1 = ApplyCameraTransform(Vec2{x1, y1});
        const Vec2 p2 = ApplyCameraTransform(Vec2{x2, y2});
        const Vec2 p3 = ApplyCameraTransform(Vec2{x3, y3});

        const uint32_t base = static_cast<uint32_t>(vVertices.size());
        vVertices.push_back({p0.x, p0.y, u0, v0, r, g, b, a});
        vVertices.push_back({p1.x, p1.y, u1, v0, r, g, b, a});
        vVertices.push_back({p2.x, p2.y, u1, v1, r, g, b, a});
        vVertices.push_back({p3.x, p3.y, u0, v1, r, g, b, a});

        const uint32_t firstIndex = static_cast<uint32_t>(vIndices.size());
        vIndices.push_back(base + 0);
        vIndices.push_back(base + 1);
        vIndices.push_back(base + 2);
        vIndices.push_back(base + 2);
        vIndices.push_back(base + 3);
        vIndices.push_back(base + 0);

        if (!vDrawItems.empty() &&
            vDrawItems.back().m_DescriptorSet == ds &&
            vDrawItems.back().m_ShaderProgramId == m_VkShaderCompiler.CurrentProgramId() &&
            vDrawItems.back().m_DebugOverlay == m_DrawingDebugOverlay &&
            vDrawItems.back().m_FirstIndex + vDrawItems.back().m_IndexCount == firstIndex)
        {
            vDrawItems.back().m_IndexCount += 6;
        }
        else
        {
            vDrawItems.push_back({0, m_VkShaderCompiler.CurrentProgramId(), ds, firstIndex, 6,
                                 m_DrawingDebugOverlay});
        }
    }

    void QuarkVkRenderer::AppendQuad(
        VkDescriptorSet ds,
        float x0, float y0,
        float x1, float y1,
        float x2, float y2,
        float x3, float y3,
        float r, float g, float b, float a,
        float u0, float v0, float u1, float v1)
    {
        if (m_ActiveRenderTargetId != 0)
        {
            auto itRt = m_RenderTargets.find(m_ActiveRenderTargetId);
            if (itRt != m_RenderTargets.end())
            {
                AppendQuadToBatch(itRt->second.m_vVertices, itRt->second.m_vIndices,
                                  itRt->second.m_vDrawItems, ds,
                                  x0, y0, x1, y1, x2, y2, x3, y3,
                                  r, g, b, a, u0, v0, u1, v1);
                return;
            }
        }
        AppendQuadToBatch(m_vBatchVertices, m_vBatchIndices, m_vBatchDrawItems, ds,
                          x0, y0, x1, y1, x2, y2, x3, y3,
                          r, g, b, a, u0, v0, u1, v1);
    }

    bool QuarkVkRenderer::RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex)
    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
            return false;

        m_VkCommandContext.ResetFrameWorkerPools(m_CurrentFrame);

        const VkFrameData& frame = m_aFrames[m_CurrentFrame];
        const VkDeviceSize offsets[] = {0};
        const uint32_t frameUniformOffset = static_cast<uint32_t>(
            static_cast<VkDeviceSize>(m_CurrentFrame) * VK_3D_UNIFORM_FRAME_STRIDE);
        const uint32_t uniformDynamicOffsets[] = {
            frameUniformOffset, frameUniformOffset, frameUniformOffset};
        const VkDescriptorSet whiteDescriptorSet = m_White3DDescriptorSet;
        VkDescriptorSet white2DDescriptorSet = VK_NULL_HANDLE;
        const VkTextureData* pWhiteTex = m_VkResources.Get(m_WhiteTextureId);
        if (pWhiteTex != nullptr)
        {
            white2DDescriptorSet = pWhiteTex->m_DescriptorSet;
        }

        if (m_p3DDummyMapped != nullptr)
        {
            std::memcpy(static_cast<char*>(m_p3DDummyMapped) + 0,
                        m_CurrentMatrix.m, sizeof(m_CurrentMatrix.m));
            std::memcpy(static_cast<char*>(m_p3DDummyMapped) + 64,
                        m_ViewMatrix.m, sizeof(m_ViewMatrix.m));
            std::memcpy(static_cast<char*>(m_p3DDummyMapped) + 128,
                        m_ProjectionMatrix.m, sizeof(m_ProjectionMatrix.m));

            float* pLightData = static_cast<float*>(static_cast<void*>(static_cast<char*>(m_p3DDummyMapped) + 1024));
            std::fill(pLightData, pLightData + 80, 0.0f);
            pLightData[0] = 0.1f;
            pLightData[1] = 0.1f;
            pLightData[2] = 0.1f;
            pLightData[3] = 1.0f;
            pLightData[4] = 1.0f;
            pLightData[5] = 1.0f;
            pLightData[6] = 1.0f;
            pLightData[7] = 1.0f;
            pLightData[8] = m_ViewPos.x;
            pLightData[9] = m_ViewPos.y;
            pLightData[10] = m_ViewPos.z;
            pLightData[11] = 1.0f;
            for (int lightIndex = 0; lightIndex < 4; ++lightIndex)
            {
                const VkLight3D& light = m_aLights[static_cast<size_t>(lightIndex)];
                const size_t base = 12 + static_cast<size_t>(lightIndex) * 16;
                pLightData[base + 0] = light.m_Position.x;
                pLightData[base + 1] = light.m_Position.y;
                pLightData[base + 2] = light.m_Position.z;
                pLightData[base + 3] = 1.0f;
                pLightData[base + 4] = light.m_Target.x;
                pLightData[base + 5] = light.m_Target.y;
                pLightData[base + 6] = light.m_Target.z;
                pLightData[base + 7] = 1.0f;
                pLightData[base + 8] = light.m_Color.x;
                pLightData[base + 9] = light.m_Color.y;
                pLightData[base + 10] = light.m_Color.z;
                pLightData[base + 11] = 1.0f;
                pLightData[base + 12] = light.m_Attenuation;
                const int enabledValue = light.m_Enabled ? 1 : 0;
                std::memcpy(pLightData + base + 13, &enabledValue, sizeof(int));
                const int typeValue = light.m_Type;
                std::memcpy(pLightData + base + 14, &typeValue, sizeof(int));
            }
        }

        Vk3DPushConstants lightingConstants{};
        for (int lightIndex = 0; lightIndex < 4; ++lightIndex)
        {
            const VkLight3D& light = m_aLights[static_cast<size_t>(lightIndex)];
            lightingConstants.m_aLightPositions[lightIndex][0] = light.m_Position.x;
            lightingConstants.m_aLightPositions[lightIndex][1] = light.m_Position.y;
            lightingConstants.m_aLightPositions[lightIndex][2] = light.m_Position.z;
            lightingConstants.m_aLightPositions[lightIndex][3] = 1.0f;
            lightingConstants.m_aLightColors[lightIndex][0] = light.m_Color.x;
            lightingConstants.m_aLightColors[lightIndex][1] = light.m_Color.y;
            lightingConstants.m_aLightColors[lightIndex][2] = light.m_Color.z;
            lightingConstants.m_aLightColors[lightIndex][3] = 1.0f;
            lightingConstants.m_aLightEnabled[lightIndex] = light.m_Enabled ? 1.0f : 0.0f;
        }
        lightingConstants.m_aTimeData[0] = static_cast<float>(SDL_GetTicks()) / 1000.0f;

        auto pushLightingToCmd = [&lightingConstants](VkCommandBuffer targetCmd, VkPipelineLayout layout)
        {
            vkCmdPushConstants(targetCmd, layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(lightingConstants), &lightingConstants);
        };
        auto pushGpuMeshTransformToCmd = [](VkCommandBuffer targetCmd, VkPipelineLayout layout, const Vk3DDrawItem& item)
        {
            VkGpuMeshPushConstants transform{};
            std::memcpy(transform.m_aModelViewProjection, item.m_ModelViewProjection.m, sizeof(transform.m_aModelViewProjection));
            std::memcpy(transform.m_aModel, item.m_Model.m, sizeof(transform.m_aModel));
            std::copy(item.m_aMaterialColor.begin(), item.m_aMaterialColor.end(), transform.m_aMaterialColor);
            transform.m_ObjectSpaceTransform = item.m_GpuMesh ? 1u : 0u;
            transform.m_UseInstanceBuffer = item.m_UseInstanceBuffer ? 1u : 0u;
            vkCmdPushConstants(targetCmd, layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(transform), &transform);
        };

        VkPipeline defaultMainPipeline2D = m_VkPipeline.Get2D();
        if (m_VkShaderCompiler.CurrentProgramId() != 0)
        {
            const VkShaderProgramData* pShaderProgram = m_VkShaderCompiler.GetProgram(m_VkShaderCompiler.CurrentProgramId());
            if (pShaderProgram != nullptr && pShaderProgram->m_Pipeline != VK_NULL_HANDLE)
            {
                defaultMainPipeline2D = pShaderProgram->m_Pipeline;
            }
        }

        enum class SecondaryTaskKind
        {
            Draw3DChunk,
            Draw2DChunk
        };

        struct SecondaryPassTask
        {
            size_t m_PassIndex = 0;
            SecondaryTaskKind m_Kind = SecondaryTaskKind::Draw3DChunk;
            VkRenderPass m_RenderPass = VK_NULL_HANDLE;
            VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
            uint32_t m_Width = 0;
            uint32_t m_Height = 0;
            bool m_IsOffscreen = false;
            uint32_t m_ChunkBegin = 0;
            uint32_t m_ChunkEnd = 0;
            bool m_IsLast3DChunk = false;
            VkCommandBuffer m_RecordedCmd = VK_NULL_HANDLE;
        };

        const uint32_t workerCount = std::max(1u, m_ThreadPool.WorkerCount());
        std::vector<SecondaryPassTask> vSecondaryTasks;
        vSecondaryTasks.reserve(m_vFramePasses.size() * 4);

        for (size_t passIdx = 0; passIdx < m_vFramePasses.size(); ++passIdx)
        {
            const VkFramePass& pass = m_vFramePasses[passIdx];
            const bool isOffscreen = (pass.m_RenderTargetId != 0);
            VkRenderPass targetRenderPass = VK_NULL_HANDLE;
            VkFramebuffer targetFramebuffer = VK_NULL_HANDLE;
            uint32_t passWidth = pass.m_Width;
            uint32_t passHeight = pass.m_Height;

            if (isOffscreen)
            {
                if (m_RenderTargets.find(pass.m_RenderTargetId) == m_RenderTargets.end())
                {
                    continue;
                }
                targetRenderPass = m_VkRenderTarget.RenderPass();
                targetFramebuffer = m_VkRenderTarget.Framebuffer(pass.m_RenderTargetId);
            }
            else
            {
                targetRenderPass = m_VkRenderPass.Get();
                targetFramebuffer = m_vSwapChainFramebuffers[imageIndex];
                passWidth = m_SwapChainExtent.width;
                passHeight = m_SwapChainExtent.height;
            }

            if (targetRenderPass == VK_NULL_HANDLE || targetFramebuffer == VK_NULL_HANDLE)
            {
                continue;
            }

            if (((pass.m_TriVertexCount + pass.m_LineVertexCount) > 0 || pass.m_DrawItemCount3D > 0) &&
                whiteDescriptorSet != VK_NULL_HANDLE)
            {
                if (pass.m_DrawItemCount3D > 0)
                {
                    const uint32_t chunkSize3D = std::max<uint32_t>(
                        16u, (pass.m_DrawItemCount3D + workerCount - 1) / workerCount);
                    for (uint32_t start = 0; start < pass.m_DrawItemCount3D; start += chunkSize3D)
                    {
                        const uint32_t end = std::min(pass.m_DrawItemCount3D, start + chunkSize3D);
                        SecondaryPassTask task{};
                        task.m_PassIndex = passIdx;
                        task.m_Kind = SecondaryTaskKind::Draw3DChunk;
                        task.m_RenderPass = targetRenderPass;
                        task.m_Framebuffer = targetFramebuffer;
                        task.m_Width = passWidth;
                        task.m_Height = passHeight;
                        task.m_IsOffscreen = isOffscreen;
                        task.m_ChunkBegin = start;
                        task.m_ChunkEnd = end;
                        task.m_IsLast3DChunk = (end == pass.m_DrawItemCount3D);
                        vSecondaryTasks.push_back(task);
                    }
                }
                else
                {
                    SecondaryPassTask task{};
                    task.m_PassIndex = passIdx;
                    task.m_Kind = SecondaryTaskKind::Draw3DChunk;
                    task.m_RenderPass = targetRenderPass;
                    task.m_Framebuffer = targetFramebuffer;
                    task.m_Width = passWidth;
                    task.m_Height = passHeight;
                    task.m_IsOffscreen = isOffscreen;
                    task.m_ChunkBegin = 0;
                    task.m_ChunkEnd = 0;
                    task.m_IsLast3DChunk = true;
                    vSecondaryTasks.push_back(task);
                }
            }

            uint32_t nonOverlay2DCount = 0;
            for (uint32_t i = 0; i < pass.m_DrawItemCount; ++i)
            {
                const VkDrawItem& item = m_vFrameDrawItems[pass.m_FirstDrawItem + i];
                if (!item.m_DebugOverlay && item.m_DescriptorSet != VK_NULL_HANDLE)
                {
                    ++nonOverlay2DCount;
                }
            }
            if (nonOverlay2DCount > 0)
            {
                const uint32_t chunkSize2D = std::max<uint32_t>(
                    32u, (pass.m_DrawItemCount + workerCount - 1) / workerCount);
                for (uint32_t start = 0; start < pass.m_DrawItemCount; start += chunkSize2D)
                {
                    const uint32_t end = std::min(pass.m_DrawItemCount, start + chunkSize2D);
                    SecondaryPassTask task{};
                    task.m_PassIndex = passIdx;
                    task.m_Kind = SecondaryTaskKind::Draw2DChunk;
                    task.m_RenderPass = targetRenderPass;
                    task.m_Framebuffer = targetFramebuffer;
                    task.m_Width = passWidth;
                    task.m_Height = passHeight;
                    task.m_IsOffscreen = isOffscreen;
                    task.m_ChunkBegin = start;
                    task.m_ChunkEnd = end;
                    task.m_IsLast3DChunk = false;
                    vSecondaryTasks.push_back(task);
                }
            }
        }

        auto applyPassViewportAndScissor = [this](VkCommandBuffer targetCmd, uint32_t width, uint32_t height)
        {
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(width);
            viewport.height = static_cast<float>(height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(targetCmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = {width, height};
            if (m_ScissorEnabled)
            {
                scissor = m_ScissorRect;
                scissor.offset.x = std::max(0, std::min(scissor.offset.x, static_cast<int32_t>(width)));
                scissor.offset.y = std::max(0, std::min(scissor.offset.y, static_cast<int32_t>(height)));
            }
            vkCmdSetScissor(targetCmd, 0, 1, &scissor);
        };

        auto recordSecondaryTask = [&](uint32_t workerIndex, SecondaryPassTask& task)
        {
            VkCommandBuffer secCmd = m_VkCommandContext.AcquireSecondaryCommandBuffer(m_CurrentFrame, workerIndex);
            if (secCmd == VK_NULL_HANDLE)
            {
                return;
            }

            VkCommandBufferInheritanceInfo inheritanceInfo{};
            inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
            inheritanceInfo.renderPass = task.m_RenderPass;
            inheritanceInfo.subpass = 0;
            inheritanceInfo.framebuffer = task.m_Framebuffer;

            VkCommandBufferBeginInfo secBeginInfo{};
            secBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            secBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT |
                                 VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
            secBeginInfo.pInheritanceInfo = &inheritanceInfo;
            if (vkBeginCommandBuffer(secCmd, &secBeginInfo) != VK_SUCCESS)
            {
                return;
            }

            applyPassViewportAndScissor(secCmd, task.m_Width, task.m_Height);
            const VkFramePass& pass = m_vFramePasses[task.m_PassIndex];

            if (task.m_Kind == SecondaryTaskKind::Draw3DChunk)
            {
                const VkPipeline defaultTriPipeline = task.m_IsOffscreen
                                                          ? m_VkPipeline.GetOffscreen3DTri()
                                                          : m_VkPipeline.Get3DTri();
                const VkPipeline defaultLinePipeline = task.m_IsOffscreen
                                                           ? m_VkPipeline.GetOffscreen3DLines()
                                                           : m_VkPipeline.Get3DLines();
                const VkPipelineLayout layout3D = m_VkPipeline.GetLayout3D();

                VkPipeline custom3DPipeline = defaultTriPipeline;
                VkDescriptorSet custom3DSet = whiteDescriptorSet;
                if (!task.m_IsOffscreen && m_Frame3DShaderProgramId != 0)
                {
                    const VkShaderProgramData* pShaderProgram = m_VkShaderCompiler.GetProgram(m_Frame3DShaderProgramId);
                    if (pShaderProgram != nullptr && pShaderProgram->m_Pipeline3D != VK_NULL_HANDLE)
                    {
                        custom3DPipeline = pShaderProgram->m_Pipeline3D;
                        custom3DSet = pShaderProgram->m_DescriptorSet3D;
                    }
                }

                vkCmdBindVertexBuffers(secCmd, 0, 1, &frame.m_VertexBuffer3D, offsets);
                vkCmdBindIndexBuffer(secCmd, frame.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindDescriptorSets(secCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        layout3D, 0, 1, &custom3DSet,
                                        3, uniformDynamicOffsets);
                pushLightingToCmd(secCmd, layout3D);

                if (pass.m_DrawItemCount3D > 0)
                {
                    uint32_t cursor = pass.m_TriFirstVertex;
                    if (task.m_ChunkBegin > 0)
                    {
                        const Vk3DDrawItem& prevItem = m_vFrame3DDrawItems[pass.m_First3DDrawItem + task.m_ChunkBegin - 1];
                        cursor = std::max(cursor, prevItem.m_FirstVertex + (prevItem.m_GpuMesh ? 0u : prevItem.m_VertexCount));
                    }

                    bool gpuTransformPushActive = false;
                    VkPipeline boundPipeline = VK_NULL_HANDLE;
                    VkDescriptorSet boundDescriptorSet = VK_NULL_HANDLE;
                    VkBuffer boundVertexBuffer = frame.m_VertexBuffer3D;
                    VkBuffer boundIndexBuffer = frame.m_IndexBuffer;

                    for (uint32_t i = task.m_ChunkBegin; i < task.m_ChunkEnd; ++i)
                    {
                        const Vk3DDrawItem& item = m_vFrame3DDrawItems[pass.m_First3DDrawItem + i];
                        const VkShaderProgramData* itemShaderProgram = item.m_ShaderProgramId != 0
                                                                           ? m_VkShaderCompiler.GetProgram(item.m_ShaderProgramId)
                                                                           : nullptr;
                        const bool usesGpuMeshShader = itemShaderProgram && itemShaderProgram->m_SupportsGpuMeshTransform;
                        if (!usesGpuMeshShader && gpuTransformPushActive)
                        {
                            pushLightingToCmd(secCmd, layout3D);
                            gpuTransformPushActive = false;
                        }
                        if (item.m_FirstVertex > cursor && defaultTriPipeline != VK_NULL_HANDLE)
                        {
                            if (boundVertexBuffer != frame.m_VertexBuffer3D)
                            {
                                vkCmdBindVertexBuffers(secCmd, 0, 1, &frame.m_VertexBuffer3D, offsets);
                                boundVertexBuffer = frame.m_VertexBuffer3D;
                            }
                            if (boundIndexBuffer != frame.m_IndexBuffer)
                            {
                                vkCmdBindIndexBuffer(secCmd, frame.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                                boundIndexBuffer = frame.m_IndexBuffer;
                            }
                            if (boundPipeline != defaultTriPipeline)
                            {
                                vkCmdBindPipeline(secCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultTriPipeline);
                                boundPipeline = defaultTriPipeline;
                            }
                            if (boundDescriptorSet != whiteDescriptorSet)
                            {
                                vkCmdBindDescriptorSets(secCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                        layout3D, 0, 1, &whiteDescriptorSet,
                                                        3, uniformDynamicOffsets);
                                boundDescriptorSet = whiteDescriptorSet;
                            }
                            vkCmdDraw(secCmd, item.m_FirstVertex - cursor, 1, cursor, 0);
                            RecordDebugRenderDraw(item.m_FirstVertex - cursor);
                        }

                        VkPipeline itemPipeline = task.m_IsOffscreen
                                                      ? m_VkPipeline.GetOffscreen3DTri(item.m_BackfaceCulling)
                                                      : m_VkPipeline.Get3DTri(item.m_BackfaceCulling);
                        VkDescriptorSet descriptorSet = item.m_DescriptorSet != VK_NULL_HANDLE
                                                            ? item.m_DescriptorSet
                                                            : whiteDescriptorSet;
                        if (itemShaderProgram != nullptr)
                        {
                            const VkPipeline shaderPipe = task.m_IsOffscreen
                                                              ? (item.m_BackfaceCulling ? itemShaderProgram->m_Pipeline3DOffscreenCulled : itemShaderProgram->m_Pipeline3DOffscreen)
                                                              : (item.m_BackfaceCulling ? itemShaderProgram->m_Pipeline3DCulled : itemShaderProgram->m_Pipeline3D);
                            if (shaderPipe != VK_NULL_HANDLE)
                            {
                                itemPipeline = shaderPipe;
                            }
                        }
                        if (itemPipeline == VK_NULL_HANDLE || descriptorSet == VK_NULL_HANDLE)
                        {
                            cursor = std::max(cursor, item.m_FirstVertex + (item.m_GpuMesh ? 0u : item.m_VertexCount));
                            continue;
                        }

                        if (boundPipeline != itemPipeline)
                        {
                            vkCmdBindPipeline(secCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, itemPipeline);
                            boundPipeline = itemPipeline;
                        }
                        if (boundDescriptorSet != descriptorSet)
                        {
                            vkCmdBindDescriptorSets(secCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                    layout3D, 0, 1, &descriptorSet,
                                                    3, uniformDynamicOffsets);
                            boundDescriptorSet = descriptorSet;
                        }

                        if (item.m_Indexed)
                        {
                            if (item.m_GpuMesh)
                            {
                                if (boundVertexBuffer != item.m_GpuVertexBuffer)
                                {
                                    vkCmdBindVertexBuffers(secCmd, 0, 1, &item.m_GpuVertexBuffer, offsets);
                                    boundVertexBuffer = item.m_GpuVertexBuffer;
                                }
                                if (boundIndexBuffer != item.m_GpuIndexBuffer)
                                {
                                    vkCmdBindIndexBuffer(secCmd, item.m_GpuIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                                    boundIndexBuffer = item.m_GpuIndexBuffer;
                                }
                                pushGpuMeshTransformToCmd(secCmd, layout3D, item);
                                const uint32_t drawInstances = item.m_InstanceCount > 0 ? item.m_InstanceCount : 1u;
                                vkCmdDrawIndexed(secCmd, item.m_IndexCount, drawInstances, 0, 0, item.m_FirstInstance);
                                gpuTransformPushActive = true;
                            }
                            else
                            {
                                if (boundVertexBuffer != frame.m_VertexBuffer3D)
                                {
                                    vkCmdBindVertexBuffers(secCmd, 0, 1, &frame.m_VertexBuffer3D, offsets);
                                    boundVertexBuffer = frame.m_VertexBuffer3D;
                                }
                                if (boundIndexBuffer != frame.m_IndexBuffer)
                                {
                                    vkCmdBindIndexBuffer(secCmd, frame.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                                    boundIndexBuffer = frame.m_IndexBuffer;
                                }
                                if (usesGpuMeshShader)
                                {
                                    pushGpuMeshTransformToCmd(secCmd, layout3D, item);
                                    gpuTransformPushActive = true;
                                }
                                vkCmdDrawIndexed(secCmd, item.m_IndexCount, 1, item.m_FirstIndex,
                                                 static_cast<int32_t>(item.m_FirstVertex), 0);
                            }
                            RecordDebugRenderDraw(item.m_IndexCount);
                        }
                        else
                        {
                            if (boundVertexBuffer != frame.m_VertexBuffer3D)
                            {
                                vkCmdBindVertexBuffers(secCmd, 0, 1, &frame.m_VertexBuffer3D, offsets);
                                boundVertexBuffer = frame.m_VertexBuffer3D;
                            }
                            if (usesGpuMeshShader)
                            {
                                pushGpuMeshTransformToCmd(secCmd, layout3D, item);
                                gpuTransformPushActive = true;
                            }
                            vkCmdDraw(secCmd, item.m_VertexCount, 1, item.m_FirstVertex, 0);
                            RecordDebugRenderDraw(item.m_VertexCount);
                        }
                        cursor = std::max(cursor, item.m_FirstVertex + (item.m_GpuMesh ? 0u : item.m_VertexCount));
                    }

                    if (task.m_IsLast3DChunk)
                    {
                        const uint32_t triEnd = pass.m_TriFirstVertex + pass.m_TriVertexCount;
                        if (cursor < triEnd && defaultTriPipeline != VK_NULL_HANDLE)
                        {
                            if (gpuTransformPushActive)
                            {
                                pushLightingToCmd(secCmd, layout3D);
                                gpuTransformPushActive = false;
                            }
                            vkCmdBindVertexBuffers(secCmd, 0, 1, &frame.m_VertexBuffer3D, offsets);
                            vkCmdBindIndexBuffer(secCmd, frame.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                            vkCmdBindPipeline(secCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultTriPipeline);
                            vkCmdBindDescriptorSets(secCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                    layout3D, 0, 1, &whiteDescriptorSet,
                                                    3, uniformDynamicOffsets);
                            vkCmdDraw(secCmd, triEnd - cursor, 1, cursor, 0);
                            RecordDebugRenderDraw(triEnd - cursor);
                        }
                    }
                }
                else if (task.m_IsLast3DChunk && pass.m_TriVertexCount > 0 && custom3DPipeline != VK_NULL_HANDLE)
                {
                    vkCmdBindPipeline(secCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, custom3DPipeline);
                    vkCmdDraw(secCmd, pass.m_TriVertexCount, 1, pass.m_TriFirstVertex, 0);
                    RecordDebugRenderDraw(pass.m_TriVertexCount);
                }

                if (task.m_IsLast3DChunk && pass.m_LineVertexCount > 0 && defaultLinePipeline != VK_NULL_HANDLE)
                {
                    pushLightingToCmd(secCmd, layout3D);
                    vkCmdBindVertexBuffers(secCmd, 0, 1, &frame.m_VertexBuffer3D, offsets);
                    vkCmdBindIndexBuffer(secCmd, frame.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdBindDescriptorSets(secCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            layout3D, 0, 1, &whiteDescriptorSet,
                                            3, uniformDynamicOffsets);
                    vkCmdBindPipeline(secCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultLinePipeline);
                    vkCmdDraw(secCmd, pass.m_LineVertexCount, 1,
                              static_cast<uint32_t>(m_vFrameTriangleVertices3D.size()) + pass.m_LineFirstVertex, 0);
                    RecordDebugRenderDraw(pass.m_LineVertexCount);
                }
            }
            else
            {
                const VkPushConstants2D pushConstants2D{
                    static_cast<float>(task.m_Width),
                    static_cast<float>(task.m_Height)};
                vkCmdPushConstants(secCmd, m_VkPipeline.GetLayout2D(),
                                   VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(pushConstants2D), &pushConstants2D);
                vkCmdBindVertexBuffers(secCmd, 0, 1, &frame.m_VertexBuffer, offsets);
                vkCmdBindIndexBuffer(secCmd, frame.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

                if (task.m_IsOffscreen)
                {
                    if (m_VkPipeline.GetOffscreen2D() != VK_NULL_HANDLE)
                    {
                        vkCmdBindPipeline(secCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_VkPipeline.GetOffscreen2D());
                    }
                    for (uint32_t i = task.m_ChunkBegin; i < task.m_ChunkEnd; ++i)
                    {
                        const VkDrawItem& item = m_vFrameDrawItems[pass.m_FirstDrawItem + i];
                        if (item.m_DescriptorSet == VK_NULL_HANDLE || item.m_DebugOverlay)
                            continue;
                        vkCmdBindDescriptorSets(secCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                m_VkPipeline.GetLayout2D(), 0, 1, &item.m_DescriptorSet, 0, nullptr);
                        vkCmdDrawIndexed(secCmd, item.m_IndexCount, 1, item.m_FirstIndex, 0, 0);
                        RecordDebugRenderDraw(item.m_IndexCount);
                    }
                }
                else
                {
                    if (white2DDescriptorSet != VK_NULL_HANDLE)
                    {
                        vkCmdBindDescriptorSets(secCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                m_VkPipeline.GetLayout2D(), 0, 1, &white2DDescriptorSet, 0, nullptr);
                    }
                    VkPipeline boundPipeline = VK_NULL_HANDLE;
                    for (uint32_t i = task.m_ChunkBegin; i < task.m_ChunkEnd; ++i)
                    {
                        const VkDrawItem& item = m_vFrameDrawItems[pass.m_FirstDrawItem + i];
                        if (item.m_DescriptorSet == VK_NULL_HANDLE || item.m_DebugOverlay)
                            continue;

                        VkPipeline itemPipeline = defaultMainPipeline2D;
                        if (item.m_ShaderProgramId != 0)
                        {
                            const VkShaderProgramData* pShaderProgram = m_VkShaderCompiler.GetProgram(item.m_ShaderProgramId);
                            if (pShaderProgram != nullptr && pShaderProgram->m_Pipeline != VK_NULL_HANDLE)
                            {
                                itemPipeline = pShaderProgram->m_Pipeline;
                            }
                        }
                        if (itemPipeline == VK_NULL_HANDLE)
                            continue;
                        if (itemPipeline != boundPipeline)
                        {
                            vkCmdBindPipeline(secCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, itemPipeline);
                            boundPipeline = itemPipeline;
                        }
                        vkCmdBindDescriptorSets(secCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                m_VkPipeline.GetLayout2D(), 0, 1, &item.m_DescriptorSet, 0, nullptr);
                        vkCmdDrawIndexed(secCmd, item.m_IndexCount, 1, item.m_FirstIndex, 0, 0);
                        RecordDebugRenderDraw(item.m_IndexCount);
                    }
                }
            }

            if (vkEndCommandBuffer(secCmd) == VK_SUCCESS)
            {
                task.m_RecordedCmd = secCmd;
            }
        };

        size_t totalSecondaryDrawWork = 0;
        for (const SecondaryPassTask& task : vSecondaryTasks)
        {
            totalSecondaryDrawWork += static_cast<size_t>(task.m_ChunkEnd - task.m_ChunkBegin);
        }

        if (vSecondaryTasks.size() == 1 || totalSecondaryDrawWork < 64)
        {
            for (SecondaryPassTask& task : vSecondaryTasks)
            {
                recordSecondaryTask(0, task);
            }
        }
        else if (vSecondaryTasks.size() > 1)
        {
            m_ThreadPool.ParallelFor(vSecondaryTasks.size(), 1u, [&](uint32_t workerIndex, size_t begin, size_t end)
                                     {
                                         for (size_t idx = begin; idx < end; ++idx)
                                         {
                                             recordSecondaryTask(workerIndex, vSecondaryTasks[idx]);
                                         }
                                     });
        }

        std::vector<VkCommandBuffer> vPassSecondaryCmds;
        vPassSecondaryCmds.reserve(32);

        for (size_t passIdx = 0; passIdx < m_vFramePasses.size(); ++passIdx)
        {
            const VkFramePass& pass = m_vFramePasses[passIdx];
            if (pass.m_RenderTargetId == 0)
                continue;

            auto itRt = m_RenderTargets.find(pass.m_RenderTargetId);
            if (itRt == m_RenderTargets.end())
                continue;

            const Color passClearColor = itRt->second.m_ClearColor;
            std::array<VkClearValue, 2> clearValues{};
            clearValues[0].color = {{NormalizeColorComponent(passClearColor.r),
                                     NormalizeColorComponent(passClearColor.g),
                                     NormalizeColorComponent(passClearColor.b),
                                     NormalizeColorComponent(passClearColor.a)}};
            clearValues[1].depthStencil = {1.0f, 0};

            vPassSecondaryCmds.clear();
            for (const SecondaryPassTask& task : vSecondaryTasks)
            {
                if (task.m_PassIndex == passIdx && task.m_RecordedCmd != VK_NULL_HANDLE)
                {
                    vPassSecondaryCmds.push_back(task.m_RecordedCmd);
                }
            }

            VkRenderPassBeginInfo rtPassInfo{};
            rtPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rtPassInfo.renderPass = m_VkRenderTarget.RenderPass();
            rtPassInfo.framebuffer = m_VkRenderTarget.Framebuffer(pass.m_RenderTargetId);
            rtPassInfo.renderArea.extent = {pass.m_Width, pass.m_Height};
            rtPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
            rtPassInfo.pClearValues = clearValues.data();
            vkCmdBeginRenderPass(cmd, &rtPassInfo,
                                 vPassSecondaryCmds.empty()
                                     ? VK_SUBPASS_CONTENTS_INLINE
                                     : VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
            if (!vPassSecondaryCmds.empty())
            {
                vkCmdExecuteCommands(cmd, static_cast<uint32_t>(vPassSecondaryCmds.size()), vPassSecondaryCmds.data());
            }
            vkCmdEndRenderPass(cmd);
        }

        vPassSecondaryCmds.clear();
        for (const SecondaryPassTask& task : vSecondaryTasks)
        {
            if (!task.m_IsOffscreen && task.m_RecordedCmd != VK_NULL_HANDLE)
            {
                vPassSecondaryCmds.push_back(task.m_RecordedCmd);
            }
        }

        bool hasDebugOverlay = false;
        for (const VkFramePass& pass : m_vFramePasses)
        {
            if (pass.m_RenderTargetId != 0)
                continue;
            for (uint32_t i = 0; i < pass.m_DrawItemCount; ++i)
            {
                if (m_vFrameDrawItems[pass.m_FirstDrawItem + i].m_DebugOverlay)
                {
                    hasDebugOverlay = true;
                    break;
                }
            }
        }

        const VulkanRenderCallback callback = GetVulkanRenderCallback();
        const VkPushConstants2D screenPushConstants{
            static_cast<float>(m_SwapChainExtent.width),
            static_cast<float>(m_SwapChainExtent.height)};

        auto recordOverlayCommands = [&](VkCommandBuffer targetCmd)
        {
            if (callback)
            {
                applyPassViewportAndScissor(targetCmd, m_SwapChainExtent.width, m_SwapChainExtent.height);
                callback(targetCmd);
            }
            if (hasDebugOverlay && m_VkPipeline.Get2D() != VK_NULL_HANDLE)
            {
                VkViewport overlayViewport{};
                overlayViewport.width = static_cast<float>(m_SwapChainExtent.width);
                overlayViewport.height = static_cast<float>(m_SwapChainExtent.height);
                overlayViewport.minDepth = 0.0f;
                overlayViewport.maxDepth = 1.0f;
                vkCmdSetViewport(targetCmd, 0, 1, &overlayViewport);

                VkRect2D overlayScissor{};
                overlayScissor.extent = m_SwapChainExtent;
                vkCmdSetScissor(targetCmd, 0, 1, &overlayScissor);
                vkCmdBindPipeline(targetCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_VkPipeline.Get2D());
                vkCmdBindVertexBuffers(targetCmd, 0, 1, &frame.m_VertexBuffer, offsets);
                vkCmdBindIndexBuffer(targetCmd, frame.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdPushConstants(targetCmd, m_VkPipeline.GetLayout2D(), VK_SHADER_STAGE_VERTEX_BIT,
                                   0, sizeof(screenPushConstants), &screenPushConstants);

                for (const VkFramePass& pass : m_vFramePasses)
                {
                    if (pass.m_RenderTargetId != 0)
                        continue;
                    for (uint32_t i = 0; i < pass.m_DrawItemCount; ++i)
                    {
                        const VkDrawItem& item = m_vFrameDrawItems[pass.m_FirstDrawItem + i];
                        if (!item.m_DebugOverlay || item.m_DescriptorSet == VK_NULL_HANDLE)
                            continue;
                        vkCmdBindDescriptorSets(targetCmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                m_VkPipeline.GetLayout2D(), 0, 1,
                                                &item.m_DescriptorSet, 0, nullptr);
                        vkCmdDrawIndexed(targetCmd, item.m_IndexCount, 1, item.m_FirstIndex, 0, 0);
                        RecordDebugRenderDraw(item.m_IndexCount);
                    }
                }
            }
        };

        if (!vPassSecondaryCmds.empty() && (callback || hasDebugOverlay))
        {
            VkCommandBuffer overlaySecCmd = m_VkCommandContext.AcquireSecondaryCommandBuffer(m_CurrentFrame, 0);
            if (overlaySecCmd != VK_NULL_HANDLE)
            {
                VkCommandBufferInheritanceInfo inheritanceInfo{};
                inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
                inheritanceInfo.renderPass = m_VkRenderPass.Get();
                inheritanceInfo.subpass = 0;
                inheritanceInfo.framebuffer = m_vSwapChainFramebuffers[imageIndex];

                VkCommandBufferBeginInfo secBeginInfo{};
                secBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                secBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT |
                                     VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
                secBeginInfo.pInheritanceInfo = &inheritanceInfo;
                if (vkBeginCommandBuffer(overlaySecCmd, &secBeginInfo) == VK_SUCCESS)
                {
                    recordOverlayCommands(overlaySecCmd);
                    if (vkEndCommandBuffer(overlaySecCmd) == VK_SUCCESS)
                    {
                        vPassSecondaryCmds.push_back(overlaySecCmd);
                    }
                }
            }
        }

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{NormalizeColorComponent(m_ClearColor.r),
                                 NormalizeColorComponent(m_ClearColor.g),
                                 NormalizeColorComponent(m_ClearColor.b),
                                 NormalizeColorComponent(m_ClearColor.a)}};
        clearValues[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_VkRenderPass.Get();
        renderPassInfo.framebuffer = m_vSwapChainFramebuffers[imageIndex];
        renderPassInfo.renderArea.extent = m_SwapChainExtent;
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        const bool useSecondaryForMainPass = !vPassSecondaryCmds.empty();
        vkCmdBeginRenderPass(cmd, &renderPassInfo,
                             useSecondaryForMainPass
                                 ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS
                                 : VK_SUBPASS_CONTENTS_INLINE);

        if (useSecondaryForMainPass)
        {
            vkCmdExecuteCommands(cmd, static_cast<uint32_t>(vPassSecondaryCmds.size()), vPassSecondaryCmds.data());
        }
        else
        {
            applyPassViewportAndScissor(cmd, m_SwapChainExtent.width, m_SwapChainExtent.height);
            recordOverlayCommands(cmd);
        }

        vkCmdEndRenderPass(cmd);
        return vkEndCommandBuffer(cmd) == VK_SUCCESS;
    }

    void QuarkVkRenderer::PushQuad(float x, float y, float w, float h, Color color,
                                   float u0, float v0, float u1, float v1)
    {
        VkDescriptorSet ds = m_VkResources.DescriptorSet(m_WhiteTextureId);
        if (ds == VK_NULL_HANDLE)
            return;

        const float r = NormalizeColorComponent(color.r);
        const float g = NormalizeColorComponent(color.g);
        const float b = NormalizeColorComponent(color.b);
        const float a = NormalizeColorComponent(color.a);

        AppendQuad(ds,
                   x, y,
                   x + w, y,
                   x + w, y + h,
                   x, y + h,
                   r, g, b, a,
                   u0, v0, u1, v1);
    }

    void QuarkVkRenderer::EnsureBatchTexture(VkDescriptorSet ds)
    {
        m_CurrentDescriptorSet = ds;
    }

    bool QuarkVkRenderer::UpdateTexture(const ITexture& texture, const void* pPixels)
    {
        if (!pPixels || texture.m_Id == 0)
            return false;
        return m_VkResources.UpdateTextureFromRGBA(texture.m_Id, static_cast<const unsigned char*>(pPixels),
                                                   static_cast<uint32_t>(texture.m_Width),
                                                   static_cast<uint32_t>(texture.m_Height));
    }

    bool QuarkVkRenderer::UpdateTextureRegion(const ITexture& texture, Rectangle region, const void* pPixels)
    {
        if (!pPixels || texture.m_Id == 0)
            return false;
        return m_VkResources.UpdateTextureRegionRGBA(texture.m_Id, static_cast<const unsigned char*>(pPixels),
                                                     static_cast<uint32_t>(region.x),
                                                     static_cast<uint32_t>(region.y),
                                                     static_cast<uint32_t>(region.width),
                                                     static_cast<uint32_t>(region.height));
    }
} // qci
