#include "QuarkVkRenderer.hpp"

#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_vulkan.h>
#include <cstddef>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

namespace qc {

static const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

static float NormalizeColorComponent(std::uint8_t value) {
    return static_cast<float>(value) / 255.0f;
}

static const std::array<uint32_t, 367> kQuadVertSpv = {
    0x07230203,
    0x00010500,
    0x0008000B,
    0x00000031,
    0x00000000,
    0x00020011,
    0x00000001,
    0x0006000B,
    0x00000001,
    0x4C534C47,
    0x6474732E,
    0x3035342E,
    0x00000000,
    0x0003000E,
    0x00000000,
    0x00000001,
    0x000C000F,
    0x00000000,
    0x00000004,
    0x6E69616D,
    0x00000000,
    0x0000000B,
    0x0000000F,
    0x00000021,
    0x0000002A,
    0x0000002B,
    0x0000002D,
    0x0000002F,
    0x00030003,
    0x00000002,
    0x000001C2,
    0x00040005,
    0x00000004,
    0x6E69616D,
    0x00000000,
    0x00030005,
    0x00000009,
    0x0063646E,
    0x00040005,
    0x0000000B,
    0x736F5061,
    0x00000000,
    0x00060005,
    0x0000000D,
    0x68737550,
    0x736E6F43,
    0x746E6174,
    0x00000073,
    0x00060006,
    0x0000000D,
    0x00000000,
    0x65726373,
    0x69536E65,
    0x0000657A,
    0x00030005,
    0x0000000F,
    0x00006370,
    0x00060005,
    0x0000001F,
    0x505F6C67,
    0x65567265,
    0x78657472,
    0x00000000,
    0x00060006,
    0x0000001F,
    0x00000000,
    0x505F6C67,
    0x7469736F,
    0x006E6F69,
    0x00070006,
    0x0000001F,
    0x00000001,
    0x505F6C67,
    0x746E696F,
    0x657A6953,
    0x00000000,
    0x00070006,
    0x0000001F,
    0x00000002,
    0x435F6C67,
    0x4470696C,
    0x61747369,
    0x0065636E,
    0x00070006,
    0x0000001F,
    0x00000003,
    0x435F6C67,
    0x446C6C75,
    0x61747369,
    0x0065636E,
    0x00030005,
    0x00000021,
    0x00000000,
    0x00030005,
    0x0000002A,
    0x00565576,
    0x00030005,
    0x0000002B,
    0x00565561,
    0x00040005,
    0x0000002D,
    0x6C6F4376,
    0x0000726F,
    0x00040005,
    0x0000002F,
    0x6C6F4361,
    0x0000726F,
    0x00040047,
    0x0000000B,
    0x0000001E,
    0x00000000,
    0x00030047,
    0x0000000D,
    0x00000002,
    0x00050048,
    0x0000000D,
    0x00000000,
    0x00000023,
    0x00000000,
    0x00030047,
    0x0000001F,
    0x00000002,
    0x00050048,
    0x0000001F,
    0x00000000,
    0x0000000B,
    0x00000000,
    0x00050048,
    0x0000001F,
    0x00000001,
    0x0000000B,
    0x00000001,
    0x00050048,
    0x0000001F,
    0x00000002,
    0x0000000B,
    0x00000003,
    0x00050048,
    0x0000001F,
    0x00000003,
    0x0000000B,
    0x00000004,
    0x00040047,
    0x0000002A,
    0x0000001E,
    0x00000000,
    0x00040047,
    0x0000002B,
    0x0000001E,
    0x00000001,
    0x00040047,
    0x0000002D,
    0x0000001E,
    0x00000001,
    0x00040047,
    0x0000002F,
    0x0000001E,
    0x00000002,
    0x00020013,
    0x00000002,
    0x00030021,
    0x00000003,
    0x00000002,
    0x00030016,
    0x00000006,
    0x00000020,
    0x00040017,
    0x00000007,
    0x00000006,
    0x00000002,
    0x00040020,
    0x00000008,
    0x00000007,
    0x00000007,
    0x00040020,
    0x0000000A,
    0x00000001,
    0x00000007,
    0x0004003B,
    0x0000000A,
    0x0000000B,
    0x00000001,
    0x0003001E,
    0x0000000D,
    0x00000007,
    0x00040020,
    0x0000000E,
    0x00000009,
    0x0000000D,
    0x0004003B,
    0x0000000E,
    0x0000000F,
    0x00000009,
    0x00040015,
    0x00000010,
    0x00000020,
    0x00000001,
    0x0004002B,
    0x00000010,
    0x00000011,
    0x00000000,
    0x00040020,
    0x00000012,
    0x00000009,
    0x00000007,
    0x0004002B,
    0x00000006,
    0x00000016,
    0x40000000,
    0x0004002B,
    0x00000006,
    0x00000018,
    0x3F800000,
    0x00040017,
    0x0000001B,
    0x00000006,
    0x00000004,
    0x00040015,
    0x0000001C,
    0x00000020,
    0x00000000,
    0x0004002B,
    0x0000001C,
    0x0000001D,
    0x00000001,
    0x0004001C,
    0x0000001E,
    0x00000006,
    0x0000001D,
    0x0006001E,
    0x0000001F,
    0x0000001B,
    0x00000006,
    0x0000001E,
    0x0000001E,
    0x00040020,
    0x00000020,
    0x00000003,
    0x0000001F,
    0x0004003B,
    0x00000020,
    0x00000021,
    0x00000003,
    0x0004002B,
    0x00000006,
    0x00000023,
    0x00000000,
    0x00040020,
    0x00000027,
    0x00000003,
    0x0000001B,
    0x00040020,
    0x00000029,
    0x00000003,
    0x00000007,
    0x0004003B,
    0x00000029,
    0x0000002A,
    0x00000003,
    0x0004003B,
    0x0000000A,
    0x0000002B,
    0x00000001,
    0x0004003B,
    0x00000027,
    0x0000002D,
    0x00000003,
    0x00040020,
    0x0000002E,
    0x00000001,
    0x0000001B,
    0x0004003B,
    0x0000002E,
    0x0000002F,
    0x00000001,
    0x00050036,
    0x00000002,
    0x00000004,
    0x00000000,
    0x00000003,
    0x000200F8,
    0x00000005,
    0x0004003B,
    0x00000008,
    0x00000009,
    0x00000007,
    0x0004003D,
    0x00000007,
    0x0000000C,
    0x0000000B,
    0x00050041,
    0x00000012,
    0x00000013,
    0x0000000F,
    0x00000011,
    0x0004003D,
    0x00000007,
    0x00000014,
    0x00000013,
    0x00050088,
    0x00000007,
    0x00000015,
    0x0000000C,
    0x00000014,
    0x0005008E,
    0x00000007,
    0x00000017,
    0x00000015,
    0x00000016,
    0x00050050,
    0x00000007,
    0x00000019,
    0x00000018,
    0x00000018,
    0x00050083,
    0x00000007,
    0x0000001A,
    0x00000017,
    0x00000019,
    0x0003003E,
    0x00000009,
    0x0000001A,
    0x0004003D,
    0x00000007,
    0x00000022,
    0x00000009,
    0x00050051,
    0x00000006,
    0x00000024,
    0x00000022,
    0x00000000,
    0x00050051,
    0x00000006,
    0x00000025,
    0x00000022,
    0x00000001,
    0x00070050,
    0x0000001B,
    0x00000026,
    0x00000024,
    0x00000025,
    0x00000023,
    0x00000018,
    0x00050041,
    0x00000027,
    0x00000028,
    0x00000021,
    0x00000011,
    0x0003003E,
    0x00000028,
    0x00000026,
    0x0004003D,
    0x00000007,
    0x0000002C,
    0x0000002B,
    0x0003003E,
    0x0000002A,
    0x0000002C,
    0x0004003D,
    0x0000001B,
    0x00000030,
    0x0000002F,
    0x0003003E,
    0x0000002D,
    0x00000030,
    0x000100FD,
    0x00010038
};

static const std::array<uint32_t, 166> kQuadFragSpv = {
    0x07230203, 0x00010500, 0x0008000B, 0x00000018, 0x00000000, 0x00020011, 0x00000001, 0x0006000B,
    0x00000001, 0x4C534C47, 0x6474732E, 0x3035342E, 0x00000000, 0x0003000E, 0x00000000, 0x00000001,
    0x0009000F, 0x00000004, 0x00000004, 0x6E69616D, 0x00000000, 0x00000009, 0x0000000D, 0x00000011,
    0x00000015, 0x00030010, 0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001C2, 0x00040005,
    0x00000004, 0x6E69616D, 0x00000000, 0x00050005, 0x00000009, 0x4374756F, 0x726F6C6F, 0x00000000,
    0x00050005, 0x0000000D, 0x78655475, 0x65727574, 0x00000000, 0x00030005, 0x00000011, 0x00565576,
    0x00040005, 0x00000015, 0x6C6F4376, 0x0000726F, 0x00040047, 0x00000009, 0x0000001E, 0x00000000,
    0x00040047, 0x0000000D, 0x00000021, 0x00000000, 0x00040047, 0x0000000D, 0x00000022, 0x00000000,
    0x00040047, 0x00000011, 0x0000001E, 0x00000000, 0x00040047, 0x00000015, 0x0000001E, 0x00000001,
    0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020,
    0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008, 0x00000003, 0x00000007,
    0x0004003B, 0x00000008, 0x00000009, 0x00000003, 0x00090019, 0x0000000A, 0x00000006, 0x00000001,
    0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x0003001B, 0x0000000B, 0x0000000A,
    0x00040020, 0x0000000C, 0x00000000, 0x0000000B, 0x0004003B, 0x0000000C, 0x0000000D, 0x00000000,
    0x00040017, 0x0000000F, 0x00000006, 0x00000002, 0x00040020, 0x00000010, 0x00000001, 0x0000000F,
    0x0004003B, 0x00000010, 0x00000011, 0x00000001, 0x00040020, 0x00000014, 0x00000001, 0x00000007,
    0x0004003B, 0x00000014, 0x00000015, 0x00000001, 0x00050036, 0x00000002, 0x00000004, 0x00000000,
    0x00000003, 0x000200F8, 0x00000005, 0x0004003D, 0x0000000B, 0x0000000E, 0x0000000D, 0x0004003D,
    0x0000000F, 0x00000012, 0x00000011, 0x00050057, 0x00000007, 0x00000013, 0x0000000E, 0x00000012,
    0x0004003D, 0x00000007, 0x00000016, 0x00000015, 0x00050085, 0x00000007, 0x00000017, 0x00000013,
    0x00000016, 0x0003003E, 0x00000009, 0x00000017, 0x000100FD, 0x00010038
};

QuarkVkRenderer::~QuarkVkRenderer() {
    Shutdown();
}

void QuarkVkRenderer::Init(SDL_Window* window, int width, int height) {
    uint32_t version = 0;
    vkEnumerateInstanceVersion(&version);

    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Initializing Vulkan renderer... (API %d.%d.%d)",
        VK_VERSION_MAJOR(version),
        VK_VERSION_MINOR(version),
        VK_VERSION_PATCH(version)));

