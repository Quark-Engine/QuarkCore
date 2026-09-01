#include "QuarkVkRenderer.hpp"
#include "../../QuarkInternal.hpp"

#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_vulkan.h>
#include <shaderc/shaderc.hpp>
#include <cstddef>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <filesystem>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace qc {

static const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

static float NormalizeColorComponent(std::uint8_t value) {
    return static_cast<float>(value) / 255.0f;
}

const char* GetVulkanVendorName(uint32_t vendorID) {
    switch (vendorID) {
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        case 0x13B5: return "ARM";
        case 0x5143: return "Qualcomm";
        case 0x1010: return "ImgTec";
        case 0x106B: return "Apple";
        case 0x1414: return "Microsoft";
        case 0x10005: return "Mesa";
        default:     return "Unknown";
    }
}

const char* GetVulkanDeviceTypeString(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "Discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "Virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU (Software)";
        default:                                     return "Other";
    }
}

std::string FormatVulkanDriverVersion(uint32_t vendorID, uint32_t driverVersion) {
    if (vendorID == 0x10DE) { // NVIDIA
        return TextFormat("%u.%u.%u.%u",
            (driverVersion >> 22) & 0x3FF,
            (driverVersion >> 14) & 0x0FF,
            (driverVersion >> 6)  & 0x0FF,
            (driverVersion)       & 0x03F);
    }
#if defined(_WIN32)
    if (vendorID == 0x8086) { // Intel
        return TextFormat("%u.%u", (driverVersion >> 14), (driverVersion & 0x3FFF));
    }
#endif
    return TextFormat("%u.%u.%u",
        VK_VERSION_MAJOR(driverVersion),
        VK_VERSION_MINOR(driverVersion),
        VK_VERSION_PATCH(driverVersion));
}

const char* GetPresentModeString(VkPresentModeKHR mode) {
    switch (mode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:    return "IMMEDIATE (VSync OFF, uncapped)";
        case VK_PRESENT_MODE_MAILBOX_KHR:      return "MAILBOX (Triple Buffering)";
        case VK_PRESENT_MODE_FIFO_KHR:         return "FIFO (VSync ON)";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED (Adaptive VSync)";
        default:                               return "Unknown";
    }
}

const char* GetVkFormatString(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case VK_FORMAT_B8G8R8A8_SRGB:  return "B8G8R8A8_SRGB";
        case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_SRGB:  return "R8G8B8A8_SRGB";
        case VK_FORMAT_D32_SFLOAT:     return "D32_SFLOAT";
        case VK_FORMAT_D32_SFLOAT_S8_UINT: return "D32_SFLOAT_S8_UINT";
        case VK_FORMAT_D24_UNORM_S8_UINT:  return "D24_UNORM_S8_UINT";
        default:                       return "VkFormat(Other)";
    }
}

QuarkVkRenderer::~QuarkVkRenderer() {
    Shutdown();
}

void QuarkVkRenderer::Init(SDL_Window* window, int width, int height) {
    uint32_t version = 0;
    vkEnumerateInstanceVersion(&version);

    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Initializing Vulkan renderer... (Supported Instance API: %d.%d.%d, Window: %dx%d)",
        VK_VERSION_MAJOR(version),
        VK_VERSION_MINOR(version),
        VK_VERSION_PATCH(version),
        width, height));

#ifdef __APPLE__
    TraceLog(LogLevel::Info, "VULKAN", "Apple platform detected, using MoltenVK...");
#endif

    m_window   = window;
    m_width    = width;
    m_height   = height;
    m_framebufferResized = false;

    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateInstance");
    CreateInstance();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateSurface");
    CreateSurface();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: PickPhysicalDevice");
    PickPhysicalDevice();
    m_msaaSamples = GetSampleCountForSamples(m_requestedMsaaSamples);
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateLogicalDevice");
    CreateLogicalDevice();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateSwapChain");
    CreateSwapChain();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateRenderPass");
    CreateRenderPass();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateOffscreenRenderPass");
    m_vkRenderTarget.Initialize(m_device, m_gpuAllocator, m_swapChainImageFormat, m_depthFormat);
    m_vkRenderTarget.CreateRenderPass();
    TraceLog(LogLevel::Info, "VULKAN", "Init step: DescriptorSetManager");
    m_vkDescriptorSetManager.Initialize(m_device);
    m_vkPipeline.Initialize(m_device, m_vkDescriptorSetManager.TextureSetLayout(), m_vkDescriptorSetManager.MaterialSetLayout());
    Create3DUniformBuffer();
    m_vkPipeline.CreatePipelines(m_vkRenderPass.Get(), m_vkRenderTarget.RenderPass(), m_msaaSamples);
    TraceLog(LogLevel::Info, "VULKAN", "Init step: CreateShaderPipelines");
    CreateShaderPipelines();
    CreateFramebuffers();
    m_vkCommandContext.Initialize(m_device, FindQueueFamilies(m_physicalDevice).graphicsFamily.value());
    CreateFrameVertexIndexBuffers();
    m_batchVertices.reserve(kVkMaxVerticesPerFrame);
    m_batchIndices.reserve(kVkMaxIndicesPerFrame);
    m_batchDrawItems.reserve(256);
    m_frameVertices.reserve(kVkMaxVerticesPerFrame);
    m_frameIndices.reserve(kVkMaxIndicesPerFrame);
    m_frameDrawItems.reserve(256);
    m_main3DBatch.triangleVertices.reserve(kVkMaxVerticesPerFrame / 2);
    m_main3DBatch.lineVertices.reserve(kVkMaxVerticesPerFrame / 2);
    m_frameTriangleVertices3D.reserve(kVkMaxVerticesPerFrame);
    m_frameLineVertices3D.reserve(kVkMaxVerticesPerFrame);
    m_vkFrameManager.Initialize(m_device, m_vkCommandContext.Pool(), kVkMaxFramesInFlight);
    m_vkResources.Initialize(m_device, m_gpuAllocator, m_vkCommandContext.Pool(), m_graphicsQueue,
                             [this](VkDescriptorSet& set) { return AllocateTextureDescriptorSet(set); });
    CreateWhiteTexture();

    TraceLog(LogLevel::Info, "VULKAN", "Vulkan renderer initialized successfully.");
}

void QuarkVkRenderer::Shutdown() {
    if (m_instance == VK_NULL_HANDLE) {
        return;
    }

    TraceLog(LogLevel::Info, "VULKAN", "Shutting down Vulkan renderer...");

    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }

    {
        std::vector<uint32_t> ids;
        ids.reserve(m_renderTargets.size());
        for (const auto& [id, _] : m_renderTargets) { ids.push_back(id); }
        for (uint32_t id : ids) { DestroyRenderTargetInternal(id); }
    }

    m_vkResources.Shutdown();
    m_textureCache.clear();
    m_textureCacheKeys.clear();

    CleanupSwapChain();
    m_vkRenderTarget.Shutdown();
    m_vkFrameManager.Shutdown();
    m_vkShaderCompiler.Shutdown();

    for (auto& frame : m_frames) {

        if (frame.vertexMapped && frame.vertexAllocation != VK_NULL_HANDLE) {
            vmaUnmapMemory(m_gpuAllocator.GetAllocator(), frame.vertexAllocation);
            frame.vertexMapped = nullptr;
        }
        if (frame.vertexBuffer != VK_NULL_HANDLE) {
            m_gpuAllocator.DestroyBuffer(frame.vertexBuffer, frame.vertexAllocation);
            frame.vertexBuffer = VK_NULL_HANDLE;
        }
        if (frame.vertexAllocation != VK_NULL_HANDLE) {
            frame.vertexAllocation = VK_NULL_HANDLE;
        }
        if (frame.vertexMemory != VK_NULL_HANDLE) {
            frame.vertexMemory = VK_NULL_HANDLE;
        }
        frame.vertexCapacity = 0;
        if (frame.indexMapped && frame.indexAllocation != VK_NULL_HANDLE) {
            vmaUnmapMemory(m_gpuAllocator.GetAllocator(), frame.indexAllocation);
            frame.indexMapped = nullptr;
        }
        if (frame.indexBuffer != VK_NULL_HANDLE) {
            m_gpuAllocator.DestroyBuffer(frame.indexBuffer, frame.indexAllocation);
            frame.indexBuffer = VK_NULL_HANDLE;
        }
        if (frame.indexAllocation != VK_NULL_HANDLE) {
            frame.indexAllocation = VK_NULL_HANDLE;
        }
        if (frame.indexMemory != VK_NULL_HANDLE) {
            frame.indexMemory = VK_NULL_HANDLE;
        }
        frame.indexCapacity = 0;
        if (frame.vertexMapped3D && frame.vertex3DAllocation != VK_NULL_HANDLE) {
            vmaUnmapMemory(m_gpuAllocator.GetAllocator(), frame.vertex3DAllocation);
            frame.vertexMapped3D = nullptr;
        }
        if (frame.vertexBuffer3D != VK_NULL_HANDLE) {
            m_gpuAllocator.DestroyBuffer(frame.vertexBuffer3D, frame.vertex3DAllocation);
            frame.vertexBuffer3D = VK_NULL_HANDLE;
        }
        if (frame.vertex3DAllocation != VK_NULL_HANDLE) {
            frame.vertex3DAllocation = VK_NULL_HANDLE;
        }
        if (frame.vertexMemory3D != VK_NULL_HANDLE) {
            frame.vertexMemory3D = VK_NULL_HANDLE;
        }
        frame.vertexCapacity3D = 0;
    }

    ClearMaterialDescriptorCache();

    m_vkPipeline.Shutdown();
    m_vkDescriptorSetManager.Shutdown(m_device);

    if (m_3DDummyBuffer != VK_NULL_HANDLE) {
        if (m_3DDummyMapped != nullptr) {
            vmaUnmapMemory(m_gpuAllocator.GetAllocator(), m_3DDummyAllocation);
            m_3DDummyMapped = nullptr;
        }
        m_gpuAllocator.DestroyBuffer(m_3DDummyBuffer, m_3DDummyAllocation);
        m_3DDummyBuffer = VK_NULL_HANDLE;
    }
    if (m_3DDummyAllocation != VK_NULL_HANDLE) {
        m_3DDummyAllocation = VK_NULL_HANDLE;
    }
    if (m_3DDummyMemory != VK_NULL_HANDLE) {
        m_3DDummyMemory = VK_NULL_HANDLE;
    }

    if (m_vkCommandContext.IsInitialized()) {
        m_vkCommandContext.Shutdown();
    }

    m_gpuAllocator.Shutdown();

    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }

    m_window             = nullptr;
    m_width              = 0;
    m_height             = 0;
    m_drawing            = false;
    m_currentFrame       = 0;
    m_imageIndex         = 0;
    m_whiteTextureId     = 0;
    m_nextRenderTargetId = 1;
    m_activeRenderTargetId = 0;
    m_graphicsQueueFamily = UINT32_MAX;
    m_msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    m_requestedMsaaSamples = 1;
    m_main3DBatch.triangleVertices.clear();
    m_main3DBatch.lineVertices.clear();
    m_frameTriangleVertices3D.clear();
    m_frameLineVertices3D.clear();
    m_frame3DDrawItems.clear();
    TraceLog(LogLevel::Info, "VULKAN", "Vulkan renderer shut down successfully.");
}

