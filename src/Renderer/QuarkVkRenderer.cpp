#include "QuarkVkRenderer.hpp"

#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <cstring>
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

    m_window = window;
    m_width = width;
    m_height = height;
    m_framebufferResized = false;

    CreateInstance();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapChain();
    CreateImageViews();
    CreateRenderPass();
    CreateFramebuffers();
    CreateCommandPool();
    CreateCommandBuffers();
    CreateSyncObjects();

    TraceLog(LogLevel::Info, "VULKAN", "Vulkan renderer initialized successfully.");
}

void QuarkVkRenderer::Shutdown() {
    TraceLog(LogLevel::Info, "VULKAN", "Shutting down Vulkan renderer...");

    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
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

    m_window = nullptr;
    m_width = 0;
    m_height = 0;
    m_drawing = false;
    m_currentFrame = 0;
    m_imageIndex = 0;
}

void QuarkVkRenderer::BeginDrawing() {
    if (!m_device || !m_swapChain || m_drawing) {
        return;
    }

    VkFrameData& frame = m_frames[m_currentFrame];

    vkWaitForFences(m_device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        m_device,
        m_swapChain,
        UINT64_MAX,
        frame.imageAvailable,
        VK_NULL_HANDLE,
        &m_imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        RecreateSwapChain();
        return;
    }

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to acquire next Vulkan swapchain image.");
    }

    m_drawing = true;
}

void QuarkVkRenderer::EndDrawing() {
    if (!m_drawing || !m_device || !m_swapChain) {
        return;
    }

    VkFrameData& frame = m_frames[m_currentFrame];
    VkCommandBuffer cmd = frame.commandBuffer;

    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin Vulkan command buffer.");
    }

    std::array<VkClearValue, 1> clearValues;
    clearValues[0].color = {{
        NormalizeColorComponent(m_clearColor.r),
        NormalizeColorComponent(m_clearColor.g),
        NormalizeColorComponent(m_clearColor.b),
        NormalizeColorComponent(m_clearColor.a)
    }};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapChainFramebuffers[m_imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapChainExtent;
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record Vulkan command buffer.");
    }

    vkResetFences(m_device, 1, &frame.inFlightFence);

    VkSemaphore waitSemaphores[] = { frame.imageAvailable };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { frame.renderFinished };

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, frame.inFlightFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit Vulkan command buffer.");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain;
    presentInfo.pImageIndices = &m_imageIndex;

    VkResult presentResult = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        m_framebufferResized = false;
        RecreateSwapChain();
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to present Vulkan swapchain image.");
    }

    m_currentFrame = (m_currentFrame + 1) % kVkMaxFramesInFlight;
    m_drawing = false;
}

void QuarkVkRenderer::ClearBackground(Color color) {
    m_clearColor = color;
}

void QuarkVkRenderer::DrawRectangle(float x, float y, float width, float height, Color color) {
    (void)x; (void)y; (void)width; (void)height; (void)color;
}

void QuarkVkRenderer::DrawRectangle(const Rectangle& rectangle, Color color) {
    DrawRectangle(rectangle.x, rectangle.y, rectangle.width, rectangle.height, color);
}

void QuarkVkRenderer::DrawRectangleV(Vec2 position, Vec2 size, Color color) {
    DrawRectangle(position.x, position.y, size.x, size.y, color);
}

void QuarkVkRenderer::DrawRectangleLines(Rectangle rectangle, float lineWidth, Color color) {
    (void)rectangle; (void)lineWidth; (void)color;
}

void QuarkVkRenderer::DrawRectangleRounded(Rectangle rectangle, float roundness, int segments, Color color) {
    (void)rectangle; (void)roundness; (void)segments; (void)color;
}

void QuarkVkRenderer::DrawCircle(float centerX, float centerY, float radius, Color color) {
    (void)centerX; (void)centerY; (void)radius; (void)color;
}

void QuarkVkRenderer::DrawCircleLines(float centerX, float centerY, float radius, Color color) {
    (void)centerX; (void)centerY; (void)radius; (void)color;
}

void QuarkVkRenderer::DrawEllipse(float centerX, float centerY, float radiusH, float radiusV, Color color) {
    (void)centerX; (void)centerY; (void)radiusH; (void)radiusV; (void)color;
}