#ifdef __APPLE__
    TraceLog(LogLevel::Info, "VULKAN", "Apple platform detected, using MoltenVK...");
#endif

    m_window   = window;
    m_width    = width;
    m_height   = height;
    m_framebufferResized = false;

    CreateInstance();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapChain();
    CreateImageViews();
    CreateRenderPass();
    CreateOffscreenRenderPass();
    CreateDescriptorSetLayout();
    CreatePipeline2D();
    CreateOffscreenPipeline2D();
    CreateFramebuffers();
    CreateCommandPool();
    CreateCommandBuffers();
    CreateFrameVertexIndexBuffers();
    CreateSyncObjects();
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

    {
        std::vector<uint32_t> ids;
        ids.reserve(m_textures.size());
        for (const auto& [id, _] : m_textures) { ids.push_back(id); }
        for (uint32_t id : ids) { DestroyTexture(id); }
    }

    CleanupSwapChain();

    for (auto& frame : m_frames) {
        if (frame.imageAvailable != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, frame.imageAvailable, nullptr);
            frame.imageAvailable = VK_NULL_HANDLE;
        }
        if (frame.renderFinished != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, frame.renderFinished, nullptr);
            frame.renderFinished = VK_NULL_HANDLE;
        }
        if (frame.inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, frame.inFlightFence, nullptr);
            frame.inFlightFence = VK_NULL_HANDLE;
        }

        if (frame.vertexMapped && frame.vertexMemory != VK_NULL_HANDLE) {
            vkUnmapMemory(m_device, frame.vertexMemory);
            frame.vertexMapped = nullptr;
        }
        if (frame.vertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, frame.vertexBuffer, nullptr);
            frame.vertexBuffer = VK_NULL_HANDLE;
        }
        if (frame.vertexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, frame.vertexMemory, nullptr);
            frame.vertexMemory = VK_NULL_HANDLE;
        }
        if (frame.indexMapped && frame.indexMemory != VK_NULL_HANDLE) {
            vkUnmapMemory(m_device, frame.indexMemory);
            frame.indexMapped = nullptr;
        }
        if (frame.indexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, frame.indexBuffer, nullptr);
            frame.indexBuffer = VK_NULL_HANDLE;
        }
        if (frame.indexMemory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, frame.indexMemory, nullptr);
            frame.indexMemory = VK_NULL_HANDLE;
        }
    }

    for (VkDescriptorPool pool : m_descriptorPools) {
        if (pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_device, pool, nullptr);
        }
    }
    m_descriptorPools.clear();

    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }

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
    m_nextTextureId      = 1;
    m_nextRenderTargetId = 1;
    m_activeRenderTargetId = 0;
}