void QuarkVkRenderer::BeginDrawing() {
    if (!m_device || m_vkSwapChain.Get() == VK_NULL_HANDLE || m_drawing) {
        return;
    }

    m_activeRenderTargetId = 0;
    if (m_lastFrameCounter == 0) {
        m_lastFrameCounter = SDL_GetPerformanceCounter();
    }

    m_vkFrameManager.BeginFrame(m_currentFrame);

    VkResult result = m_vkFrameManager.AcquireNextImage(m_vkSwapChain.Get(), m_currentFrame, m_imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapChain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire next Vulkan swapchain image.");
    }

    m_drawing = true;
}

void QuarkVkRenderer::EndDrawing() {
    if (!m_drawing || !m_device || m_vkSwapChain.Get() == VK_NULL_HANDLE) return;

    VkCommandBuffer cmd = m_vkFrameManager.GetCommandBuffer(m_currentFrame);

    m_vkFrameManager.ResetFrame(m_currentFrame);

    BuildCombinedFrameGeometry();

    m_batchVertices.clear();
    m_batchIndices.clear();
    m_batchDrawItems.clear();
    for (auto& [id, rt] : m_renderTargets) {
        (void)id;
        rt.vertices.clear();
        rt.indices.clear();
        rt.drawItems.clear();
        rt.triangleVertices3D.clear();
        rt.lineVertices3D.clear();
        rt.drawItems3D.clear();
    }
    m_main3DBatch.triangleVertices.clear();
    m_main3DBatch.lineVertices.clear();
    m_main3DBatch.drawItems.clear();

    if (!UploadFrameGeometry(m_currentFrame)) {
        m_drawing = false;
        return;
    }
    if (!RecordCommandBuffer(cmd, m_imageIndex)) {
        m_drawing = false;
        return;
    }

    if (!m_vkFrameManager.Submit(m_currentFrame, m_graphicsQueue)) {
        throw std::runtime_error("Failed to submit Vulkan command buffer.");
    }

    const std::uint64_t freq = SDL_GetPerformanceFrequency();
    if (m_targetFps > 0) {
        const std::uint64_t targetTicks = freq / static_cast<std::uint64_t>(m_targetFps);
        while (true) {
            const std::uint64_t now = SDL_GetPerformanceCounter();
            const std::uint64_t elapsed = now - m_lastFrameCounter;
            if (elapsed >= targetTicks) {
                break;
            }
            const std::uint64_t remaining = targetTicks - elapsed;
            if (remaining > freq / 500) {
                SDL_Delay(1);
            }
        }
    }

    const std::uint64_t frameEnd = SDL_GetPerformanceCounter();
    m_frameTime = static_cast<float>(frameEnd - m_lastFrameCounter) / static_cast<float>(freq);
    m_lastFrameCounter = frameEnd;

    VkResult presentResult = m_vkFrameManager.Present(m_vkSwapChain.Get(), m_currentFrame, m_imageIndex, m_presentQueue);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR ||
        m_framebufferResized)
    {
        m_framebufferResized = false;
        RecreateSwapChain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to present Vulkan swapchain image.");
    }

    m_currentFrame = (m_currentFrame + 1) % kVkMaxFramesInFlight;
    m_drawing = false;
    m_frameGeometryPending = false;
}

void QuarkVkRenderer::ClearBackground(Color color) {
    if (m_activeRenderTargetId != 0) {
        auto itRt = m_renderTargets.find(m_activeRenderTargetId);
        if (itRt != m_renderTargets.end()) {
            itRt->second.clearColor = color;
            return;
        }
    }
    m_clearColor = color;
}

void QuarkVkRenderer::SetTextureFilterMode(TextureFilterMode mode) {
    gTextureFilterMode = mode;
    m_textureFilterMode = mode;
    m_vkResources.SetTextureSamplingMode(mode, m_textureWrapMode);
}

void QuarkVkRenderer::SetTextureFilter(int filter) {
    gTextureFilterMode = (filter == TEXTURE_FILTER_POINT) ? TextureFilterMode::Nearest : TextureFilterMode::Linear;
    m_textureFilterMode = gTextureFilterMode;
    m_vkResources.SetTextureSamplingMode(m_textureFilterMode, m_textureWrapMode);
}

void QuarkVkRenderer::SetTextureWrap(int wrap) {
    m_textureWrapMode = wrap;
    m_vkResources.SetTextureSamplingMode(m_textureFilterMode, m_textureWrapMode);
}

void QuarkVkRenderer::BeginScissorMode(int x, int y, int width, int height) {
    m_scissorEnabled = true;
    m_scissorRect = VkRect2D{{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}};
}

void QuarkVkRenderer::EndScissorMode() {
    m_scissorEnabled = false;
    m_scissorRect = VkRect2D{{0, 0}, {m_swapChainExtent.width, m_swapChainExtent.height}};
}

void QuarkVkRenderer::SetBlendMode(int mode) {
    if (m_device == VK_NULL_HANDLE) {
        return;
    }

    m_vkPipeline.SetBlendMode(mode);
}

void QuarkVkRenderer::PushMatrix() {
    m_matrixStack.push_back(m_currentMatrix);
}

void QuarkVkRenderer::PopMatrix() {
    if (!m_matrixStack.empty()) {
        m_currentMatrix = m_matrixStack.back();
        m_matrixStack.pop_back();
    }
}

void QuarkVkRenderer::Translate(const Vec3& t) {
    m_currentMatrix = m_currentMatrix * Mat4::translation(t.x, t.y, t.z);
}

void QuarkVkRenderer::Rotate(float angle, const Vec3& axis) {
    if (axis.x == 1.f && axis.y == 0.f && axis.z == 0.f)
        m_currentMatrix = m_currentMatrix * Mat4::rotationX(angle);
    else if (axis.y == 1.f && axis.x == 0.f && axis.z == 0.f)
        m_currentMatrix = m_currentMatrix * Mat4::rotationY(angle);
    else if (axis.z == 1.f && axis.x == 0.f && axis.y == 0.f)
        m_currentMatrix = m_currentMatrix * Mat4::rotationZ(angle);
}

void QuarkVkRenderer::Scale(const Vec3& s) {
    m_currentMatrix = m_currentMatrix * Mat4::scale(s.x, s.y, s.z);
}

void QuarkVkRenderer::MultMatrix(const Mat4& matrix) {
    m_currentMatrix = m_currentMatrix * matrix;
}

const float* QuarkVkRenderer::GetMatrixModelview()  {
    return m_viewMatrix.m;
}

const float* QuarkVkRenderer::GetMatrixProjection() {
    return m_projectionMatrix.m;
}

void QuarkVkRenderer::EnableBackfaceCulling() {
    if (m_vkPipeline.BackfaceCulling() || m_device == VK_NULL_HANDLE) {
        m_vkPipeline.SetBackfaceCulling(true);
        return;
    }

    vkDeviceWaitIdle(m_device);
    m_vkPipeline.SetBackfaceCulling(true);
    CreateShaderPipelines();
}

void QuarkVkRenderer::DisableBackfaceCulling() {
    if (!m_vkPipeline.BackfaceCulling() || m_device == VK_NULL_HANDLE) {
        m_vkPipeline.SetBackfaceCulling(false);
        return;
    }

    vkDeviceWaitIdle(m_device);
    m_vkPipeline.SetBackfaceCulling(false);
    CreateShaderPipelines();
}

void QuarkVkRenderer::RefreshViewport() {
    m_framebufferResized = true;
}

VkDescriptorSet QuarkVkRenderer::GetTextureDescriptorSet(uint32_t textureId) const {
    return m_vkResources.DescriptorSet(textureId);
}

void QuarkVkRenderer::CreateInstance() {
    if (m_instance != VK_NULL_HANDLE) return;

    unsigned int extensionCount = 0;
    const char* const* extensionsData = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (!extensionsData) {
        throw std::runtime_error("SDL_Vulkan_GetInstanceExtensions failed.");
    }
    std::vector<const char*> extensions(extensionsData, extensionsData + extensionCount);

#ifdef __APPLE__
    extensions.push_back(
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    );

    extensions.push_back(
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
    );
#endif


#ifdef _DEBUG
    const char* validationLayer =
        "VK_LAYER_KHRONOS_validation";
#endif

    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "QuarkCore Vulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "QuarkCore";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo        = &appInfo;
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

#ifdef _DEBUG
    createInfo.enabledLayerCount = 1;
    createInfo.ppEnabledLayerNames = &validationLayer;
#else
    createInfo.enabledLayerCount = 0;
#endif

#ifdef __APPLE__
    createInfo.flags |=
        VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance.");
    }
    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Vulkan %d.%d.%d instance created.", VK_VERSION_MAJOR(appInfo.apiVersion), VK_VERSION_MINOR(appInfo.apiVersion), VK_VERSION_PATCH(appInfo.apiVersion)));

#ifdef __APPLE__
    TraceLog(
        LogLevel::Info,
        "VULKAN",
        "MoltenVK compatibility enabled."
    );