void QuarkVkRenderer::DrawLine(float x1, float y1, float x2, float y2, Color color) {
    (void)x1; (void)y1; (void)x2; (void)y2; (void)color;
}

void QuarkVkRenderer::DrawLineV(Vec2 start, Vec2 end, Color color) {
    DrawLine(start.x, start.y, end.x, end.y, color);
}

void QuarkVkRenderer::DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color) {
    (void)v1; (void)v2; (void)v3; (void)color;
}

void QuarkVkRenderer::DrawPoly(Vec2 center, int sides, float radius, float rotation, Color color) {
    (void)center; (void)sides; (void)radius; (void)rotation; (void)color;
}

void QuarkVkRenderer::UploadMesh(Mesh& mesh, bool dynamic) {
    (void)mesh; (void)dynamic;
}

void QuarkVkRenderer::UpdateMeshBuffer(Mesh& mesh, int index, const void* data, int dataSize, int offset) {
    (void)mesh; (void)index; (void)data; (void)dataSize; (void)offset;
}

void QuarkVkRenderer::UnloadMesh(Mesh& mesh) {
    (void)mesh;
}

void QuarkVkRenderer::DrawMesh(const Mesh& mesh, const Material& material, const Mat4& transform) {
    (void)mesh; (void)material; (void)transform;
}

void QuarkVkRenderer::DrawMeshInstanced(const Mesh& mesh, const Material& material, const Mat4* transforms, int instances) {
    (void)mesh; (void)material; (void)transforms; (void)instances;
}

void QuarkVkRenderer::Set3DView(const Mat4& view, const Mat4& projection) {
    m_viewMatrix = view;
    m_projectionMatrix = projection;
}

void QuarkVkRenderer::DrawLine3D(Vec3 startPos, Vec3 endPos, Color color) {
    (void)startPos; (void)endPos; (void)color;
}

void QuarkVkRenderer::DrawPlane(Vec3 center, Vec2 size, Color color) {
    (void)center; (void)size; (void)color;
}

void QuarkVkRenderer::DrawCube(Vec3 position, float width, float height, float length, Color color) {
    (void)position; (void)width; (void)height; (void)length; (void)color;
}

void QuarkVkRenderer::DrawCubeV(Vec3 position, Vec3 size, Color color) {
    DrawCube(position, size.x, size.y, size.z, color);
}

void QuarkVkRenderer::DrawCubeWires(Vec3 position, float width, float height, float length, Color color) {
    (void)position; (void)width; (void)height; (void)length; (void)color;
}

void QuarkVkRenderer::DrawCubeWiresV(Vec3 position, Vec3 size, Color color) {
    DrawCubeWires(position, size.x, size.y, size.z, color);
}

void QuarkVkRenderer::DrawSphere(Vec3 centerPos, float radius, Color color) {
    (void)centerPos; (void)radius; (void)color;
}

void QuarkVkRenderer::DrawSphereEx(Vec3 centerPos, float radius, int rings, int slices, Color color) {
    (void)centerPos; (void)radius; (void)rings; (void)slices; (void)color;
}

void QuarkVkRenderer::DrawSphereWires(Vec3 centerPos, float radius, int rings, int slices, Color color) {
    (void)centerPos; (void)radius; (void)rings; (void)slices; (void)color;
}

void QuarkVkRenderer::DrawCylinder(Vec3 position, float radiusTop, float radiusBottom, float height, int slices, Color color) {
    (void)position; (void)radiusTop; (void)radiusBottom; (void)height; (void)slices; (void)color;
}

void QuarkVkRenderer::DrawCylinderEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius, int sides, Color color) {
    (void)startPos; (void)endPos; (void)startRadius; (void)endRadius; (void)sides; (void)color;
}

void QuarkVkRenderer::DrawCylinderWires(Vec3 position, float radiusTop, float radiusBottom, float height, int slices, Color color) {
    (void)position; (void)radiusTop; (void)radiusBottom; (void)height; (void)slices; (void)color;
}

void QuarkVkRenderer::DrawCylinderWiresEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius, int slices, Color color) {
    (void)startPos; (void)endPos; (void)startRadius; (void)endRadius; (void)slices; (void)color;
}

