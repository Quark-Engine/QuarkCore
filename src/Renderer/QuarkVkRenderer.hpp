#pragma once

#include "QuarkIRenderer.hpp"
#include "QuarkCore/QuarkCore.hpp"
#include "QuarkFont.hpp"

#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace qc {

static constexpr int    kVkMaxFramesInFlight = 2;
static constexpr size_t kVkMaxBatchQuads     = 4096;
static constexpr size_t kVkMaxBatchVertices  = kVkMaxBatchQuads * 4;
static constexpr size_t kVkMaxBatchIndices   = kVkMaxBatchQuads * 6;

struct VkQueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct VkSwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct VkBatchVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};

struct VkPushConstants2D {
    float screenWidth;
    float screenheight;
};

struct VkFrameData {
    VkCommandBuffer commandBuffer  = VK_NULL_HANDLE;
    VkSemaphore     imageAvailable = VK_NULL_HANDLE;
    VkSemaphore     renderFinished = VK_NULL_HANDLE;
    VkFence         inFlightFence  = VK_NULL_HANDLE;

    VkBuffer       vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    void*          vertexMapped = nullptr;
};

struct VkTextureData {
    VkImage         image         = VK_NULL_HANDLE;
    VkDeviceMemory  memory        = VK_NULL_HANDLE;
    VkImageView     view          = VK_NULL_HANDLE;
    VkSampler       sampler       = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    int width = 0;
    int height = 0;
};


class QCAPI QuarkVkRenderer final : public IRenderer {
public:
    QuarkVkRenderer() = default;
    ~QuarkVkRenderer() override;

    void Init(SDL_Window* window, int width, int height);
    void Shutdown();

    void BeginDrawing() override;
    void EndDrawing() override;
    void ClearBackground(Color color) override;

    void DrawRectangle(float x, float y, float width, float height, Color color) override;
    void DrawRectangle(const Rectangle& rectangle, Color color) override;
    void DrawRectangleV(Vec2 position, Vec2 size, Color color) override;
    void DrawRectangleLines(Rectangle rectangle, float lineWidth, Color color) override;
    void DrawRectangleRounded(Rectangle rectangle, float roundness, int segments, Color color) override;
    void DrawCircle(float centerX, float centerY, float radius, Color color) override;
    void DrawCircleLines(float centerX, float centerY, float radius, Color color) override;
    void DrawEllipse(float centerX, float centerY, float radiusH, float radiusV, Color color) override;
    void DrawLine(float x1, float y1, float x2, float y2, Color color) override;
    void DrawLineV(Vec2 start, Vec2 end, Color color) override;
    void DrawTriangle(Vec2 v1, Vec2 v2, Vec2 v3, Color color) override;
    void DrawPoly(Vec2 center, int sides, float radius, float rotation, Color color) override;