#endif
}

void QuarkVkRenderer::CreateSurface() {
    if (m_surface != VK_NULL_HANDLE) return;
    if (!SDL_Vulkan_CreateSurface(m_window, m_instance, nullptr, &m_surface)) {
        throw std::runtime_error("Failed to create Vulkan surface from SDL window.");
    }
    TraceLog(LogLevel::Info, "VULKAN", "Vulkan surface created.");
}

void QuarkVkRenderer::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        TraceLog(LogLevel::Error, "VULKAN", "Failed to find GPUs with Vulkan support.");
        throw std::runtime_error("Failed to find GPUs with Vulkan support.");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Found %u physical device(s) with Vulkan support:", deviceCount));

    for (uint32_t i = 0; i < deviceCount; ++i) {
        VkPhysicalDevice device = devices[i];
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(device, &memProps);
        uint64_t vramBytes = 0;
        uint64_t hostBytes = 0;
        for (uint32_t h = 0; h < memProps.memoryHeapCount; ++h) {
            if (memProps.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                vramBytes += memProps.memoryHeaps[h].size;
            } else {
                hostBytes += memProps.memoryHeaps[h].size;
            }
        }
        double vramGb = static_cast<double>(vramBytes) / (1024.0 * 1024.0 * 1024.0);
        double hostGb = static_cast<double>(hostBytes) / (1024.0 * 1024.0 * 1024.0);

        const char* devType = GetVulkanDeviceTypeString(props.deviceType);
        const char* vendorName = GetVulkanVendorName(props.vendorID);
        std::string driverVer = FormatVulkanDriverVersion(props.vendorID, props.driverVersion);
        bool suitable = IsDeviceSuitable(device);

        TraceLog(LogLevel::Info, "VULKAN", TextFormat("  [%u] GPU: %s (%s)", i, props.deviceName, devType));
        TraceLog(LogLevel::Info, "VULKAN", TextFormat("      Vendor: %s (0x%04X), Device ID: 0x%04X, Driver: %s",
            vendorName, props.vendorID, props.deviceID, driverVer.c_str()));
        TraceLog(LogLevel::Info, "VULKAN", TextFormat("      Vulkan API: %d.%d.%d, VRAM: %.2f GB, Shared RAM: %.2f GB (Suitable: %s)",
            VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion),
            vramGb, hostGb, suitable ? "Yes" : "No"));

        if (m_physicalDevice == VK_NULL_HANDLE && suitable) {
            m_physicalDevice = device;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        TraceLog(LogLevel::Error, "VULKAN", "Failed to find a suitable Vulkan physical device.");
        throw std::runtime_error("Failed to find a suitable Vulkan physical device.");
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Selected physical device: %s (%s, Vendor: %s)",
        props.deviceName, GetVulkanDeviceTypeString(props.deviceType), GetVulkanVendorName(props.vendorID)));

    const VkPhysicalDeviceLimits& lim = props.limits;
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Limits: Max 2D Image: %ux%u, Max 3D Image: %u, Max Cube: %u, Max Layers: %u",
        lim.maxImageDimension2D, lim.maxImageDimension3D, lim.maxImageDimensionCube, lim.maxImageArrayLayers));
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Limits: Max UBO Range: %u bytes, Max SSBO Range: %u bytes, Max Push Constants: %u bytes",
        lim.maxUniformBufferRange, lim.maxStorageBufferRange, lim.maxPushConstantsSize));
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Limits: Max Sampler Anisotropy: %.1fx, Max Color Attachments: %u, Max Bound Sets: %u",
        lim.maxSamplerAnisotropy, lim.maxColorAttachments, lim.maxBoundDescriptorSets));
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Limits: Max Vertex Input Attribs: %u, Max Vertex Bindings: %u, Max Memory Allocations: %u",
        lim.maxVertexInputAttributes, lim.maxVertexInputBindings, lim.maxMemoryAllocationCount));
}

void QuarkVkRenderer::CreateLogicalDevice() {
    VkQueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    m_vkDevice.Initialize(m_instance, m_physicalDevice, m_surface);
    m_vkDevice.CreateLogicalDevice(indices, kDeviceExtensions);
    m_device = m_vkDevice.Get();
    m_graphicsQueue = m_vkDevice.GraphicsQueue();
    m_presentQueue = m_vkDevice.PresentQueue();
    m_graphicsQueueFamily = m_vkDevice.GraphicsQueueFamily();

    if (!m_gpuAllocator.Initialize(m_instance, m_physicalDevice, m_device)) {
        throw std::runtime_error("Failed to initialize Vulkan GPU allocator.");
    }

    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Logical device created successfully (Graphics Queue: #%u, Present Queue: #%u, Extensions: %zu).",
        indices.graphicsFamily.value(), indices.presentFamily.value(), kDeviceExtensions.size()));
}

void QuarkVkRenderer::CreateSwapChain() {
    VkSwapChainSupportDetails details = QuerySwapChainSupport(m_physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(details.formats);
    VkPresentModeKHR   presentMode   = ChooseSwapPresentMode(details.presentModes);
    VkQueueFamilyIndices indices    = FindQueueFamilies(m_physicalDevice);

    m_vkSwapChain.Initialize(m_device, m_physicalDevice, m_surface,
                             m_width, m_height, details, indices, surfaceFormat, presentMode);

    m_swapChainImageFormat = m_vkSwapChain.GetImageFormat();
    m_swapChainExtent = m_vkSwapChain.GetExtent();

    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Swapchain created: %ux%u, Images: %u, Format: %s (%d), PresentMode: %s, MSAA: %dx",
        m_swapChainExtent.width, m_swapChainExtent.height, m_vkSwapChain.GetImageCount(),
        GetVkFormatString(m_swapChainImageFormat), m_swapChainImageFormat,
        GetPresentModeString(presentMode), m_requestedMsaaSamples));
}

void QuarkVkRenderer::CreateRenderPass() {
    m_depthFormat = FindDepthFormat();
    m_vkRenderPass.Initialize(m_device, m_swapChainImageFormat, m_depthFormat, m_msaaSamples > VK_SAMPLE_COUNT_1_BIT, false);
    TraceLog(LogLevel::Trace, "VULKAN", "Swapchain render pass created.");
}

void QuarkVkRenderer::Create3DUniformBuffer() {
    {
        VkDeviceMemory dummyMemory = VK_NULL_HANDLE;
        if (!CreateBuffer(4096,
                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          VMA_MEMORY_USAGE_AUTO,
                          m_3DDummyBuffer, m_3DDummyAllocation, dummyMemory,
                          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
            throw std::runtime_error("Failed to create Vulkan 3D uniform buffer.");
        }
    }
    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(m_gpuAllocator.GetAllocator(), m_3DDummyAllocation, &allocInfo);
    m_3DDummyMemory = allocInfo.deviceMemory;
    if (vmaMapMemory(m_gpuAllocator.GetAllocator(), m_3DDummyAllocation, &m_3DDummyMapped) != VK_SUCCESS) {
        throw std::runtime_error("Failed to map Vulkan 3D uniform buffer.");
    }
    std::memset(m_3DDummyMapped, 0, 4096);
    const Mat4 identity = Mat4::identity();
    std::memcpy(static_cast<char*>(m_3DDummyMapped) + 0, identity.m, sizeof(identity.m));
    std::memcpy(static_cast<char*>(m_3DDummyMapped) + 64, identity.m, sizeof(identity.m));
    std::memcpy(static_cast<char*>(m_3DDummyMapped) + 128, identity.m, sizeof(identity.m));
}

bool QuarkVkRenderer::Allocate3DDescriptorSet(VkDescriptorSet& outSet, VkDescriptorPool& outPool) {
    return m_vkDescriptorSetManager.Allocate3DDescriptorSet(m_device, outSet, outPool);
}

uint64_t QuarkVkRenderer::ComputeMaterialCacheKey(const Material& material) const {
    uint64_t hash = 14695981039346656037ull;
    const auto mix = [&](uint64_t value) {
        hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    };

    const auto mixColor = [&](const Color& color) {
        mix(static_cast<uint64_t>(color.r));
        mix(static_cast<uint64_t>(color.g));
        mix(static_cast<uint64_t>(color.b));
        mix(static_cast<uint64_t>(color.a));
    };

    const auto mixTextureId = [&](uint32_t textureId) {
        mix(static_cast<uint64_t>(textureId));
    };

    if (material.maps) {
        for (int i = 0; i <= MATERIAL_MAP_BRDF; ++i) {
            const MaterialMap& map = material.maps[i];
            mixColor(map.color);
            mix(static_cast<uint64_t>(map.texture.valid ? 1u : 0u));
            mixTextureId(map.texture.id);
            mix(static_cast<uint64_t>(std::lround(map.value * 1000.0f)));
        }
    } else {
        mix(0ull);
    }

    for (int i = 0; i < 4; ++i) {
        mix(static_cast<uint64_t>(std::lround(material.params[i] * 1000.0f)));
    }

    return hash;
}

VkDescriptorSet QuarkVkRenderer::CreateMaterialDescriptorSet(const Material& material) {
    const uint64_t cacheKey = ComputeMaterialCacheKey(material);
    auto it = m_materialCache.find(cacheKey);
    if (it != m_materialCache.end() && it->second.descriptorSet != VK_NULL_HANDLE) {
        return it->second.descriptorSet;
    }

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    if (!Allocate3DDescriptorSet(descriptorSet, descriptorPool)) return VK_NULL_HANDLE;

    const VkTextureData* whiteTex = m_vkResources.Get(m_whiteTextureId);
    if (whiteTex == nullptr) {
        vkFreeDescriptorSets(m_device, descriptorPool, 1, &descriptorSet);
        return VK_NULL_HANDLE;
    }

    VkDescriptorImageInfo whiteImage{};
    whiteImage.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    whiteImage.imageView = whiteTex->view;
    whiteImage.sampler = whiteTex->sampler;

    std::array<VkDescriptorImageInfo, 7> images{};
    images.fill(whiteImage);
    const auto setFallbackImage = [&](size_t index, uint32_t textureId) {
        const VkTextureData* texture = m_vkResources.Get(textureId);
        if (texture != nullptr) {
            images[index].imageView = texture->view;
            images[index].sampler = texture->sampler;
        }
    };
    setFallbackImage(1, m_blackTextureId);
    setFallbackImage(2, m_flatNormalTextureId);
    setFallbackImage(5, m_blackTextureId);
    const std::array<int, 6> mapIndices = {
        MATERIAL_MAP_ALBEDO, MATERIAL_MAP_METALNESS, MATERIAL_MAP_NORMAL,
        MATERIAL_MAP_ROUGHNESS, MATERIAL_MAP_OCCLUSION, MATERIAL_MAP_EMISSION
    };
    for (size_t i = 0; i < mapIndices.size(); ++i) {
        if (!material.maps) continue;
        const MaterialMap& map = material.maps[mapIndices[i]];
        const VkTextureData* texture = m_vkResources.Get(map.texture.id);
        if (map.texture.valid && texture != nullptr) {
            images[i].imageView = texture->view;
            images[i].sampler = texture->sampler;
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
    if (material.maps) {
        for (uint32_t i = 0; i < 4; ++i) {
            const MaterialMap& map = material.maps[MATERIAL_MAP_HEIGHT + i];
            const VkTextureData* texture = m_vkResources.Get(map.texture.id);
            if (map.texture.valid && texture != nullptr) {
                shadowImages[i].imageView = texture->view;
                shadowImages[i].sampler = texture->sampler;
            }
        }
    }

    std::array<VkWriteDescriptorSet, 11> writes{};
    writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 0, 0, 1,
                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &matrices, nullptr };
    writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 1, 0, 1,
                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &images[0], nullptr, nullptr };
    writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 2, 0, 4,
                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, shadowImages.data(), nullptr, nullptr };
    writes[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 3, 0, 1,
                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &shadowBuffer, nullptr };
    writes[4] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 4, 0, 1,
                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &lightBuffer, nullptr };
    for (uint32_t i = 0; i < 6; ++i) {
        writes[5 + i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, 5 + i, 0, 1,
                          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &images[i + 1], nullptr, nullptr };
    }
    vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    VkMaterialCacheEntry entry{};
    entry.descriptorSet = descriptorSet;
    entry.descriptorPool = descriptorPool;
    entry.key = cacheKey;
    if (material.maps) {
        for (int i = 0; i <= MATERIAL_MAP_BRDF; ++i) {
            entry.textureIds.push_back(material.maps[i].texture.id);
        }
    }
    m_materialCache[cacheKey] = entry;
    return descriptorSet;
}