void QuarkVkRenderer::DrawGrid(int slices, float spacing) {
    (void)slices; (void)spacing;
}

void QuarkVkRenderer::DrawText(const char* text, int x, int y, int fontSize, Color color) {
    (void)text; (void)x; (void)y; (void)fontSize; (void)color;
}

void QuarkVkRenderer::DrawTextEx(IFont font, const char* text, Vec2 position, float fontSize, float spacing, Color tint) {
    (void)font; (void)text; (void)position; (void)fontSize; (void)spacing; (void)tint;
}

Vec2 QuarkVkRenderer::MeasureTextEx(IFont font, const char* text, float fontSize, float spacing) {
    (void)font;
    const int length = text ? static_cast<int>(std::strlen(text)) : 0;
    const float width = length * (fontSize * 0.5f + spacing);
    const float height = static_cast<float>(fontSize);
    return Vec2{width, height};
}

int QuarkVkRenderer::MeasureText(const char* text, int fontSize) {
    if (!text) return 0;
    return static_cast<int>(std::strlen(text)) * (fontSize / 2);
}

void QuarkVkRenderer::DrawTexture(const ITexture& texture, float x, float y, Color tint) {
    (void)texture; (void)x; (void)y; (void)tint;
}

void QuarkVkRenderer::DrawTextureV(const ITexture& texture, Vec2 position, Color tint) {
    DrawTexture(texture, position.x, position.y, tint);
}

void QuarkVkRenderer::DrawTextureEx(const ITexture& texture, Vec2 position, float rotation, float scale, Color tint) {
    (void)texture; (void)position; (void)rotation; (void)scale; (void)tint;
}

void QuarkVkRenderer::DrawTextureRec(const ITexture& texture, Rectangle source, Vec2 position, Color tint) {
    (void)texture; (void)source; (void)position; (void)tint;
}

void QuarkVkRenderer::DrawTexturePro(ITexture texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint) {
    (void)texture; (void)source; (void)dest; (void)origin; (void)rotation; (void)tint;
}

void QuarkVkRenderer::DrawTextureTiled(ITexture texture, float scale, Vec2 offset, Color tint) {
    (void)texture; (void)scale; (void)offset; (void)tint;
}

void QuarkVkRenderer::DrawTextureNPatch(ITexture texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint) {
    (void)texture; (void)source; (void)dest; (void)origin; (void)rotation; (void)tint;
}

ITexture QuarkVkRenderer::LoadTexture(const char* filePath) {
    (void)filePath;
    return ITexture{};
}

ITexture QuarkVkRenderer::GetRenderTextureTexture(IRenderTexture target) {
    (void)target;
    return ITexture{};
}

void QuarkVkRenderer::UnloadTexture(ITexture& texture) {
    (void)texture;
}

bool QuarkVkRenderer::isTextureValid(ITexture& texture) {
    (void)texture;
    return false;
}

IRenderTexture QuarkVkRenderer::LoadRenderTexture(int width, int height) {
    (void)width; (void)height;
    return IRenderTexture{};
}

void QuarkVkRenderer::UnloadRenderTexture(IRenderTexture target) {
    (void)target;
}

bool QuarkVkRenderer::isRenderTextureValid(IRenderTexture& target) {
    (void)target;
    return false;
}

ITexture QuarkVkRenderer::GenCheckerTexture(int width, int height, int cellSize, Color colorA, Color colorB) {
    (void)width; (void)height; (void)cellSize; (void)colorA; (void)colorB;
    return ITexture{};
}

void QuarkVkRenderer::BeginTextureMode(IRenderTexture target) {
    (void)target;
}

void QuarkVkRenderer::EndTextureMode() {
}

IFont QuarkVkRenderer::LoadFont(const char* filePath, int fontSize) {
    (void)filePath; (void)fontSize;
    return IFont{};
}

void QuarkVkRenderer::UnloadFont(IFont& font) {
    (void)font;
}

void QuarkVkRenderer::BeginShaderMode(const Shader& shader) {
    (void)shader;
}

void QuarkVkRenderer::EndShaderMode() {
}