void QuarkVkRenderer::BeginDrawing() {
    if (!m_device || !m_swapChain || m_drawing) {
        return;
    }

    m_activeRenderTargetId = 0;
    if (m_lastFrameCounter == 0) {
        m_lastFrameCounter = SDL_GetPerformanceCounter();
    }

    VkFrameData& frame = m_frames[m_currentFrame];
    vkWaitForFences(m_device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        m_device, m_swapChain, UINT64_MAX,
        frame.imageAvailable, VK_NULL_HANDLE, &m_imageIndex);

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
    if (!m_drawing || !m_device || !m_swapChain) return;

    VkFrameData& frame = m_frames[m_currentFrame];
    VkCommandBuffer cmd = frame.commandBuffer;

    vkResetFences(m_device, 1, &frame.inFlightFence);
    vkResetCommandBuffer(cmd, 0);

    BuildCombinedFrameGeometry();

    m_batchVertices.clear();
    m_batchIndices.clear();
    m_batchDrawItems.clear();
    for (auto& [id, rt] : m_renderTargets) {
        (void)id;
        rt.vertices.clear();
        rt.indices.clear();
        rt.drawItems.clear();
    }

    if (!UploadFrameGeometry(m_currentFrame)) {
        m_drawing = false;
        return;
    }
    if (!RecordCommandBuffer(cmd, m_imageIndex)) {
        m_drawing = false;
        return;
    }
    if (!UploadFrameGeometry(m_currentFrame)) {
        m_drawing = false;
        return;
    }

    if (!RecordCommandBuffer(cmd, m_imageIndex)) {
        m_drawing = false;
        return;
    }

    VkSemaphore waitSemaphores[]   = { frame.imageAvailable };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { frame.renderFinished };

    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = waitSemaphores;
    submitInfo.pWaitDstStageMask    = waitStages;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = signalSemaphores;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, frame.inFlightFence) != VK_SUCCESS) {
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

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = signalSemaphores;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &m_swapChain;
    presentInfo.pImageIndices      = &m_imageIndex;

    VkResult presentResult = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (presentResult == VK_SUCCESS) {
        vkQueueWaitIdle(m_presentQueue);
    }
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
    return m_currentMatrix.m;
}

const float* QuarkVkRenderer::GetMatrixProjection() {
    return m_projectionMatrix.m;
}

void QuarkVkRenderer::EnableBackfaceCulling()  {}
void QuarkVkRenderer::DisableBackfaceCulling() {}

void QuarkVkRenderer::RefreshViewport() {
    m_framebufferResized = true;
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
        throw std::runtime_error("Failed to find GPUs with Vulkan support.");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        if (IsDeviceSuitable(device)) {
            m_physicalDevice = device;
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            TraceLog(LogLevel::Info, "VULKAN", TextFormat("Selected physical device: %s", props.deviceName));
            break;
        }
    }
    if (m_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find a suitable Vulkan physical device.");
    }
}