void QuarkVkRenderer::ClearMaterialDescriptorCache() {
    for (auto& [key, entry] : m_materialCache) {
        if (entry.descriptorSet != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(m_device, entry.descriptorPool, 1, &entry.descriptorSet);
        }
    }
    m_materialCache.clear();
}

bool QuarkVkRenderer::AllocateTextureDescriptorSet(VkDescriptorSet& outSet) {
    return m_vkDescriptorSetManager.AllocateTextureDescriptorSet(m_device, outSet);
}

void QuarkVkRenderer::CreateFramebuffers() {
    m_vkSwapChain.CreateAttachmentResources(m_device, m_gpuAllocator, m_depthFormat, m_msaaSamples);

    m_vkFramebufferManager.Initialize(m_device);
    m_vkFramebufferManager.CreateSwapChainFramebuffers(m_device,
                                                      m_vkRenderPass.Get(),
                                                      m_vkSwapChain.ImageViews(),
                                                      m_vkSwapChain.GetExtent(),
                                                      m_vkSwapChain.MsaaColorImageView(),
                                                      m_msaaSamples,
                                                      m_vkSwapChain.DepthImageViews());
    m_swapChainFramebuffers = m_vkFramebufferManager.Framebuffers();
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Created %zu framebuffers.", m_swapChainFramebuffers.size()));
}

bool QuarkVkRenderer::RecreateRenderTargetFramebuffers() {
    for (const auto& [id, rt] : m_renderTargets) {
        const VkTextureData* tex = m_vkResources.Get(rt.textureId);
        if (tex == nullptr) continue;

        if (!m_vkRenderTarget.CreateTarget(id, tex->view, rt.width, rt.height)) {
            return false;
        }
    }
    return true;
}

void QuarkVkRenderer::CreateFrameVertexIndexBuffers() {
    const VkDeviceSize vertexBufSize = sizeof(VkBatchVertex) * kVkMaxVerticesPerFrame;
    const VkDeviceSize indexBufSize  = sizeof(uint32_t)      * kVkMaxIndicesPerFrame;
    const VkDeviceSize vertexBufSize3D = sizeof(Vk3DVertex)  * kVkMaxVerticesPerFrame;

    for (auto& frame : m_frames) {
        {
            VkDeviceMemory dummyMemory = VK_NULL_HANDLE;
            if (!CreateBuffer(vertexBufSize,
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              VMA_MEMORY_USAGE_AUTO,
                              frame.vertexBuffer, frame.vertexAllocation, dummyMemory,
                              VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                              VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
                throw std::runtime_error("Failed to create per-frame Vulkan vertex buffer.");
            }
        }
        VmaAllocationInfo vertexInfo{};
        vmaGetAllocationInfo(m_gpuAllocator.GetAllocator(), frame.vertexAllocation, &vertexInfo);
        frame.vertexMemory = vertexInfo.deviceMemory;
        if (vmaMapMemory(m_gpuAllocator.GetAllocator(), frame.vertexAllocation, &frame.vertexMapped) != VK_SUCCESS) {
            throw std::runtime_error("Failed to map per-frame Vulkan vertex buffer.");
        }
        frame.vertexCapacity = vertexBufSize;

        {
            VkDeviceMemory dummyMemory = VK_NULL_HANDLE;
            if (!CreateBuffer(indexBufSize,
                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                              VMA_MEMORY_USAGE_AUTO,
                              frame.indexBuffer, frame.indexAllocation, dummyMemory,
                              VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                              VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
                throw std::runtime_error("Failed to create per-frame Vulkan index buffer.");
            }
        }
        VmaAllocationInfo indexInfo{};
        vmaGetAllocationInfo(m_gpuAllocator.GetAllocator(), frame.indexAllocation, &indexInfo);
        frame.indexMemory = indexInfo.deviceMemory;
        if (vmaMapMemory(m_gpuAllocator.GetAllocator(), frame.indexAllocation, &frame.indexMapped) != VK_SUCCESS) {
            throw std::runtime_error("Failed to map per-frame Vulkan index buffer.");
        }
        frame.indexCapacity = indexBufSize;

        {
            VkDeviceMemory dummyMemory = VK_NULL_HANDLE;
            if (!CreateBuffer(vertexBufSize3D,
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              VMA_MEMORY_USAGE_AUTO,
                              frame.vertexBuffer3D, frame.vertex3DAllocation, dummyMemory,
                              VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                              VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
                throw std::runtime_error("Failed to create per-frame Vulkan 3D vertex buffer.");
            }
        }
        VmaAllocationInfo vertex3DInfo{};
        vmaGetAllocationInfo(m_gpuAllocator.GetAllocator(), frame.vertex3DAllocation, &vertex3DInfo);
        frame.vertexMemory3D = vertex3DInfo.deviceMemory;
        if (vmaMapMemory(m_gpuAllocator.GetAllocator(), frame.vertex3DAllocation, &frame.vertexMapped3D) != VK_SUCCESS) {
            throw std::runtime_error("Failed to map per-frame Vulkan 3D vertex buffer.");
        }
        frame.vertexCapacity3D = vertexBufSize3D;
    }
    TraceLog(LogLevel::Trace, "VULKAN", "Per-frame vertex/index buffers created.");
}

void QuarkVkRenderer::CreateWhiteTexture() {
    const unsigned char white[4] = {255, 255, 255, 255};
    m_whiteTextureId = m_vkResources.CreateTextureFromRGBA(white, 1, 1);
    if (m_whiteTextureId == 0) {
        throw std::runtime_error("Failed to create Vulkan white fallback texture.");
    }
    const unsigned char black[4] = {0, 0, 0, 255};
    m_blackTextureId = m_vkResources.CreateTextureFromRGBA(black, 1, 1);
    if (m_blackTextureId == 0) {
        throw std::runtime_error("Failed to create Vulkan black fallback texture.");
    }
    const unsigned char flatNormal[4] = {128, 128, 255, 255};
    m_flatNormalTextureId = m_vkResources.CreateTextureFromRGBA(flatNormal, 1, 1);
    if (m_flatNormalTextureId == 0) {
        throw std::runtime_error("Failed to create Vulkan normal fallback texture.");
    }
    Material whiteMaterial{};
    m_white3DDescriptorSet = CreateMaterialDescriptorSet(whiteMaterial);
    if (m_white3DDescriptorSet == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to create Vulkan white 3D descriptor set.");
    }
    TraceLog(LogLevel::Trace, "VULKAN", "White fallback texture created.");
}

void QuarkVkRenderer::CreateShaderPipelines() {
    m_vkShaderCompiler.FreePipelines();
    for (auto& [id, program] : m_vkShaderCompiler.Programs()) {
        (void)id;
        if (!program.supports3D) {
            program.pipeline = m_vkPipeline.Create2DPipeline(m_vkRenderPass.Get(),
                                                             program.vertexModule,
                                                             program.fragmentModule);
        } else {
            program.pipeline3D = m_vkPipeline.Create3DPipeline(m_vkRenderPass.Get(),
                                                               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                               program.vertexModule,
                                                               program.fragmentModule);
            if (m_vkRenderTarget.RenderPass() != VK_NULL_HANDLE) {
                program.pipeline3DOffscreen = m_vkPipeline.Create3DPipeline(m_vkRenderTarget.RenderPass(),
                                                                            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                                            program.vertexModule,
                                                                            program.fragmentModule);
            }
        }
    }
}
void QuarkVkRenderer::RecreateSwapChain() {
    TraceLog(LogLevel::Info, "VULKAN", "Recreating swapchain...");
    vkDeviceWaitIdle(m_device);

    CleanupSwapChain();

    CreateSwapChain();
    CreateRenderPass();
    m_vkRenderTarget.Initialize(m_device, m_gpuAllocator, m_swapChainImageFormat, m_depthFormat);
    m_vkRenderTarget.CreateRenderPass();
    m_vkPipeline.CreatePipelines(m_vkRenderPass.Get(), m_vkRenderTarget.RenderPass(), m_msaaSamples);
    CreateShaderPipelines();
    CreateFramebuffers();
    RecreateRenderTargetFramebuffers();
}

void QuarkVkRenderer::CleanupSwapChain() {
    TraceLog(LogLevel::Trace, "VULKAN", "Cleaning up swapchain resources...");

    m_vkRenderTarget.DestroyFramebuffers();

    m_vkFramebufferManager.Shutdown(m_device);
    m_swapChainFramebuffers.clear();

    m_vkPipeline.DestroyPipelines();
    m_vkShaderCompiler.FreePipelines();
    m_vkRenderTarget.DestroyRenderPass();
    m_vkRenderPass.Shutdown();

    m_vkSwapChain.Shutdown(m_device);
}

VkQueueFamilyIndices QuarkVkRenderer::FindQueueFamilies(VkPhysicalDevice device) const {
    VkQueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }
        if (indices.isComplete()) break;
    }
    return indices;
}

VkSwapChainSupportDetails QuarkVkRenderer::QuerySwapChainSupport(VkPhysicalDevice device) const {
    VkSwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
    }
    return details;
}