Shader QuarkVkRenderer::LoadShader(const char* vsFileName, const char* fsFileName) {
    (void)vsFileName; (void)fsFileName;
    return Shader{};
}

Shader QuarkVkRenderer::LoadShaderFromMemory(const char* vsSource, const char* fsSource) {
    (void)vsSource; (void)fsSource;
    return Shader{};
}

void QuarkVkRenderer::UnloadShader(Shader& shader) {
    (void)shader;
}

bool QuarkVkRenderer::isShaderValid(Shader& shader) {
    (void)shader;
    return false;
}

int QuarkVkRenderer::GetShaderLocation(const Shader& shader, const char* uniformName) {
    (void)shader; (void)uniformName;
    return -1;
}

int QuarkVkRenderer::GetShaderLocation(const Shader& shader, ShaderLocationIndex locIndex) {
    (void)shader; (void)locIndex;
    return -1;
}

int QuarkVkRenderer::GetShaderAttributeLocation(const Shader& shader, const char* attribName) {
    (void)shader; (void)attribName;
    return -1;
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, float value) {
    (void)shader; (void)locIndex; (void)value;
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, int value) {
    (void)shader; (void)locIndex; (void)value;
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const Color& value) {
    (void)shader; (void)locIndex; (void)value;
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const qc::Vec2& value) {
    (void)shader; (void)locIndex; (void)value;
}

void QuarkVkRenderer::SetShaderValue(const Shader& shader, int locIndex, const qc::Vec3& value) {
    (void)shader; (void)locIndex; (void)value;
}

void QuarkVkRenderer::SetShaderValueMatrix(const Shader& shader, int locIndex, const float* mat) {
    (void)shader; (void)locIndex; (void)mat;
}

void QuarkVkRenderer::SetShaderValueSampler(const Shader& sfhader, int locIndex, int textureUnit) {
    (void)sfhader; (void)locIndex; (void)textureUnit;
}

void QuarkVkRenderer::BeginMode2D(const Camera2D& camera) {
    m_camera2D = camera;
    m_camera2DActive = true;
}

void QuarkVkRenderer::EndMode2D() {
    m_camera2DActive = false;
}

void QuarkVkRenderer::BeginMode3D(const Camera3D& camera) {
    m_viewMatrix = Mat4::lookAt(camera.position, camera.target, camera.up);
    if (camera.projection == CAMERA_PERSPECTIVE) {
        m_projectionMatrix = Mat4::perspective(camera.fovy * 3.14159265f / 180.0f, static_cast<float>(m_width) / static_cast<float>(m_height), 0.1f, 1000.0f);
    } else {
        m_projectionMatrix = Mat4::identity();
    }
}

void QuarkVkRenderer::EndMode3D() {
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

void QuarkVkRenderer::Translate(const Vec3& translation) {
    m_currentMatrix = m_currentMatrix * Mat4::translation(translation.x, translation.y, translation.z);
}

void QuarkVkRenderer::Rotate(float angle, const Vec3& axis) {
    if (axis.x == 1.0f && axis.y == 0.0f && axis.z == 0.0f) {
        m_currentMatrix = m_currentMatrix * Mat4::rotationX(angle);
    } else if (axis.y == 1.0f && axis.x == 0.0f && axis.z == 0.0f) {
        m_currentMatrix = m_currentMatrix * Mat4::rotationY(angle);
    } else if (axis.z == 1.0f && axis.x == 0.0f && axis.y == 0.0f) {
        m_currentMatrix = m_currentMatrix * Mat4::rotationZ(angle);
    }
}

void QuarkVkRenderer::Scale(const Vec3& scale) {
    m_currentMatrix = m_currentMatrix * Mat4::scale(scale.x, scale.y, scale.z);
}

void QuarkVkRenderer::MultMatrix(const Mat4& matrix) {
    m_currentMatrix = m_currentMatrix * matrix;
}

const float* QuarkVkRenderer::GetMatrixModelview() {
    return m_currentMatrix.m;
}

const float* QuarkVkRenderer::GetMatrixProjection() {
    return m_projectionMatrix.m;
}

void QuarkVkRenderer::EnableBackfaceCulling() {
}

void QuarkVkRenderer::DisableBackfaceCulling() {
}

Model QuarkVkRenderer::LoadModel(const char* filePath) {
    (void)filePath;
    return Model{};
}

void QuarkVkRenderer::UnloadModel(Model& model) {
    (void)model;
}

void QuarkVkRenderer::DrawModel(const Model& model, const Vec3& position, float scale, float rotationX, float rotationY, float rotationZ) {
    (void)model; (void)position; (void)scale; (void)rotationX; (void)rotationY; (void)rotationZ;
}

void QuarkVkRenderer::DrawModelEx(const Model& model, const Mat4& transform) {
    (void)model; (void)transform;
}

void QuarkVkRenderer::DrawModelEx(const Model& model, const Mat4& transform, Color tint) {
    (void)model; (void)transform; (void) tint;
}

void QuarkVkRenderer::RefreshViewport() {
    m_framebufferResized = true;
}

void QuarkVkRenderer::CreateInstance() {
    if (m_instance != VK_NULL_HANDLE) {
        return;
    }

    unsigned int extensionCount = 0;
    const char* const* extensionsData = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (!extensionsData) {
        throw std::runtime_error("SDL_Vulkan_GetInstanceExtensions failed to retrieve instance extensions.");
    }

    std::vector<const char*> extensions(extensionsData, extensionsData + extensionCount);

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "QuarkCore Vulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "QuarkCore";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = 0;

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance.");
    }
    TraceLog(LogLevel::Info, "VULKAN", "Vulkan instance created.");
}