void QuarkVkRenderer::CreateLogicalDevice() {
    VkQueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(), indices.presentFamily.value()
    };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount       = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount    = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos       = queueCreateInfos.data();
    createInfo.pEnabledFeatures        = &deviceFeatures;
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(kDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();
    createInfo.enabledLayerCount       = 0;

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan logical device.");
    }
    vkGetDeviceQueue(m_device, indices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily.value(),  0, &m_presentQueue);
    TraceLog(LogLevel::Info, "VULKAN", "Logical device and queues created.");
}

void QuarkVkRenderer::CreateSwapChain() {
    VkSwapChainSupportDetails details = QuerySwapChainSupport(m_physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(details.formats);
    VkPresentModeKHR   presentMode   = ChooseSwapPresentMode(details.presentModes);
    VkExtent2D         extent        = ChooseSwapExtent(details.capabilities);

    uint32_t imageCount = details.capabilities.minImageCount + 1;
    if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount) {
        imageCount = details.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface          = m_surface;
    createInfo.minImageCount    = imageCount;
    createInfo.imageFormat      = surfaceFormat.format;
    createInfo.imageColorSpace  = surfaceFormat.colorSpace;
    createInfo.imageExtent      = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkQueueFamilyIndices indices         = FindQueueFamilies(m_physicalDevice);
    uint32_t             queueFamilyIndices[] = {
        indices.graphicsFamily.value(), indices.presentFamily.value()
    };

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices   = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices   = nullptr;
    }

    createInfo.preTransform   = details.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode    = presentMode;
    createInfo.clipped        = VK_TRUE;
    createInfo.oldSwapchain   = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan swap chain.");
    }

    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
    m_swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, m_swapChainImages.data());

    m_swapChainImageFormat = surfaceFormat.format;
    m_swapChainExtent      = extent;

    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Swapchain created (Extent: %ux%u, Format: %d)",
        m_swapChainExtent.width, m_swapChainExtent.height, m_swapChainImageFormat));
}

void QuarkVkRenderer::CreateImageViews() {
    m_swapChainImageViews.resize(m_swapChainImages.size());

    for (size_t i = 0; i < m_swapChainImages.size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image                           = m_swapChainImages[i];
        createInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format                          = m_swapChainImageFormat;
        createInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel   = 0;
        createInfo.subresourceRange.levelCount     = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(m_device, &createInfo, nullptr, &m_swapChainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan image views.");
        }
    }
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Created %zu image views.", m_swapChainImageViews.size()));
}

void QuarkVkRenderer::CreateRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = m_swapChainImageFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments    = &colorAttachment;
    renderPassInfo.subpassCount    = 1;
    renderPassInfo.pSubpasses      = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies   = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan render pass.");
    }
    TraceLog(LogLevel::Trace, "VULKAN", "Swapchain render pass created.");
}

void QuarkVkRenderer::CreateOffscreenRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = m_swapChainImageFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments    = &colorAttachment;
    renderPassInfo.subpassCount    = 1;
    renderPassInfo.pSubpasses      = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies   = &dependency;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_offscreenRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan offscreen render pass.");
    }
    TraceLog(LogLevel::Trace, "VULKAN", "Offscreen render pass created.");
}

void QuarkVkRenderer::CreateDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding            = 0;
    samplerBinding.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount    = 1;
    samplerBinding.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings    = &samplerBinding;

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan descriptor set layout.");
    }

    VkDescriptorPool firstSlab = VK_NULL_HANDLE;
    if (!CreateDescriptorPoolSlab(kVkDescriptorPoolSlabSize, firstSlab)) {
        throw std::runtime_error("Failed to create initial Vulkan descriptor pool.");
    }
    m_descriptorPools.push_back(firstSlab);
    TraceLog(LogLevel::Trace, "VULKAN", "Descriptor set layout and initial pool created.");
}

bool QuarkVkRenderer::CreateDescriptorPoolSlab(uint32_t maxSets, VkDescriptorPool& outPool) {
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = maxSets;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets       = maxSets;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;

    outPool = VK_NULL_HANDLE;
    return vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &outPool) == VK_SUCCESS;
}

bool QuarkVkRenderer::AllocateTextureDescriptorSet(VkDescriptorSet& outSet) {
    if (m_descriptorSetLayout == VK_NULL_HANDLE) return false;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &m_descriptorSetLayout;

    for (auto it = m_descriptorPools.rbegin(); it != m_descriptorPools.rend(); ++it) {
        allocInfo.descriptorPool = *it;
        VkResult result = vkAllocateDescriptorSets(m_device, &allocInfo, &outSet);
        if (result == VK_SUCCESS) return true;
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
            return false;
        }
    }

    VkDescriptorPool newSlab = VK_NULL_HANDLE;
    if (!CreateDescriptorPoolSlab(kVkDescriptorPoolSlabSize, newSlab)) return false;
    m_descriptorPools.push_back(newSlab);

    allocInfo.descriptorPool = newSlab;
    return vkAllocateDescriptorSets(m_device, &allocInfo, &outSet) == VK_SUCCESS;
}

void QuarkVkRenderer::CreateFramebuffers() {
    m_swapChainFramebuffers.resize(m_swapChainImageViews.size());

    for (size_t i = 0; i < m_swapChainImageViews.size(); ++i) {
        VkImageView attachments[] = { m_swapChainImageViews[i] };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass      = m_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments    = attachments;
        framebufferInfo.width           = m_swapChainExtent.width;
        framebufferInfo.height          = m_swapChainExtent.height;
        framebufferInfo.layers          = 1;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan framebuffer.");
        }
    }
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Created %zu framebuffers.", m_swapChainFramebuffers.size()));
}