bool QuarkVkRenderer::IsDeviceSuitable(VkPhysicalDevice device) const {
    VkQueueFamilyIndices indices = FindQueueFamilies(device);
    if (!indices.isComplete()) return false;

    VkSwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device);
    return !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
}

VkSurfaceFormatKHR QuarkVkRenderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const {
    for (const auto& fmt : formats) {
        if ((fmt.format == VK_FORMAT_B8G8R8A8_SRGB || fmt.format == VK_FORMAT_B8G8R8A8_UNORM) &&
            fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return fmt;
        }
    }
    return formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
}

VkPresentModeKHR QuarkVkRenderer::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& modes) const {
    const bool vsync = m_vsyncExplicitlySet ? m_vsync : (m_targetFps != 0);
    if (!vsync) {
        for (const auto& mode : modes) {
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) return mode;
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

void QuarkVkRenderer::SetTargetFPS(int fps) {
    m_targetFps = fps;
    if (!m_vsyncExplicitlySet && m_vkSwapChain.Get() != VK_NULL_HANDLE) {
        RecreateSwapChain();
    }
}

bool QuarkVkRenderer::SetVSync(bool enabled) {
    if (m_vsync == enabled && m_vsyncExplicitlySet) {
        return true;
    }
    m_vsync = enabled;
    m_vsyncExplicitlySet = true;
    if (m_vkSwapChain.Get() != VK_NULL_HANDLE) {
        RecreateSwapChain();
    }
    return true;
}

VkExtent2D QuarkVkRenderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps) const {
    if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;

    VkExtent2D actual = { static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height) };
    actual.width  = std::max(caps.minImageExtent.width,  std::min(caps.maxImageExtent.width,  actual.width));
    actual.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, actual.height));
    return actual;
}

uint32_t QuarkVkRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable Vulkan memory type.");
}

VkFormat QuarkVkRenderer::FindSupportedFormat(const std::vector<VkFormat>& candidates,
                                              VkImageTiling tiling,
                                              VkFormatFeatureFlags features) const {
    for (VkFormat format : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR &&
            (props.linearTilingFeatures & features) == features) {
            return format;
        }
        if (tiling == VK_IMAGE_TILING_OPTIMAL &&
            (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("Failed to find supported Vulkan format.");
}

VkFormat QuarkVkRenderer::FindDepthFormat() const {
    return FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

bool QuarkVkRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                    VkMemoryPropertyFlags props,
                                    VkBuffer& outBuffer, VkDeviceMemory& outMemory) {
    VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO;
    VmaAllocationCreateFlags flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
        VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if ((props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
        memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
        flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    }

    VmaAllocation allocation = VK_NULL_HANDLE;
    if (!CreateBuffer(size, usage, memoryUsage, outBuffer, allocation, outMemory, flags)) {
        return false;
    }
    return true;
}

bool QuarkVkRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                    VmaMemoryUsage memoryUsage,
                                    VkBuffer& outBuffer, VmaAllocation& outAllocation,
                                    VkDeviceMemory& outMemory,
                                    VmaAllocationCreateFlags flags) {
    outBuffer = VK_NULL_HANDLE;
    outAllocation = VK_NULL_HANDLE;
    outMemory = VK_NULL_HANDLE;

    if (!m_gpuAllocator.CreateBuffer(size, usage, memoryUsage, outBuffer, outAllocation, flags)) {
        return false;
    }

    VmaAllocationInfo allocationInfo{};
    vmaGetAllocationInfo(m_gpuAllocator.GetAllocator(), outAllocation, &allocationInfo);
    outMemory = allocationInfo.deviceMemory;
    return true;
}

bool QuarkVkRenderer::EnsureMappedBufferCapacity(VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped,
                                                 VkDeviceSize& capacity, VkDeviceSize required,
                                                 VkBufferUsageFlags usage) {
    VmaAllocation allocation = VK_NULL_HANDLE;
    return EnsureMappedBufferCapacity(buffer, memory, allocation, mapped, capacity, required, usage);
}

bool QuarkVkRenderer::EnsureMappedBufferCapacity(VkBuffer& buffer, VkDeviceMemory& memory, VmaAllocation& allocation, void*& mapped,
                                                 VkDeviceSize& capacity, VkDeviceSize required,
                                                 VkBufferUsageFlags usage) {
    if (required <= capacity) {
        return true;
    }

    VkDeviceSize newCapacity = capacity > 0 ? capacity : 4096;
    while (newCapacity < required) {
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
                      VMA_ALLOCATION_CREATE_MAPPED_BIT)) {
        return false;
    }

    void* newMapped = nullptr;
    if (vmaMapMemory(m_gpuAllocator.GetAllocator(), newAllocation, &newMapped) != VK_SUCCESS) {
        m_gpuAllocator.DestroyBuffer(newBuffer, newAllocation);
        return false;
    }

    vkDeviceWaitIdle(m_device);
    if (mapped != nullptr && allocation != VK_NULL_HANDLE && memory != VK_NULL_HANDLE) {
        vmaUnmapMemory(m_gpuAllocator.GetAllocator(), allocation);
    }
    if (buffer != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE) {
        m_gpuAllocator.DestroyBuffer(buffer, allocation);
    }

    buffer = newBuffer;
    allocation = newAllocation;
    memory = newMemory;
    mapped = newMapped;
    capacity = newCapacity;
    return true;
}

VkSampleCountFlagBits QuarkVkRenderer::GetSampleCountForSamples(int samples) const {
    if (samples <= 1 || m_physicalDevice == VK_NULL_HANDLE) return VK_SAMPLE_COUNT_1_BIT;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts &
                                props.limits.framebufferDepthSampleCounts;

    VkSampleCountFlagBits requested = VK_SAMPLE_COUNT_1_BIT;
    if (samples >= 8) requested = VK_SAMPLE_COUNT_8_BIT;
    else if (samples >= 4) requested = VK_SAMPLE_COUNT_4_BIT;
    else if (samples >= 2) requested = VK_SAMPLE_COUNT_2_BIT;

    if (counts & requested) {
        return requested;
    }
    if (samples >= 8 && (counts & VK_SAMPLE_COUNT_4_BIT)) return VK_SAMPLE_COUNT_4_BIT;
    if (samples >= 4 && (counts & VK_SAMPLE_COUNT_2_BIT)) return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

void QuarkVkRenderer::SetMSAASamples(int samples) {
    m_requestedMsaaSamples = (samples == 2 || samples == 4 || samples == 8) ? samples : 1;
    if (m_physicalDevice != VK_NULL_HANDLE) {
        VkSampleCountFlagBits newSamples = GetSampleCountForSamples(m_requestedMsaaSamples);
        if (newSamples != m_msaaSamples) {
            m_msaaSamples = newSamples;
            if (m_vkSwapChain.Get() != VK_NULL_HANDLE) {
                RecreateSwapChain();
            }
        }
    }
}

IRenderTexture QuarkVkRenderer::CreateRenderTargetInternal(int width, int height) {
    if (width <= 0 || height <= 0 ||
        m_device == VK_NULL_HANDLE ||
        m_vkRenderTarget.RenderPass() == VK_NULL_HANDLE) {
        return IRenderTexture{};
    }

    VkTextureData tex{};
    tex.width  = static_cast<uint32_t>(width);
    tex.height = static_cast<uint32_t>(height);
    tex.isRenderTarget = true;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.extent        = { tex.width, tex.height, 1 };
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.format        = m_swapChainImageFormat;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    tex.allocation = VK_NULL_HANDLE;
    if (!m_gpuAllocator.CreateImage(imageInfo,
                                    VMA_MEMORY_USAGE_AUTO,
                                    tex.image,
                                    tex.allocation,
                                    VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT)) {
        return IRenderTexture{};
    }

    VmaAllocationInfo rtAllocInfo{};
    vmaGetAllocationInfo(m_gpuAllocator.GetAllocator(), tex.allocation, &rtAllocInfo);
    tex.memory = rtAllocInfo.deviceMemory;

    if (!m_vkResources.TransitionImageLayout(tex.image, imageInfo.format,
                                         VK_IMAGE_LAYOUT_UNDEFINED,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        m_gpuAllocator.DestroyImage(tex.image, tex.allocation);
        tex.image = VK_NULL_HANDLE;
        tex.allocation = VK_NULL_HANDLE;
        tex.memory = VK_NULL_HANDLE;
        return IRenderTexture{};
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = tex.image;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = imageInfo.format;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &tex.view) != VK_SUCCESS) {
        m_gpuAllocator.DestroyImage(tex.image, tex.allocation);
        tex.image = VK_NULL_HANDLE;
        tex.allocation = VK_NULL_HANDLE;
        tex.memory = VK_NULL_HANDLE;
        return IRenderTexture{};
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter    = VK_FILTER_LINEAR;
    samplerInfo.minFilter    = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod       = 1.0f;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &tex.sampler) != VK_SUCCESS) {
        vkDestroyImageView(m_device, tex.view, nullptr);
        m_gpuAllocator.DestroyImage(tex.image, tex.allocation);
        tex.image = VK_NULL_HANDLE;
        tex.allocation = VK_NULL_HANDLE;
        tex.memory = VK_NULL_HANDLE;
        return IRenderTexture{};
    }

    if (!AllocateTextureDescriptorSet(tex.descriptorSet)) {
        vkDestroySampler(m_device, tex.sampler, nullptr);
        vkDestroyImageView(m_device, tex.view, nullptr);
        m_gpuAllocator.DestroyImage(tex.image, tex.allocation);
        tex.image = VK_NULL_HANDLE;
        tex.allocation = VK_NULL_HANDLE;
        tex.memory = VK_NULL_HANDLE;
        return IRenderTexture{};
    }

    VkDescriptorImageInfo imageDescriptor{};
    imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageDescriptor.imageView   = tex.view;
    imageDescriptor.sampler     = tex.sampler;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = tex.descriptorSet;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imageDescriptor;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

    const uint32_t textureId = m_vkResources.Import(tex);

    const uint32_t rtId = m_nextRenderTargetId++;
    if (!m_vkRenderTarget.CreateTarget(rtId, tex.view, tex.width, tex.height)) {
        m_vkResources.DestroyTexture(textureId);
        return IRenderTexture{};
    }

    VkRenderTargetData rt{};
    rt.textureId   = textureId;
    rt.width       = tex.width;
    rt.height      = tex.height;
    rt.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    m_renderTargets[rtId] = rt;

    TraceLog(LogLevel::Info, "RENDER_TARGET", TextFormat("[Vulkan] Render target created: %ux%u (Target ID: %u, Color Tex ID: %u, Depth Buffer: yes)",
        tex.width, tex.height, rtId, textureId));
    return IRenderTexture{ rtId, {}, 0 };
}

void QuarkVkRenderer::DestroyRenderTargetInternal(uint32_t renderTargetId) {
    auto it = m_renderTargets.find(renderTargetId);
    if (it == m_renderTargets.end()) return;

    TraceLog(LogLevel::Info, "RENDER_TARGET", TextFormat("[Vulkan] Render target destroyed (Target ID: %u, %ux%u)",
        renderTargetId, it->second.width, it->second.height));

    const uint32_t textureId = it->second.textureId;
    m_vkRenderTarget.DestroyTarget(renderTargetId);
    m_renderTargets.erase(it);

    if (m_activeRenderTargetId == renderTargetId) {
        m_activeRenderTargetId = 0;
    }
    m_vkResources.DestroyTexture(textureId);
}

bool QuarkVkRenderer::ReadBinaryFile(const char* path, std::vector<char>& outData) const {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) return false;
    const std::streamsize fileSize = file.tellg();
    outData.resize(static_cast<size_t>(fileSize));
    file.seekg(0);
    file.read(outData.data(), fileSize);
    return true;
}