void QuarkVkRenderer::CreateSurface() {
    if (m_surface != VK_NULL_HANDLE) {
        return;
    }
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

    VkPhysicalDeviceProperties deviceProperties;
    for (const auto& device : devices) {
        if (IsDeviceSuitable(device)) {
            m_physicalDevice = device;
            vkGetPhysicalDeviceProperties(device, &deviceProperties);
            break;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find a suitable Vulkan physical device.");
    }

    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Selected physical device: %s", deviceProperties.deviceName));
}

void QuarkVkRenderer::CreateLogicalDevice() {
    VkQueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(kDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();
    createInfo.enabledLayerCount = 0;

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan logical device.");
    }

    vkGetDeviceQueue(m_device, indices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily.value(), 0, &m_presentQueue);
    TraceLog(LogLevel::Info, "VULKAN", "Logical device and queues created.");
}

void QuarkVkRenderer::CreateSwapChain() {
    VkSwapChainSupportDetails details = QuerySwapChainSupport(m_physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(details.formats);
    VkPresentModeKHR presentMode = ChooseSwapPresentMode(details.presentModes);
    VkExtent2D extent = ChooseSwapExtent(details.capabilities);

    uint32_t imageCount = details.capabilities.minImageCount + 1;
    if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount) {
        imageCount = details.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkQueueFamilyIndices indices = FindQueueFamilies(m_physicalDevice);
    uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = details.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan swap chain.");
    }

    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
    m_swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, m_swapChainImages.data());

    m_swapChainImageFormat = surfaceFormat.format;
    m_swapChainExtent = extent;

    TraceLog(LogLevel::Info, "VULKAN", TextFormat("Swapchain created (Extent: %ux%u, Format: %d)", 
        m_swapChainExtent.width, m_swapChainExtent.height, m_swapChainImageFormat));
}

void QuarkVkRenderer::CreateImageViews() {
    m_swapChainImageViews.resize(m_swapChainImages.size());

    for (size_t i = 0; i < m_swapChainImages.size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_swapChainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device, &createInfo, nullptr, &m_swapChainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan image views.");
        }
    }
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Created %zu image views.", m_swapChainImageViews.size()));
}

void QuarkVkRenderer::CreateRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan render pass.");
    }
    TraceLog(LogLevel::Trace, "VULKAN", "Render pass created.");
}

void QuarkVkRenderer::CreateFramebuffers() {
    m_swapChainFramebuffers.resize(m_swapChainImageViews.size());

    for (size_t i = 0; i < m_swapChainImageViews.size(); ++i) {
        VkImageView attachments[] = { m_swapChainImageViews[i] };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapChainExtent.width;
        framebufferInfo.height = m_swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan framebuffer.");
        }
    }
    TraceLog(LogLevel::Trace, "VULKAN", TextFormat("Created %zu framebuffers.", m_swapChainFramebuffers.size()));
}