bool QuarkVkRenderer::RecreateRenderTargetFramebuffers() {
    for (auto& [id, rt] : m_renderTargets) {
        (void)id;
        auto itTex = m_textures.find(rt.textureId);
        if (itTex == m_textures.end()) continue;

        if (rt.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, rt.framebuffer, nullptr);
            rt.framebuffer = VK_NULL_HANDLE;
        }

        VkImageView attachments[] = { itTex->second.view };
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = m_offscreenRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = attachments;
        fbInfo.width           = rt.width;
        fbInfo.height          = rt.height;
        fbInfo.layers          = 1;
        if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &rt.framebuffer) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

void QuarkVkRenderer::CreateCommandPool() {
    VkQueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan command pool.");
    }
    TraceLog(LogLevel::Trace, "VULKAN", "Command pool created.");
}

void QuarkVkRenderer::CreateCommandBuffers() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_frames.size());

    std::vector<VkCommandBuffer> commandBuffers(m_frames.size());
    if (vkAllocateCommandBuffers(m_device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan command buffers.");
    }
    for (size_t i = 0; i < m_frames.size(); ++i) {
        m_frames[i].commandBuffer = commandBuffers[i];
    }
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Allocated %zu command buffers.", m_frames.size()));
}

void QuarkVkRenderer::CreateFrameVertexIndexBuffers() {
    const VkDeviceSize vertexBufSize = sizeof(VkBatchVertex) * kVkMaxVerticesPerFrame;
    const VkDeviceSize indexBufSize  = sizeof(uint32_t)      * kVkMaxIndicesPerFrame;

    for (auto& frame : m_frames) {
        if (!CreateBuffer(vertexBufSize,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          frame.vertexBuffer, frame.vertexMemory)) {
            throw std::runtime_error("Failed to create per-frame Vulkan vertex buffer.");
        }
        vkMapMemory(m_device, frame.vertexMemory, 0, vertexBufSize, 0, &frame.vertexMapped);

        if (!CreateBuffer(indexBufSize,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          frame.indexBuffer, frame.indexMemory)) {
            throw std::runtime_error("Failed to create per-frame Vulkan index buffer.");
        }
        vkMapMemory(m_device, frame.indexMemory, 0, indexBufSize, 0, &frame.indexMapped);
    }
    TraceLog(LogLevel::Trace, "VULKAN", "Per-frame vertex/index buffers created.");
}

void QuarkVkRenderer::CreateSyncObjects() {
    for (auto& frame : m_frames) {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &frame.imageAvailable) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &frame.renderFinished) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &frame.inFlightFence) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan synchronization objects.");
        }
    }
    TraceLog(LogLevel::Trace, "VULKAN", "Synchronization objects created.");
}

void QuarkVkRenderer::CreateWhiteTexture() {
    const unsigned char white[4] = {255, 255, 255, 255};
    if (!CreateTextureFromRGBA(white, 1, 1, m_whiteTextureId)) {
        throw std::runtime_error("Failed to create Vulkan white fallback texture.");
    }
    TraceLog(LogLevel::Trace, "VULKAN", "White fallback texture created.");
}

VkPipeline QuarkVkRenderer::CreatePipelineForRenderPass(VkRenderPass renderPass) {
    if (m_device == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    if (m_pipelineLayout == VK_NULL_HANDLE) {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset     = 0;
        pushConstantRange.size       = sizeof(VkPushConstants2D);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount         = 1;
        layoutInfo.pSetLayouts            = &m_descriptorSetLayout;
        layoutInfo.pushConstantRangeCount  = 1;
        layoutInfo.pPushConstantRanges     = &pushConstantRange;

        if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan pipeline layout.");
        }
    }

    const std::vector<uint32_t> vertCode(kQuadVertSpv.begin(), kQuadVertSpv.end());
    const std::vector<uint32_t> fragCode(kQuadFragSpv.begin(), kQuadFragSpv.end());

    VkShaderModule vertShader = CreateShaderModule(vertCode);
    VkShaderModule fragShader = CreateShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertShader;
    vertStage.pName  = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragShader;
    fragStage.pName  = "main";

    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding   = 0;
    bindingDesc.stride    = sizeof(VkBatchVertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributes{};
    attributes[0].binding  = 0;
    attributes[0].location = 0;
    attributes[0].format   = VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset   = offsetof(VkBatchVertex, x);

    attributes[1].binding  = 0;
    attributes[1].location = 1;
    attributes[1].format   = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset   = offsetof(VkBatchVertex, u);

    attributes[2].binding  = 0;
    attributes[2].location = 2;
    attributes[2].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[2].offset   = offsetof(VkBatchVertex, r);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount  = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions    = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable  = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable        = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.cullMode                = VK_CULL_MODE_NONE;
    rasterizer.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.sampleShadingEnable  = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                          VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT |
                                          VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable    = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor  = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor  = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp         = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable     = VK_FALSE;
    colorBlending.attachmentCount   = 1;
    colorBlending.pAttachments      = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(sizeof(dynamicStates) / sizeof(dynamicStates[0]));
    dynamicState.pDynamicStates    = dynamicStates;

    std::array<VkPipelineShaderStageCreateInfo, 2> stages = { vertStage, fragStage };

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages             = stages.data();
    pipelineInfo.pVertexInputState   = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = m_pipelineLayout;
    pipelineInfo.renderPass          = renderPass;
    pipelineInfo.subpass             = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(m_device, fragShader, nullptr);
        vkDestroyShaderModule(m_device, vertShader, nullptr);
        throw std::runtime_error("Failed to create Vulkan 2D pipeline.");
    }

    vkDestroyShaderModule(m_device, fragShader, nullptr);
    vkDestroyShaderModule(m_device, vertShader, nullptr);
    return pipeline;
}