    void Set3DView(const Mat4& view, const Mat4& projection) override;
    void DrawLine3D(Vec3 startPos, Vec3 endPos, Color color) override;
    void DrawPlane(Vec3 center, Vec2 size, Color color) override;
    void DrawCube(Vec3 position, float width, float height, float length, Color color) override;
    void DrawCubeV(Vec3 position, Vec3 size, Color color) override;
    void DrawCubeWires(Vec3 position, float width, float height, float length, Color color) override;
    void DrawCubeWiresV(Vec3 position, Vec3 size, Color color) override;
    void DrawSphere(Vec3 centerPos, float radius, Color color) override;
    void DrawSphereEx(Vec3 centerPos, float radius, int rings, int slices, Color color) override;
    void DrawSphereWires(Vec3 centerPos, float radius, int rings, int slices, Color color) override;
    void DrawCylinder(Vec3 position, float radiusTop, float radiusBottom, float height, int slices, Color color) override;
    void DrawCylinderEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius, int sides, Color color) override;
    void DrawCylinderWires(Vec3 position, float radiusTop, float radiusBottom, float height, int slices, Color color) override;
    void DrawCylinderWiresEx(Vec3 startPos, Vec3 endPos, float startRadius, float endRadius, int slices, Color color) override;
    void DrawGrid(int slices, float spacing) override;

    void DrawText(const char* text, int x, int y, int fontSize, Color color) override;
    void DrawTextEx(IFont font, const char* text, Vec2 position, float fontSize, float spacing, Color tint) override;
    Vec2 MeasureTextEx(IFont font, const char* text, float fontSize, float spacing) override;
    int  MeasureText(const char* text, int fontSize) override;

    void           DrawTexture(const ITexture& texture, float x, float y, Color tint) override;
    void           DrawTextureV(const ITexture& texture, Vec2 position, Color tint) override;
    void           DrawTextureEx(const ITexture& texture, Vec2 position, float rotation, float scale, Color tint) override;
    void           DrawTextureRec(const ITexture& texture, Rectangle source, Vec2 position, Color tint) override;
    void           DrawTexturePro(ITexture texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint) override;
    void           DrawTextureTiled(ITexture texture, float scale, Vec2 offset, Color tint) override;
    void           DrawTextureNPatch(ITexture texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint) override;
    ITexture       LoadTexture(const char* filePath) override;
    ITexture       GetRenderTextureTexture(IRenderTexture target) override;
    void           UnloadTexture(ITexture& texture) override;
    bool           isTextureValid(ITexture& texture) override;
    IRenderTexture LoadRenderTexture(int width, int height) override;
    void           UnloadRenderTexture(IRenderTexture target) override;
    bool           isRenderTextureValid(IRenderTexture& target) override;
    ITexture       GenCheckerTexture(int width, int height, int cellSize, Color colorA, Color colorB) override;

    void BeginTextureMode(IRenderTexture target) override;
    void EndTextureMode() override;

    IFont LoadFont(const char* filePath, int fontSize) override;
    void  UnloadFont(IFont& font) override;

    void   BeginShaderMode(const Shader& shader) override;
    void   EndShaderMode() override;
    Shader LoadShader(const char* vsFileName, const char* fsFileName) override;
    Shader LoadShaderFromMemory(const char* vsSource, const char* fsSource) override;
    void   UnloadShader(Shader& shader) override;
    bool   isShaderValid(Shader& shader) override;
    int    GetShaderLocation(const Shader& shader, const char* uniformName) override;
    int    GetShaderLocation(const Shader& shader, ShaderLocationIndex locIndex) override;
    int    GetShaderAttributeLocation(const Shader& shader, const char* attribName) override;
    void   SetShaderValue(const Shader& shader, int locIndex, float value) override;
    void   SetShaderValue(const Shader& shader, int locIndex, int value) override;
    void   SetShaderValue(const Shader& shader, int locIndex, const Color& value) override;
    void   SetShaderValue(const Shader& shader, int locIndex, const qc::Vec2& value) override;
    void   SetShaderValue(const Shader& shader, int locIndex, const qc::Vec3& value) override;
    void   SetShaderValueMatrix(const Shader& shader, int locIndex, const float* mat) override;
    void   SetShaderValueSampler(const Shader& sfhader, int locIndex, int textureUnit) override;

    void BeginMode2D(const Camera2D& camera) override;
    void EndMode2D() override;
    void BeginMode3D(const Camera3D& camera) override;
    void EndMode3D() override;
    Camera2D GetCamera2D() const { return m_camera2D; }

    void         PushMatrix() override;
    void         PopMatrix() override;
    void         Translate(const Vec3& translation) override;
    void         Rotate(float angle, const Vec3& axis) override;
    void         Scale(const Vec3& scale) override;
    void         MultMatrix(const Mat4& matrix) override;
    const float* GetMatrixModelview() override;
    const float* GetMatrixProjection() override;
    void         EnableBackfaceCulling() override;
    void         DisableBackfaceCulling() override;

    Model LoadModel(const char* filePath) override;
    void  UnloadModel(Model& model) override;
    void  DrawModel(const Model& model, const Vec3& position, float scale, float rotationX, float rotationY, float rotationZ) override;
    void  DrawModelEx(const Model& model, const Mat4& transform) override;

    int GetScreenWidth()  const { return m_width; }
    int GetScreenHeight() const { return m_height; }
    void SetTargetFPS(int fps)   { m_targetFps = fps; }
    float GetFrameTime()   const { return m_frameTime; }

    void RefreshViewport();


    RendererType GetType() const override { return RendererType::Vulkan; }