void QuarkVkRenderer::BuildCombinedFrameGeometry() {
    m_frameVertices.clear();
    m_frameIndices.clear();
    m_frameDrawItems.clear();
    m_framePasses.clear();
    m_frameTriangleVertices3D.clear();
    m_frameLineVertices3D.clear();
    m_frame3DShaderProgramId = 0;

    for (auto& [id, rt] : m_renderTargets) {
        const bool has2D = !rt.drawItems.empty();
        const bool has3D = !rt.triangleVertices3D.empty() || !rt.lineVertices3D.empty();
        if (!has2D && !has3D) continue;

        const uint32_t baseVertex = static_cast<uint32_t>(m_frameVertices.size());
        const uint32_t baseIndex  = static_cast<uint32_t>(m_frameIndices.size());
        const uint32_t firstDraw  = static_cast<uint32_t>(m_frameDrawItems.size());
        const uint32_t triFirst   = static_cast<uint32_t>(m_frameTriangleVertices3D.size());
        const uint32_t lineFirst  = static_cast<uint32_t>(m_frameLineVertices3D.size());
        const uint32_t first3DDrawItem = static_cast<uint32_t>(m_frame3DDrawItems.size());

        m_frameVertices.insert(m_frameVertices.end(), rt.vertices.begin(), rt.vertices.end());
        for (uint32_t idx : rt.indices) {
            m_frameIndices.push_back(baseVertex + idx);
        }
        for (VkDrawItem item : rt.drawItems) {
            item.firstIndex += baseIndex;
            m_frameDrawItems.push_back(item);
        }

        m_frameTriangleVertices3D.insert(m_frameTriangleVertices3D.end(),
                                         rt.triangleVertices3D.begin(), rt.triangleVertices3D.end());
        m_frameLineVertices3D.insert(m_frameLineVertices3D.end(),
                                     rt.lineVertices3D.begin(), rt.lineVertices3D.end());
        for (Vk3DDrawItem item : rt.drawItems3D) {
            item.firstVertex += triFirst;
            m_frame3DDrawItems.push_back(item);
        }

        m_framePasses.push_back(VkFramePass{
            id,
            firstDraw,
            static_cast<uint32_t>(m_frameDrawItems.size() - firstDraw),
            rt.width,
            rt.height,
            triFirst,
            static_cast<uint32_t>(rt.triangleVertices3D.size()),
            lineFirst,
            static_cast<uint32_t>(rt.lineVertices3D.size()),
            first3DDrawItem,
            static_cast<uint32_t>(m_frame3DDrawItems.size() - first3DDrawItem)
        });
    }

    {
        const uint32_t baseVertex = static_cast<uint32_t>(m_frameVertices.size());
        const uint32_t baseIndex  = static_cast<uint32_t>(m_frameIndices.size());
        const uint32_t firstDraw  = static_cast<uint32_t>(m_frameDrawItems.size());
        const uint32_t triFirst   = static_cast<uint32_t>(m_frameTriangleVertices3D.size());
        const uint32_t lineFirst  = static_cast<uint32_t>(m_frameLineVertices3D.size());
        const uint32_t first3DDrawItem = static_cast<uint32_t>(m_frame3DDrawItems.size());

        m_frameVertices.insert(m_frameVertices.end(), m_batchVertices.begin(), m_batchVertices.end());
        for (uint32_t idx : m_batchIndices) {
            m_frameIndices.push_back(baseVertex + idx);
        }
        for (VkDrawItem item : m_batchDrawItems) {
            item.firstIndex += baseIndex;
            m_frameDrawItems.push_back(item);
        }

        m_frameTriangleVertices3D.insert(m_frameTriangleVertices3D.end(),
                                         m_main3DBatch.triangleVertices.begin(),
                                         m_main3DBatch.triangleVertices.end());
        m_frameLineVertices3D.insert(m_frameLineVertices3D.end(),
                                     m_main3DBatch.lineVertices.begin(),
                                     m_main3DBatch.lineVertices.end());
        for (Vk3DDrawItem item : m_main3DBatch.drawItems) {
            item.firstVertex += triFirst;
            m_frame3DDrawItems.push_back(item);
        }
        m_frame3DShaderProgramId = m_main3DBatch.shaderProgramId;

        m_framePasses.push_back(VkFramePass{
            0,
            firstDraw,
            static_cast<uint32_t>(m_frameDrawItems.size() - firstDraw),
            static_cast<uint32_t>(m_swapChainExtent.width),
            static_cast<uint32_t>(m_swapChainExtent.height),
            triFirst,
            static_cast<uint32_t>(m_main3DBatch.triangleVertices.size()),
            lineFirst,
            static_cast<uint32_t>(m_main3DBatch.lineVertices.size()),
            first3DDrawItem,
            static_cast<uint32_t>(m_frame3DDrawItems.size() - first3DDrawItem)
        });
    }
}