void QuarkVkRenderer::CreatePipeline2D() {
    if (m_pipeline2D != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline2D, nullptr);
        m_pipeline2D = VK_NULL_HANDLE;
    }
    m_pipeline2D = CreatePipelineForRenderPass(m_renderPass);
}

void QuarkVkRenderer::CreateOffscreenPipeline2D() {
    if (m_offscreenPipeline2D != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_offscreenPipeline2D, nullptr);
        m_offscreenPipeline2D = VK_NULL_HANDLE;
    }
    m_offscreenPipeline2D = CreatePipelineForRenderPass(m_offscreenRenderPass);
}

void QuarkVkRenderer::RecreateSwapChain() {
    TraceLog(LogLevel::Info, "VULKAN", "Recreating swapchain...");
    vkDeviceWaitIdle(m_device);

    CleanupSwapChain();

    CreateSwapChain();
    CreateImageViews();
    CreateRenderPass();
    CreateOffscreenRenderPass();
    CreatePipeline2D();
    CreateOffscreenPipeline2D();
    CreateFramebuffers();
    RecreateRenderTargetFramebuffers();
}

void QuarkVkRenderer::CleanupSwapChain() {
    TraceLog(LogLevel::Trace, "VULKAN", "Cleaning up swapchain resources...");

    for (auto& [id, rt] : m_renderTargets) {
        (void)id;
        if (rt.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, rt.framebuffer, nullptr);
            rt.framebuffer = VK_NULL_HANDLE;
        }
    }

    for (auto framebuffer : m_swapChainFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, framebuffer, nullptr);
        }
    }
    m_swapChainFramebuffers.clear();

    if (m_offscreenPipeline2D != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_offscreenPipeline2D, nullptr);
        m_offscreenPipeline2D = VK_NULL_HANDLE;
    }
    if (m_pipeline2D != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline2D, nullptr);
        m_pipeline2D = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    if (m_offscreenRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_offscreenRenderPass, nullptr);
        m_offscreenRenderPass = VK_NULL_HANDLE;
    }
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    for (auto imageView : m_swapChainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, imageView, nullptr);
        }
    }
    m_swapChainImageViews.clear();

    if (m_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }
    m_swapChainImages.clear();
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
    for (const auto& mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
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

bool QuarkVkRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                    VkMemoryPropertyFlags props,
                                    VkBuffer& outBuffer, VkDeviceMemory& outMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_device, outBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, props);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(m_device, outBuffer, outMemory, 0);
    return true;
}

VkCommandBuffer QuarkVkRenderer::BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, &cmd) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

void QuarkVkRenderer::EndSingleTimeCommands(VkCommandBuffer cmd) {
    if (cmd == VK_NULL_HANDLE) return;
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
}

bool QuarkVkRenderer::TransitionImageLayout(VkImage image, VkFormat /*format*/,
                                             VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) return false;

    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout                       = oldLayout;
    barrier.newLayout                       = newLayout;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndSingleTimeCommands(cmd);
    return true;
}

bool QuarkVkRenderer::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t w, uint32_t h) {
    VkCommandBuffer cmd = BeginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) return false;

    VkBufferImageCopy region{};
    region.bufferOffset                    = 0;
    region.bufferRowLength                 = 0;
    region.bufferImageHeight               = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset                     = {0, 0, 0};
    region.imageExtent                     = {w, h, 1};

    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndSingleTimeCommands(cmd);
    return true;
}

bool QuarkVkRenderer::CreateTextureFromRGBA(const unsigned char* rgba,
                                             uint32_t width, uint32_t height,
                                             uint32_t& outId) {
    if (!rgba || width == 0 || height == 0) return false;

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4u;

    VkBuffer       stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    if (!CreateBuffer(imageSize,
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      stagingBuffer, stagingMemory)) {
        return false;
    }

    void* mapped = nullptr;
    vkMapMemory(m_device, stagingMemory, 0, imageSize, 0, &mapped);
    std::memcpy(mapped, rgba, static_cast<size_t>(imageSize));
    vkUnmapMemory(m_device, stagingMemory);

    VkTextureData tex{};
    tex.width  = width;
    tex.height = height;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width  = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth  = 1;
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(m_device, &imageInfo, nullptr, &tex.image) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        return false;
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device, tex.image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &tex.memory) != VK_SUCCESS) {
        vkDestroyImage(m_device, tex.image, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        return false;
    }
    vkBindImageMemory(m_device, tex.image, tex.memory, 0);

    if (!TransitionImageLayout(tex.image, imageInfo.format,
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) ||
        !CopyBufferToImage(stagingBuffer, tex.image, width, height) ||
        !TransitionImageLayout(tex.image, imageInfo.format,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        return false;
    }

    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = tex.image;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &tex.view) != VK_SUCCESS) {
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
        return false;
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
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
        return false;
    }

    if (!AllocateTextureDescriptorSet(tex.descriptorSet)) {
        vkDestroySampler(m_device, tex.sampler, nullptr);
        vkDestroyImageView(m_device, tex.view, nullptr);
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
        return false;
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

    outId = m_nextTextureId++;
    m_textures[outId] = tex;
    return true;
}