private:
    void CreateInstance();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapChain();
    void CreateImageViews();
    void CreateRenderPass();
    void CreateDescriptorSetLayout();
    void CreatePipeline2D();
    void CreateFramebuffers();
    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void CreateVertexBuffers();
    void CreateIndexBuffer();
    void CreateWhiteTexture();
    void CreateDescriptorPool();
    void CreateDescriptorSets();
 
    void RecreateSwapChain();
    void CleanupSwapChain();
 
    VkQueueFamilyIndices      FindQueueFamilies(VkPhysicalDevice device) const;
    VkSwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) const;
    bool                      IsDeviceSuitable(VkPhysicalDevice device) const;
    VkSurfaceFormatKHR        ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR          ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& modes) const;
    VkExtent2D                ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps) const;
    uint32_t                  FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;
 
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& outBuffer, VkDeviceMemory& outMemory);
 
    VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv);
    VkCommandBuffer BeginSingleTimeCommands();
    void            EndSingleTimeCommands(VkCommandBuffer cmd);
 
    void TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout); 
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t w, uint32_t h);

    void FlushBatch();
    void PushQuad(float x, float y, float w, float h, Color color, float u0 = 0.f, float v0 = 0.f, float u1 = 1.f, float v1 = 1.f);
    void EnsureBatchTexture(VkDescriptorSet ds);
 
    SDL_Window* m_window = nullptr;
    int         m_width  = 0;
    int         m_height = 0;
    int         m_targetFps  = 60;
    float       m_frameTime  = 0.f;
    uint64_t    m_lastFrameCounter = 0;
    bool        m_drawing    = false;
    bool        m_framebufferResized = false;
 
    VkInstance               m_instance       = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface        = VK_NULL_HANDLE;
    VkPhysicalDevice         m_physicalDevice = VK_NULL_HANDLE;
    VkDevice                 m_device         = VK_NULL_HANDLE;
    VkQueue                  m_graphicsQueue  = VK_NULL_HANDLE;
    VkQueue                  m_presentQueue   = VK_NULL_HANDLE;
 
    VkSwapchainKHR           m_swapChain            = VK_NULL_HANDLE;
    std::vector<VkImage>     m_swapChainImages;
    std::vector<VkImageView> m_swapChainImageViews;
    VkFormat                 m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D               m_swapChainExtent      = {0, 0};
 
    VkRenderPass                  m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>    m_swapChainFramebuffers;
 
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
    VkPipeline            m_pipeline2D          = VK_NULL_HANDLE;
 
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
 
    std::array<VkFrameData, kVkMaxFramesInFlight> m_frames{};
    uint32_t m_currentFrame    = 0;
    uint32_t m_imageIndex      = 0;
 
    VkBuffer       m_indexBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory m_indexBufferMemory = VK_NULL_HANDLE;
 
    VkImage        m_whiteImage       = VK_NULL_HANDLE;
    VkDeviceMemory m_whiteImageMemory = VK_NULL_HANDLE;
    VkImageView    m_whiteImageView   = VK_NULL_HANDLE;
    VkSampler      m_whiteSampler     = VK_NULL_HANDLE;
 
    VkDescriptorPool m_descriptorPool      = VK_NULL_HANDLE;
    VkDescriptorSet  m_whiteDescriptorSet  = VK_NULL_HANDLE;
 
    std::vector<VkBatchVertex> m_batchVertices;
    VkDescriptorSet            m_currentDescriptorSet = VK_NULL_HANDLE;
    Color                      m_clearColor           = {0, 0, 0, 255};
 
    Mat4              m_currentMatrix = Mat4::identity();
    std::vector<Mat4> m_matrixStack;
    Mat4              m_viewMatrix       = Mat4::identity();
    Mat4              m_projectionMatrix = Mat4::identity();
 
    Camera2D m_camera2D{};
    bool     m_camera2DActive = false;

};


} // namespace qc