bool QuarkVkRenderer::UploadFrameGeometry(uint32_t frameIndex) {
    VkFrameData& frame = m_frames[frameIndex];
    const VkDeviceSize vertexBytes = m_frameVertices.size() * sizeof(VkBatchVertex);
    const VkDeviceSize indexBytes = m_frameIndices.size() * sizeof(uint32_t);
    const VkDeviceSize vertexBytes3D =
        (m_frameTriangleVertices3D.size() + m_frameLineVertices3D.size()) * sizeof(Vk3DVertex);

    if (!EnsureMappedBufferCapacity(frame.vertexBuffer, frame.vertexMemory, frame.vertexMapped,
                                    frame.vertexCapacity, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
        !EnsureMappedBufferCapacity(frame.indexBuffer, frame.indexMemory, frame.indexMapped,
                                    frame.indexCapacity, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT) ||
        !EnsureMappedBufferCapacity(frame.vertexBuffer3D, frame.vertexMemory3D, frame.vertexMapped3D,
                                    frame.vertexCapacity3D, vertexBytes3D, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        TraceLog(LogLevel::Error, "VULKAN", "Failed to grow per-frame geometry buffer.");
        return false;
    }

    if (!m_frameVertices.empty() && frame.vertexMapped) {
        std::memcpy(frame.vertexMapped, m_frameVertices.data(),
                    m_frameVertices.size() * sizeof(VkBatchVertex));
    }
    if (!m_frameIndices.empty() && frame.indexMapped) {
        std::memcpy(frame.indexMapped, m_frameIndices.data(),
                    m_frameIndices.size() * sizeof(uint32_t));
    }
    if (frame.vertexMapped3D) {
        const size_t triBytes = m_frameTriangleVertices3D.size() * sizeof(Vk3DVertex);
        const size_t lineBytes = m_frameLineVertices3D.size() * sizeof(Vk3DVertex);
        if (triBytes + lineBytes > 0) {
            std::memcpy(frame.vertexMapped3D, m_frameTriangleVertices3D.data(), triBytes);
            if (lineBytes > 0) {
                std::memcpy(static_cast<char*>(frame.vertexMapped3D) + triBytes,
                            m_frameLineVertices3D.data(), lineBytes);
            }
        }
    }
    return true;
}

void QuarkVkRenderer::AppendQuadToBatch(
    std::vector<VkBatchVertex>& vertices,
    std::vector<uint32_t>&      indices,
    std::vector<VkDrawItem>&    drawItems,
    VkDescriptorSet             ds,
    float x0, float y0,
    float x1, float y1,
    float x2, float y2,
    float x3, float y3,
    float r, float g, float b, float a,
    float u0, float v0,
    float u1, float v1)
{
    const Vec2 p0 = ApplyCameraTransform(Vec2{ x0, y0 });
    const Vec2 p1 = ApplyCameraTransform(Vec2{ x1, y1 });
    const Vec2 p2 = ApplyCameraTransform(Vec2{ x2, y2 });
    const Vec2 p3 = ApplyCameraTransform(Vec2{ x3, y3 });

    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.push_back({ p0.x, p0.y, u0, v0, r, g, b, a });
    vertices.push_back({ p1.x, p1.y, u1, v0, r, g, b, a });
    vertices.push_back({ p2.x, p2.y, u1, v1, r, g, b, a });
    vertices.push_back({ p3.x, p3.y, u0, v1, r, g, b, a });

    const uint32_t firstIndex = static_cast<uint32_t>(indices.size());
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
    indices.push_back(base + 0);

    if (!drawItems.empty() &&
        drawItems.back().descriptorSet == ds &&
        drawItems.back().shaderProgramId == m_vkShaderCompiler.CurrentProgramId() &&
        drawItems.back().firstIndex + drawItems.back().indexCount == firstIndex) {
        drawItems.back().indexCount += 6;
    } else {
        drawItems.push_back({ 0, m_vkShaderCompiler.CurrentProgramId(), ds, firstIndex, 6 });
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
    if (m_activeRenderTargetId != 0) {
        auto itRt = m_renderTargets.find(m_activeRenderTargetId);
        if (itRt != m_renderTargets.end()) {
            AppendQuadToBatch(itRt->second.vertices, itRt->second.indices,
                              itRt->second.drawItems, ds,
                              x0, y0, x1, y1, x2, y2, x3, y3,
                              r, g, b, a, u0, v0, u1, v1);
            return;
        }
    }
    AppendQuadToBatch(m_batchVertices, m_batchIndices, m_batchDrawItems, ds,
                      x0, y0, x1, y1, x2, y2, x3, y3,
                      r, g, b, a, u0, v0, u1, v1);
}

bool QuarkVkRenderer::RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) return false;

    const VkFrameData& frame = m_frames[m_currentFrame];
    VkDeviceSize offsets[] = { 0 };
    VkDescriptorSet whiteDescriptorSet = m_white3DDescriptorSet;
    VkDescriptorSet white2DDescriptorSet = VK_NULL_HANDLE;
    const VkTextureData* whiteTex = m_vkResources.Get(m_whiteTextureId);
    if (whiteTex != nullptr) {
        white2DDescriptorSet = whiteTex->descriptorSet;
    }
    
    for (const VkFramePass& pass : m_framePasses) {
        if (pass.renderTargetId == 0) continue;

        auto itRt  = m_renderTargets.find(pass.renderTargetId);
        if (itRt == m_renderTargets.end()) continue;

        Color passClearColor = m_clearColor;
        if (pass.renderTargetId != 0) {
            if (itRt != m_renderTargets.end()) {
                passClearColor = itRt->second.clearColor;
            }
        }

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{
            NormalizeColorComponent(passClearColor.r),
            NormalizeColorComponent(passClearColor.g),
            NormalizeColorComponent(passClearColor.b),
            NormalizeColorComponent(passClearColor.a)
        }};
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rtPassInfo{};
        rtPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rtPassInfo.renderPass        = m_vkRenderTarget.RenderPass();
        rtPassInfo.framebuffer       = m_vkRenderTarget.Framebuffer(pass.renderTargetId);
        rtPassInfo.renderArea.extent = { pass.width, pass.height };
        rtPassInfo.clearValueCount   = static_cast<uint32_t>(clearValues.size());
        rtPassInfo.pClearValues      = clearValues.data();
        vkCmdBeginRenderPass(cmd, &rtPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = 0.0f;
        viewport.width    = static_cast<float>(pass.width);
        viewport.height   = static_cast<float>(pass.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = { pass.width, pass.height };
        if (m_scissorEnabled) {
            scissor = m_scissorRect;
            scissor.offset.x = std::max(0, std::min(scissor.offset.x, static_cast<int32_t>(pass.width)));
            scissor.offset.y = std::max(0, std::min(scissor.offset.y, static_cast<int32_t>(pass.height)));
        }
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        if ((pass.triVertexCount + pass.lineVertexCount) > 0 &&
            whiteDescriptorSet != VK_NULL_HANDLE) {
            vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertexBuffer3D, offsets);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_vkPipeline.GetLayout3D(), 0, 1, &whiteDescriptorSet, 0, nullptr);

            if (pass.drawItemCount3D > 0) {
                uint32_t cursor = pass.triFirstVertex;
                for (uint32_t i = 0; i < pass.drawItemCount3D; ++i) {
                    const Vk3DDrawItem& item = m_frame3DDrawItems[pass.first3DDrawItem + i];
                    if (item.firstVertex > cursor && m_vkPipeline.GetOffscreen3DTri() != VK_NULL_HANDLE) {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vkPipeline.GetOffscreen3DTri());
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                m_vkPipeline.GetLayout3D(), 0, 1, &whiteDescriptorSet, 0, nullptr);
                        vkCmdDraw(cmd, item.firstVertex - cursor, 1, cursor, 0);
                    }
                    const VkDescriptorSet descriptorSet = item.descriptorSet != VK_NULL_HANDLE
                        ? item.descriptorSet : whiteDescriptorSet;
                    VkPipeline itemPipeline = m_vkPipeline.GetOffscreen3DTri();
                    if (item.shaderProgramId != 0) {
                        const VkShaderProgramData* shaderProgram = m_vkShaderCompiler.GetProgram(item.shaderProgramId);
                        if (shaderProgram != nullptr && shaderProgram->pipeline3DOffscreen != VK_NULL_HANDLE) {
                            itemPipeline = shaderProgram->pipeline3DOffscreen;
                        }
                    }
                    if (itemPipeline == VK_NULL_HANDLE || descriptorSet == VK_NULL_HANDLE) continue;
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, itemPipeline);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_vkPipeline.GetLayout3D(), 0, 1, &descriptorSet, 0, nullptr);
                    vkCmdDraw(cmd, item.vertexCount, 1, item.firstVertex, 0);
                    cursor = std::max(cursor, item.firstVertex + item.vertexCount);
                }
                const uint32_t triEnd = pass.triFirstVertex + pass.triVertexCount;
                if (cursor < triEnd && m_vkPipeline.GetOffscreen3DTri() != VK_NULL_HANDLE) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vkPipeline.GetOffscreen3DTri());
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_vkPipeline.GetLayout3D(), 0, 1, &whiteDescriptorSet, 0, nullptr);
                    vkCmdDraw(cmd, triEnd - cursor, 1, cursor, 0);
                }
            } else if (pass.triVertexCount > 0 && m_vkPipeline.GetOffscreen3DTri() != VK_NULL_HANDLE) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vkPipeline.GetOffscreen3DTri());
                vkCmdDraw(cmd, pass.triVertexCount, 1, pass.triFirstVertex, 0);
            }
            if (pass.lineVertexCount > 0 && m_vkPipeline.GetOffscreen3DLines() != VK_NULL_HANDLE) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vkPipeline.GetOffscreen3DLines());
                vkCmdDraw(cmd, pass.lineVertexCount, 1,
                          static_cast<uint32_t>(m_frameTriangleVertices3D.size()) + pass.lineFirstVertex, 0);
            }
        }

        if (m_vkPipeline.GetOffscreen2D() != VK_NULL_HANDLE) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vkPipeline.GetOffscreen2D());
        }
        const VkPushConstants2D rtPushConstants{
            static_cast<float>(pass.width),
            static_cast<float>(pass.height)
        };
        vkCmdPushConstants(cmd, m_vkPipeline.GetLayout2D(),
                   VK_SHADER_STAGE_VERTEX_BIT,
                   0, sizeof(rtPushConstants), &rtPushConstants);
        vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertexBuffer, offsets);
        vkCmdBindIndexBuffer(cmd, frame.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        for (uint32_t i = 0; i < pass.drawItemCount; ++i) {
            const VkDrawItem& item = m_frameDrawItems[pass.firstDrawItem + i];
            if (item.descriptorSet == VK_NULL_HANDLE) continue;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_vkPipeline.GetLayout2D(), 0, 1, &item.descriptorSet, 0, nullptr);
            vkCmdDrawIndexed(cmd, item.indexCount, 1, item.firstIndex, 0, 0);
        }

        vkCmdEndRenderPass(cmd);
    }

    const VkFramePass* mainPass = nullptr;
    for (const VkFramePass& pass : m_framePasses) {
        if (pass.renderTargetId == 0) {
            mainPass = &pass;
            break;
        }
    }

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{
        NormalizeColorComponent(m_clearColor.r),
        NormalizeColorComponent(m_clearColor.g),
        NormalizeColorComponent(m_clearColor.b),
        NormalizeColorComponent(m_clearColor.a)
    }};
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass        = m_vkRenderPass.Get();
    renderPassInfo.framebuffer       = m_swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.extent = m_swapChainExtent;
    renderPassInfo.clearValueCount   = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues      = clearValues.data();
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(m_swapChainExtent.width);
    viewport.height   = static_cast<float>(m_swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapChainExtent;
    if (m_scissorEnabled) {
        scissor = m_scissorRect;
        scissor.offset.x = std::max(0, std::min(scissor.offset.x, static_cast<int32_t>(m_swapChainExtent.width)));
        scissor.offset.y = std::max(0, std::min(scissor.offset.y, static_cast<int32_t>(m_swapChainExtent.height)));
    }
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (mainPass && (mainPass->triVertexCount + mainPass->lineVertexCount) > 0 &&
        whiteDescriptorSet != VK_NULL_HANDLE) {
        vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertexBuffer3D, offsets);
        VkPipeline custom3DPipeline = m_vkPipeline.Get3DTri();
        VkPipelineLayout custom3DLayout = m_vkPipeline.GetLayout3D();
        VkDescriptorSet custom3DSet = whiteDescriptorSet;
        if (m_frame3DShaderProgramId != 0) {
            const VkShaderProgramData* shaderProgram = m_vkShaderCompiler.GetProgram(m_frame3DShaderProgramId);
            if (shaderProgram != nullptr && shaderProgram->pipeline3D != VK_NULL_HANDLE) {
                custom3DPipeline = shaderProgram->pipeline3D;
                custom3DLayout = m_vkPipeline.GetLayout3D();
                custom3DSet = shaderProgram->descriptorSet3D;
            }
        }

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                custom3DLayout, 0, 1, &custom3DSet, 0, nullptr);

        {
            if (m_3DDummyMapped != nullptr) {
                std::memcpy(static_cast<char*>(m_3DDummyMapped) + 0,
                            m_currentMatrix.m, sizeof(m_currentMatrix.m));
                std::memcpy(static_cast<char*>(m_3DDummyMapped) + 64,
                            m_viewMatrix.m, sizeof(m_viewMatrix.m));
                std::memcpy(static_cast<char*>(m_3DDummyMapped) + 128,
                            m_projectionMatrix.m, sizeof(m_projectionMatrix.m));
            }
            Vk3DPushConstants lighting{};
            for (int lightIndex = 0; lightIndex < 4; ++lightIndex) {
                const VkLight3D& light = m_lights[static_cast<size_t>(lightIndex)];
                lighting.lightPositions[lightIndex][0] = light.position.x;
                lighting.lightPositions[lightIndex][1] = light.position.y;
                lighting.lightPositions[lightIndex][2] = light.position.z;
                lighting.lightPositions[lightIndex][3] = 1.0f;
                lighting.lightColors[lightIndex][0] = light.color.x;
                lighting.lightColors[lightIndex][1] = light.color.y;
                lighting.lightColors[lightIndex][2] = light.color.z;
                lighting.lightColors[lightIndex][3] = 1.0f;
                lighting.lightEnabled[lightIndex] = light.enabled ? 1.0f : 0.0f;
            }
            lighting.timeData[0] = static_cast<float>(SDL_GetTicks()) / 1000.0f;
            if (m_3DDummyMapped != nullptr) {
                float* lightData = static_cast<float*>(static_cast<void*>(static_cast<char*>(m_3DDummyMapped) + 1024));
                std::fill(lightData, lightData + 80, 0.0f);
                lightData[0] = 0.1f;
                lightData[1] = 0.1f;
                lightData[2] = 0.1f;
                lightData[3] = 1.0f;
                lightData[4] = 1.0f;
                lightData[5] = 1.0f;
                lightData[6] = 1.0f;
                lightData[7] = 1.0f;
                lightData[8] = m_viewPos.x;
                lightData[9] = m_viewPos.y;
                lightData[10] = m_viewPos.z;
                lightData[11] = 1.0f;
                for (int lightIndex = 0; lightIndex < 4; ++lightIndex) {
                    const VkLight3D& light = m_lights[static_cast<size_t>(lightIndex)];
                    const size_t base = 12 + static_cast<size_t>(lightIndex) * 16;
                    lightData[base + 0] = light.position.x;
                    lightData[base + 1] = light.position.y;
                    lightData[base + 2] = light.position.z;
                    lightData[base + 3] = 1.0f;
                    lightData[base + 4] = light.target.x;
                    lightData[base + 5] = light.target.y;
                    lightData[base + 6] = light.target.z;
                    lightData[base + 7] = 1.0f;
                    lightData[base + 8] = light.color.x;
                    lightData[base + 9] = light.color.y;
                    lightData[base + 10] = light.color.z;
                    lightData[base + 11] = 1.0f;
                    lightData[base + 12] = light.attenuation;
                    const int enabledValue = light.enabled ? 1 : 0;
                    std::memcpy(lightData + base + 13, &enabledValue, sizeof(int));
                    const int typeValue = light.type;
                    std::memcpy(lightData + base + 14, &typeValue, sizeof(int));
                }
            }
            vkCmdPushConstants(cmd, custom3DLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(lighting), &lighting);
        }

        if (mainPass->drawItemCount3D > 0) {
            uint32_t cursor = mainPass->triFirstVertex;
            for (uint32_t i = 0; i < mainPass->drawItemCount3D; ++i) {
                const Vk3DDrawItem& item = m_frame3DDrawItems[mainPass->first3DDrawItem + i];
                if (item.firstVertex > cursor && m_vkPipeline.Get3DTri() != VK_NULL_HANDLE) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vkPipeline.Get3DTri());
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_vkPipeline.GetLayout3D(), 0, 1, &whiteDescriptorSet, 0, nullptr);
                    vkCmdDraw(cmd, item.firstVertex - cursor, 1, cursor, 0);
                }
                VkPipeline pipeline = m_vkPipeline.Get3DTri();
                VkPipelineLayout layout = m_vkPipeline.GetLayout3D();
                VkDescriptorSet descriptorSet = whiteDescriptorSet;
                const VkShaderProgramData* shaderProgram = m_vkShaderCompiler.GetProgram(item.shaderProgramId);
                if (shaderProgram != nullptr && shaderProgram->pipeline3D != VK_NULL_HANDLE) {
                    pipeline = shaderProgram->pipeline3D;
                    layout = m_vkPipeline.GetLayout3D();
                    descriptorSet = item.descriptorSet;
                } else if (item.descriptorSet != VK_NULL_HANDLE) {
                    descriptorSet = item.descriptorSet;
                }
                if (pipeline == VK_NULL_HANDLE || descriptorSet == VK_NULL_HANDLE) continue;
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        layout, 0, 1, &descriptorSet, 0, nullptr);
                vkCmdDraw(cmd, item.vertexCount, 1, item.firstVertex, 0);
                cursor = std::max(cursor, item.firstVertex + item.vertexCount);
            }
            const uint32_t triEnd = mainPass->triFirstVertex + mainPass->triVertexCount;
            if (cursor < triEnd && m_vkPipeline.Get3DTri() != VK_NULL_HANDLE) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vkPipeline.Get3DTri());
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_vkPipeline.GetLayout3D(), 0, 1, &whiteDescriptorSet, 0, nullptr);
                vkCmdDraw(cmd, triEnd - cursor, 1, cursor, 0);
            }
        } else {
            if (mainPass->triVertexCount > 0 && custom3DPipeline != VK_NULL_HANDLE) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, custom3DPipeline);
                vkCmdDraw(cmd, mainPass->triVertexCount, 1, mainPass->triFirstVertex, 0);
            }
        }

        if (mainPass->lineVertexCount > 0 && m_vkPipeline.Get3DLines() != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_vkPipeline.GetLayout3D(), 0, 1, &whiteDescriptorSet, 0, nullptr);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vkPipeline.Get3DLines());
            vkCmdDraw(cmd, mainPass->lineVertexCount, 1,
                      static_cast<uint32_t>(m_frameTriangleVertices3D.size()) + mainPass->lineFirstVertex, 0);
        }
    }

    if (white2DDescriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_vkPipeline.GetLayout2D(), 0, 1, &white2DDescriptorSet, 0, nullptr);
    }

    VkPipeline pipeline2D = m_vkPipeline.Get2D();
    if (m_vkShaderCompiler.CurrentProgramId() != 0) {
        const VkShaderProgramData* shaderProgram = m_vkShaderCompiler.GetProgram(m_vkShaderCompiler.CurrentProgramId());
        if (shaderProgram != nullptr && shaderProgram->pipeline != VK_NULL_HANDLE) {
            pipeline2D = shaderProgram->pipeline;
        }
    }
    const VkPushConstants2D screenPushConstants{
        static_cast<float>(m_swapChainExtent.width),
        static_cast<float>(m_swapChainExtent.height)
    };
    vkCmdPushConstants(cmd, m_vkPipeline.GetLayout2D(),
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(screenPushConstants), &screenPushConstants);
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertexBuffer, offsets);
    vkCmdBindIndexBuffer(cmd, frame.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    VkPipeline boundPipeline = VK_NULL_HANDLE;
    for (const VkFramePass& pass : m_framePasses) {
        if (pass.renderTargetId != 0) continue;
        for (uint32_t i = 0; i < pass.drawItemCount; ++i) {
            const VkDrawItem& item = m_frameDrawItems[pass.firstDrawItem + i];
            if (item.descriptorSet == VK_NULL_HANDLE) continue;

            VkPipeline itemPipeline = pipeline2D;
            if (item.shaderProgramId != 0) {
                const VkShaderProgramData* shaderProgram = m_vkShaderCompiler.GetProgram(item.shaderProgramId);
                if (shaderProgram != nullptr && shaderProgram->pipeline != VK_NULL_HANDLE) {
                    itemPipeline = shaderProgram->pipeline;
                }
            }
            if (itemPipeline != boundPipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, itemPipeline);
                boundPipeline = itemPipeline;
            }
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_vkPipeline.GetLayout2D(), 0, 1, &item.descriptorSet, 0, nullptr);
            vkCmdDrawIndexed(cmd, item.indexCount, 1, item.firstIndex, 0, 0);
        }
    }

    if (const VulkanRenderCallback callback = GetVulkanRenderCallback()) {
        callback(cmd);
    }

    vkCmdEndRenderPass(cmd);
    return vkEndCommandBuffer(cmd) == VK_SUCCESS;
}

void QuarkVkRenderer::PushQuad(float x, float y, float w, float h, Color color,
                                float u0, float v0, float u1, float v1) {
    VkDescriptorSet ds = m_vkResources.DescriptorSet(m_whiteTextureId);
    if (ds == VK_NULL_HANDLE) return;

    const float r = NormalizeColorComponent(color.r);
    const float g = NormalizeColorComponent(color.g);
    const float b = NormalizeColorComponent(color.b);
    const float a = NormalizeColorComponent(color.a);

    AppendQuad(ds,
               x,     y,
               x + w, y,
               x + w, y + h,
               x,     y + h,
               r, g, b, a,
               u0, v0, u1, v1);
}

void QuarkVkRenderer::EnsureBatchTexture(VkDescriptorSet ds) {
    m_currentDescriptorSet = ds;
}

} // namespace qc