void QuarkVkRenderer::DestroyTexture(uint32_t textureId) {
    auto it = m_textures.find(textureId);
    if (it == m_textures.end()) return;

    VkTextureData& tex = it->second;
    if (tex.sampler  != VK_NULL_HANDLE) vkDestroySampler   (m_device, tex.sampler,  nullptr);
    if (tex.view     != VK_NULL_HANDLE) vkDestroyImageView (m_device, tex.view,     nullptr);
    if (tex.image    != VK_NULL_HANDLE) vkDestroyImage     (m_device, tex.image,    nullptr);
    if (tex.memory   != VK_NULL_HANDLE) vkFreeMemory       (m_device, tex.memory,   nullptr);
    m_textures.erase(it);
}

IRenderTexture QuarkVkRenderer::CreateRenderTargetInternal(int width, int height) {
    if (width <= 0 || height <= 0 ||
        m_device == VK_NULL_HANDLE ||
        m_offscreenRenderPass == VK_NULL_HANDLE) {
        return IRenderTexture{};
    }

    VkTextureData tex{};
    tex.width  = static_cast<uint32_t>(width);
    tex.height = static_cast<uint32_t>(height);

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

    if (vkCreateImage(m_device, &imageInfo, nullptr, &tex.image) != VK_SUCCESS) {
        return IRenderTexture{};
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device, tex.image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &tex.memory) != VK_SUCCESS) {
        vkDestroyImage(m_device, tex.image, nullptr);
        return IRenderTexture{};
    }
    vkBindImageMemory(m_device, tex.image, tex.memory, 0);

    if (!TransitionImageLayout(tex.image, imageInfo.format,
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
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
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
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
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
        return IRenderTexture{};
    }

    if (!AllocateTextureDescriptorSet(tex.descriptorSet)) {
        vkDestroySampler(m_device, tex.sampler, nullptr);
        vkDestroyImageView(m_device, tex.view, nullptr);
        vkFreeMemory(m_device, tex.memory, nullptr);
        vkDestroyImage(m_device, tex.image, nullptr);
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

    const uint32_t textureId = m_nextTextureId++;
    m_textures[textureId] = tex;

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkImageView   attachments[] = { tex.view };

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass      = m_offscreenRenderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments    = attachments;
    fbInfo.width           = tex.width;
    fbInfo.height          = tex.height;
    fbInfo.layers          = 1;
    if (vkCreateFramebuffer(m_device, &fbInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        DestroyTexture(textureId);
        return IRenderTexture{};
    }

    const uint32_t rtId = m_nextRenderTargetId++;
    VkRenderTargetData rt{};
    rt.textureId   = textureId;
    rt.width       = tex.width;
    rt.height      = tex.height;
    rt.framebuffer = framebuffer;
    rt.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    m_renderTargets[rtId] = rt;

    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Render target created (%ux%u).", tex.width, tex.height));
    return IRenderTexture{ rtId };
}

void QuarkVkRenderer::DestroyRenderTargetInternal(uint32_t renderTargetId) {
    auto it = m_renderTargets.find(renderTargetId);
    if (it == m_renderTargets.end()) return;

    const uint32_t textureId = it->second.textureId;
    if (it->second.framebuffer != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_device, it->second.framebuffer, nullptr);
    }
    m_renderTargets.erase(it);

    if (m_activeRenderTargetId == renderTargetId) {
        m_activeRenderTargetId = 0;
    }
    DestroyTexture(textureId);
}

VkShaderModule QuarkVkRenderer::CreateShaderModule(const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode    = spirv.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shader module.");
    }
    return shaderModule;
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

    for (auto& [id, rt] : m_renderTargets) {
        if (rt.drawItems.empty()) continue;

        const uint32_t baseVertex = static_cast<uint32_t>(m_frameVertices.size());
        const uint32_t baseIndex  = static_cast<uint32_t>(m_frameIndices.size());
        const uint32_t firstDraw  = static_cast<uint32_t>(m_frameDrawItems.size());

        m_frameVertices.insert(m_frameVertices.end(), rt.vertices.begin(), rt.vertices.end());
        for (uint32_t idx : rt.indices) {
            m_frameIndices.push_back(baseVertex + idx);
        }
        for (VkDrawItem item : rt.drawItems) {
            item.firstIndex += baseIndex;
            m_frameDrawItems.push_back(item);
        }

        m_framePasses.push_back(VkFramePass{
            id,
            firstDraw,
            static_cast<uint32_t>(m_frameDrawItems.size() - firstDraw),
            rt.width,
            rt.height
        });
    }

    {
        const uint32_t baseVertex = static_cast<uint32_t>(m_frameVertices.size());
        const uint32_t baseIndex  = static_cast<uint32_t>(m_frameIndices.size());
        const uint32_t firstDraw  = static_cast<uint32_t>(m_frameDrawItems.size());

        m_frameVertices.insert(m_frameVertices.end(), m_batchVertices.begin(), m_batchVertices.end());
        for (uint32_t idx : m_batchIndices) {
            m_frameIndices.push_back(baseVertex + idx);
        }
        for (VkDrawItem item : m_batchDrawItems) {
            item.firstIndex += baseIndex;
            m_frameDrawItems.push_back(item);
        }

        m_framePasses.push_back(VkFramePass{
            0,
            firstDraw,
            static_cast<uint32_t>(m_frameDrawItems.size() - firstDraw),
            static_cast<uint32_t>(m_swapChainExtent.width),
            static_cast<uint32_t>(m_swapChainExtent.height)
        });
    }
}