void QuarkVkRenderer::CreateCommandPool() {
    VkQueueFamilyIndices queueFamilyIndices = FindQueueFamilies(m_physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan command pool.");
    }
    TraceLog(LogLevel::Trace, "VULKAN", "Command pool created.");
}

void QuarkVkRenderer::CreateCommandBuffers() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
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

void QuarkVkRenderer::CreateVertexBuffers() {
}

void QuarkVkRenderer::CreateIndexBuffer() {
}

void QuarkVkRenderer::CreateWhiteTexture() {
}

void QuarkVkRenderer::CreateDescriptorPool() {
}

void QuarkVkRenderer::CreateDescriptorSets() {
}

void QuarkVkRenderer::RecreateSwapChain() {
    TraceLog(LogLevel::Info, "VULKAN", "Recreating swapchain...");
    vkDeviceWaitIdle(m_device);
    CleanupSwapChain();
    CreateSwapChain();
    CreateImageViews();
    CreateFramebuffers();
}

void QuarkVkRenderer::CleanupSwapChain() {
    TraceLog(LogLevel::Trace, "VULKAN", "Cleaning up swapchain resources...");

    for (auto framebuffer : m_swapChainFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device, framebuffer, nullptr);
        }
    }
    m_swapChainFramebuffers.clear();

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
}

VkQueueFamilyIndices QuarkVkRenderer::FindQueueFamilies(VkPhysicalDevice device) const {
    VkQueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = static_cast<uint32_t>(i);
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, static_cast<uint32_t>(i), m_surface, &presentSupport);

        if (presentSupport) {
            indices.presentFamily = static_cast<uint32_t>(i);
        }

        if (indices.isComplete()) {
            break;
        }

        ++i;
    }

    return indices;
}

VkSwapChainSupportDetails QuarkVkRenderer::QuerySwapChainSupport(VkPhysicalDevice device) const {
    VkSwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

bool QuarkVkRenderer::IsDeviceSuitable(VkPhysicalDevice device) const {
    VkQueueFamilyIndices indices = FindQueueFamilies(device);
    if (!indices.isComplete()) {
        return false;
    }

    VkSwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device);
    bool extensionsSupported = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    return extensionsSupported;
}

VkSurfaceFormatKHR QuarkVkRenderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const {
    for (const auto& availableFormat : formats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
}

VkPresentModeKHR QuarkVkRenderer::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& modes) const {
    for (const auto& availablePresentMode : modes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D QuarkVkRenderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps) const {
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }

    VkExtent2D actualExtent = { static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height) };
    actualExtent.width = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, actualExtent.width));
    actualExtent.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, actualExtent.height));
    return actualExtent;
}

uint32_t QuarkVkRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProperties.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable Vulkan memory type.");
}

void QuarkVkRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& outBuffer, VkDeviceMemory& outMemory) {
    (void)size; (void)usage; (void)props; (void)outBuffer; (void)outMemory;
}

VkShaderModule QuarkVkRenderer::CreateShaderModule(const std::vector<uint32_t>& spirv) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan shader module.");
    }
    return shaderModule;
}

VkCommandBuffer QuarkVkRenderer::BeginSingleTimeCommands() {
    return VK_NULL_HANDLE;
}

void QuarkVkRenderer::EndSingleTimeCommands(VkCommandBuffer cmd) {
    (void)cmd;
}

void QuarkVkRenderer::TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    (void)image; (void)oldLayout; (void)newLayout;
}

void QuarkVkRenderer::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t w, uint32_t h) {
    (void)buffer; (void)image; (void)w; (void)h;
}

void QuarkVkRenderer::FlushBatch() {
}

void QuarkVkRenderer::PushQuad(float x, float y, float w, float h, Color color, float u0, float v0, float u1, float v1) {
    (void)x; (void)y; (void)w; (void)h; (void)color; (void)u0; (void)v0; (void)u1; (void)v1;
}

void QuarkVkRenderer::EnsureBatchTexture(VkDescriptorSet ds) {
    (void)ds;
}

} // namespace qc