bool QuarkVkRenderer::UploadFrameGeometry(uint32_t frameIndex) {
    if (m_frameVertices.size() > kVkMaxVerticesPerFrame ||
        m_frameIndices.size()  > kVkMaxIndicesPerFrame) {
        TraceLog(LogLevel::Error, "VULKAN", "Frame geometry overflow — increase kVkMaxVerticesPerFrame / kVkMaxIndicesPerFrame.");
        return false;
    }

    VkFrameData& frame = m_frames[frameIndex];
    if (!m_frameVertices.empty() && frame.vertexMapped) {
        std::memcpy(frame.vertexMapped, m_frameVertices.data(),
                    m_frameVertices.size() * sizeof(VkBatchVertex));
    }
    if (!m_frameIndices.empty() && frame.indexMapped) {
        std::memcpy(frame.indexMapped, m_frameIndices.data(),
                    m_frameIndices.size() * sizeof(uint32_t));
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
        drawItems.back().firstIndex + drawItems.back().indexCount == firstIndex) {
        drawItems.back().indexCount += 6;
    } else {
        drawItems.push_back({ 0, ds, firstIndex, 6 });
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
    
    auto inlineTransition = [&](VkImage image,
                                 VkImageLayout oldLayout, VkImageLayout newLayout,
                                 VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                 VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = oldLayout;
        barrier.newLayout                       = newLayout;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.layerCount     = 1;
        barrier.srcAccessMask                   = srcAccess;
        barrier.dstAccessMask                   = dstAccess;
        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    };

    for (const VkFramePass& pass : m_framePasses) {
        if (pass.renderTargetId == 0 || pass.drawItemCount == 0) continue;

        auto itRt  = m_renderTargets.find(pass.renderTargetId);
        if (itRt == m_renderTargets.end()) continue;
        auto itTex = m_textures.find(itRt->second.textureId);
        if (itTex == m_textures.end()) continue;

        Color passClearColor = m_clearColor;
        if (pass.renderTargetId != 0) {
            if (itRt != m_renderTargets.end()) {
                passClearColor = itRt->second.clearColor;
            }
        }

        VkClearValue clearValue{};
        clearValue.color = {{
            NormalizeColorComponent(passClearColor.r),
            NormalizeColorComponent(passClearColor.g),
            NormalizeColorComponent(passClearColor.b),
            NormalizeColorComponent(passClearColor.a)
        }};

        VkRenderPassBeginInfo rtPassInfo{};
        rtPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rtPassInfo.renderPass        = m_offscreenRenderPass;
        rtPassInfo.framebuffer       = itRt->second.framebuffer;
        rtPassInfo.renderArea.extent = { pass.width, pass.height };
        rtPassInfo.clearValueCount   = 1;
        rtPassInfo.pClearValues      = &clearValue;
        vkCmdBeginRenderPass(cmd, &rtPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = static_cast<float>(pass.height);
        viewport.width    = static_cast<float>(pass.width);
        viewport.height   = -static_cast<float>(pass.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = { pass.width, pass.height };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        if (m_offscreenPipeline2D != VK_NULL_HANDLE) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_offscreenPipeline2D);
        }
        const VkPushConstants2D rtPushConstants{
            static_cast<float>(pass.width),
            static_cast<float>(pass.height)
        };
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(rtPushConstants), &rtPushConstants);
        vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertexBuffer, offsets);
        vkCmdBindIndexBuffer(cmd, frame.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        for (uint32_t i = 0; i < pass.drawItemCount; ++i) {
            const VkDrawItem& item = m_frameDrawItems[pass.firstDrawItem + i];
            if (item.descriptorSet == VK_NULL_HANDLE) continue;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout, 0, 1, &item.descriptorSet, 0, nullptr);
            vkCmdDrawIndexed(cmd, item.indexCount, 1, item.firstIndex, 0, 0);
        }

        vkCmdEndRenderPass(cmd);
    }

    VkClearValue clearValue{};
    clearValue.color = {{
        NormalizeColorComponent(m_clearColor.r),
        NormalizeColorComponent(m_clearColor.g),
        NormalizeColorComponent(m_clearColor.b),
        NormalizeColorComponent(m_clearColor.a)
    }};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass        = m_renderPass;
    renderPassInfo.framebuffer       = m_swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.extent = m_swapChainExtent;
    renderPassInfo.clearValueCount   = 1;
    renderPassInfo.pClearValues      = &clearValue;
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width    = static_cast<float>(m_swapChainExtent.width);
    viewport.height   = static_cast<float>(m_swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = m_swapChainExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (m_pipeline2D != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline2D);
    }
    const VkPushConstants2D screenPushConstants{
        static_cast<float>(m_swapChainExtent.width),
        static_cast<float>(m_swapChainExtent.height)
    };
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(screenPushConstants), &screenPushConstants);
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertexBuffer, offsets);
    vkCmdBindIndexBuffer(cmd, frame.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    for (const VkFramePass& pass : m_framePasses) {
        if (pass.renderTargetId != 0) continue;
        for (uint32_t i = 0; i < pass.drawItemCount; ++i) {
            const VkDrawItem& item = m_frameDrawItems[pass.firstDrawItem + i];
            if (item.descriptorSet == VK_NULL_HANDLE) continue;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout, 0, 1, &item.descriptorSet, 0, nullptr);
            vkCmdDrawIndexed(cmd, item.indexCount, 1, item.firstIndex, 0, 0);
        }
    }

    vkCmdEndRenderPass(cmd);
    return vkEndCommandBuffer(cmd) == VK_SUCCESS;
}

void QuarkVkRenderer::FlushBatch() {}

void QuarkVkRenderer::PushQuad(float x, float y, float w, float h, Color color,
                                float u0, float v0, float u1, float v1) {
    if (m_textures.find(m_whiteTextureId) == m_textures.end()) return;
    VkDescriptorSet ds = m_textures.at(m_whiteTextureId).descriptorSet;

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