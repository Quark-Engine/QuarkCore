/*
    ========================================================

        Quark Vulkan Renderer
        By Quark Engine Development Team

    --------------------------------------------------------

    Vulkan-based 2D/3D rendering backend for Quark Engine.

    This file contains:
        * Vulkan instance and device management
        * Swap chain creation and recreation
        * 2D batch rendering pipeline
        * Texture and descriptor set management
        * Frame synchronization

    Backend:
        * Vulkan (via vulkan.h)
        * SDL3 (surface creation via SDL_vulkan.h)

    --------------------------------------------------------

    THIRD-PARTY NOTICE:

        Vulkan and the Vulkan logo are registered trademarks
        of The Khronos Group Inc.

        Vulkan SDK are registered trademarks of LunarG, Inc.
        See: https://vulkan.lunarg.com/

        Vulkan is an open standard and cross-platform API
        maintained by The Khronos Group.
        See: https://www.khronos.org/vulkan/

    ========================================================
*/

#pragma once

#include "../QuarkIRenderer.hpp"
#include "../QuarkFont.hpp"
#include "QuarkCore/QuarkCore.hpp"

#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace qc {

static constexpr int    kVkMaxFramesInFlight  = 2;
static constexpr size_t kVkMaxBatchQuads      = 4096;
static constexpr size_t kVkMaxBatchVertices   = kVkMaxBatchQuads * 4;
static constexpr size_t kVkMaxBatchIndices    = kVkMaxBatchQuads * 6;

static constexpr size_t kVkMaxVerticesPerFrame = kVkMaxBatchVertices * 16;
static constexpr size_t kVkMaxIndicesPerFrame  = kVkMaxBatchIndices  * 16;

static constexpr uint32_t kVkDescriptorPoolSlabSize = 256;

struct VkQueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct VkSwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR        capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   presentModes;
};

struct VkBatchVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};

struct Vk3DVertex {
    float x, y, z, w;
    float u, v;
    float r, g, b, a;
    float nx, ny, nz, nw;
    float wx, wy, wz, ww;
};

struct Vk3DPushConstants {
    float lightPositions[4][4];
    float lightColors[4][4];
    float timeData[4];
    float lightEnabled[4];
};

struct VkPushConstants2D {
    float screenWidth;
    float screenHeight;
};

struct VkFrameData {
    VkCommandBuffer commandBuffer  = VK_NULL_HANDLE;
    VkSemaphore     imageAvailable = VK_NULL_HANDLE;
    VkSemaphore     renderFinished = VK_NULL_HANDLE;
    VkFence         inFlightFence  = VK_NULL_HANDLE;

    VkBuffer       vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    void*          vertexMapped = nullptr;
    VkDeviceSize   vertexCapacity = 0;

    VkBuffer       indexBuffer  = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory  = VK_NULL_HANDLE;
    void*          indexMapped  = nullptr;
    VkDeviceSize   indexCapacity = 0;

    VkBuffer       vertexBuffer3D = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory3D = VK_NULL_HANDLE;
    void*          vertexMapped3D = nullptr;
    VkDeviceSize   vertexCapacity3D = 0;
};

struct VkTextureData {
    VkImage         image         = VK_NULL_HANDLE;
    VkDeviceMemory  memory        = VK_NULL_HANDLE;
    VkImageView     view          = VK_NULL_HANDLE;
    VkSampler       sampler       = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    uint32_t width  = 0;
    uint32_t height = 0;
    bool      isRenderTarget = false;
};

struct VkDrawItem {
    uint32_t textureDescSet;
    uint32_t shaderProgramId = 0;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    uint32_t firstIndex  = 0;
    uint32_t indexCount  = 0;
};

struct Vk3DDrawItem {
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    uint32_t shaderProgramId = 0;
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
};

struct VkFramePass {
    uint32_t renderTargetId = 0;
    uint32_t firstDrawItem  = 0;
    uint32_t drawItemCount  = 0;
    uint32_t width          = 0;
    uint32_t height         = 0;
    uint32_t triFirstVertex  = 0;
    uint32_t triVertexCount  = 0;
    uint32_t lineFirstVertex = 0;
    uint32_t lineVertexCount = 0;
    uint32_t first3DDrawItem = 0;
    uint32_t drawItemCount3D = 0;
};
struct VkRenderTargetData {
    uint32_t      textureId   = 0;
    uint32_t      width       = 0;
    uint32_t      height      = 0;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    Color         clearColor  = {0, 0, 0, 255};
    VkImage       depthImage  = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView   depthView    = VK_NULL_HANDLE;

    std::vector<VkBatchVertex> vertices;
    std::vector<uint32_t>      indices;
    std::vector<VkDrawItem>    drawItems;
    std::vector<Vk3DVertex>    triangleVertices3D;
    std::vector<Vk3DVertex>    lineVertices3D;
    std::vector<Vk3DDrawItem>  drawItems3D;
};

struct VkShaderProgramData {
    VkShaderModule vertexModule   = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    VkPipeline     pipeline       = VK_NULL_HANDLE;
    VkPipeline     pipeline3D     = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet3D = VK_NULL_HANDLE;
    bool            supports3D    = false;
    std::unordered_map<std::string, int> uniforms;
    std::unordered_map<std::string, int> attributes;
    std::unordered_map<int, std::vector<uint8_t>> uniformValues;
    std::unordered_map<int, int> uniformTypes;
};

class QuarkVkRenderer final : public IRenderer {
public:
    QuarkVkRenderer() = default;
    ~QuarkVkRenderer() override;

    void Init(SDL_Window* window, int width, int height) override;
    void Shutdown() override;

    void BeginDrawing() override;
    void EndDrawing()   override;
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
    void   SetShaderValue(const Shader& shader, int locIndex, const qc::Vec4& value) override;
    void   SetShaderValueMatrix(const Shader& shader, int locIndex, const float* mat) override;
    void   SetShaderValueSampler(const Shader& shader, int locIndex, int textureUnit) override;
    void   SetShaderValue(const Shader& shader, int locIndex, const void* value, int uniformType) override;
    void   SetShaderValueV(const Shader& shader, int locIndex, const void* value, int uniformType, int count) override;
    void   SetShaderValueMatrix(const Shader& shader, int locIndex, const Matrix& mat) override;
    void   SetShaderValueTexture(const Shader& shader, int locIndex, const ITexture& texture) override;
    void   SetShaderValueTextureUnit(const Shader& shader, int locIndex, const ITexture& texture, int textureUnit) override;

    void BeginMode2D(const Camera2D& camera) override;
    void EndMode2D() override;
    void BeginMode3D(const Camera3D& camera) override;
    void EndMode3D() override;
    void Set3DLightEnabled(int index, bool enabled) override;
    Camera2D GetCamera2D()    const override { return m_camera2D; }
    bool     ShouldClose()    const override { return m_shouldClose; }
    void     SetShouldClose(bool v)          { m_shouldClose = v; }

    int   GetScreenWidth()  const override { return m_width; }
    int   GetScreenHeight() const override { return m_height; }
    void  SetTargetFPS(int fps)   override;
    bool  SetVSync(bool enabled)  override;
    float GetFrameTime()   const override { return m_frameTime; }

    void         PushMatrix() override;
    void         PopMatrix()  override;
    void         Translate(const Vec3& translation) override;
    void         Rotate(float angle, const Vec3& axis) override;
    void         Scale(const Vec3& scale) override;
    void         MultMatrix(const Mat4& matrix) override;
    const float* GetMatrixModelview()  override;
    const float* GetMatrixProjection() override;
    void         EnableBackfaceCulling()  override;
    void         DisableBackfaceCulling() override;

    Model LoadModel(const char* filePath) override;
    void  UnloadModel(Model& model) override;
    void  DrawModel(const Model& model, const Vec3& position, float scale,
                    float rotationX, float rotationY, float rotationZ) override;
    void  DrawModelEx(const Model& model, const Mat4& transform) override;
    void  DrawModelEx(const Model& model, const Mat4& transform, Color tint) override;

    void UploadMesh(Mesh& mesh, bool dynamic) override;
    void UpdateMeshBuffer(Mesh& mesh, int index, const void* data, int dataSize, int offset) override;
    void UnloadMesh(Mesh& mesh) override;
    void DrawMesh(const Mesh& mesh, const Material& material, const Mat4& transform) override;
    void DrawMeshInstanced(const Mesh& mesh, const Material& material, const Mat4* transforms, int instances) override;

    void RefreshViewport() override;

    RendererType GetType() const override { return RendererType::Vulkan; }

    VkInstance            GetVulkanInstance() const { return m_instance; }
    VkPhysicalDevice       GetVulkanPhysicalDevice() const { return m_physicalDevice; }
    VkDevice              GetVulkanDevice() const { return m_device; }
    uint32_t              GetVulkanGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    VkQueue               GetVulkanGraphicsQueue() const { return m_graphicsQueue; }
    VkDescriptorPool      GetVulkanDescriptorPool() const { return m_imguiDescriptorPool; }
    VkRenderPass          GetVulkanMainRenderPass() const { return m_renderPass; }
    uint32_t              GetVulkanMinImageCount() const { return m_swapChainMinImageCount; }
    uint32_t              GetVulkanImageCount() const { return static_cast<uint32_t>(m_swapChainImages.size()); }
    void                  SetMSAASamples(int samples);
    VkSampleCountFlagBits GetVulkanMSAASamples() const { return m_msaaSamples; }
    VkDescriptorSet       GetTextureDescriptorSet(uint32_t textureId) const;

private:
    void CreateInstance();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapChain();
    void CreateImageViews();
    void CreateRenderPass();
    void CreateOffscreenRenderPass();
    void CreateDescriptorSetLayout();
    void CreatePipeline2D();
    void CreateOffscreenPipeline2D();
    VkPipeline CreatePipelineForRenderPass(VkRenderPass renderPass,
                                           VkShaderModule vertexModule = VK_NULL_HANDLE,
                                           VkShaderModule fragmentModule = VK_NULL_HANDLE);
    void CreateShaderPipelines();
    VkPipeline Create3DPipelineForRenderPass(VkRenderPass renderPass, VkPrimitiveTopology topology,
                                             VkShaderModule vertexModule = VK_NULL_HANDLE,
                                             VkShaderModule fragmentModule = VK_NULL_HANDLE);
    void CreatePipeline3D();
    void CreateFramebuffers();
    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void CreateFrameVertexIndexBuffers();
    void CreateWhiteTexture();

    bool CreateDescriptorPoolSlab(uint32_t maxSets, VkDescriptorPool& outPool);
    bool AllocateTextureDescriptorSet(VkDescriptorSet& outSet);
    bool Allocate3DDescriptorSet(VkDescriptorSet& outSet);
        VkDescriptorSet CreateMaterialDescriptorSet(const Material& material);
    void Update3DDescriptorSet(VkShaderProgramData& program);

    void RecreateSwapChain();
    void CleanupSwapChain();
    bool RecreateRenderTargetFramebuffers();

    VkQueueFamilyIndices      FindQueueFamilies(VkPhysicalDevice device) const;
    VkSwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) const;
    bool                      IsDeviceSuitable(VkPhysicalDevice device) const;
    VkSurfaceFormatKHR        ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR          ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& modes) const;
    VkExtent2D                ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps) const;
    uint32_t                  FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;
    VkFormat                  FindSupportedFormat(const std::vector<VkFormat>& candidates,
                                                  VkImageTiling tiling,
                                                  VkFormatFeatureFlags features) const;
    VkFormat                  FindDepthFormat() const;

    bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags props,
                      VkBuffer& outBuffer, VkDeviceMemory& outMemory);
    bool EnsureMappedBufferCapacity(VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped,
                                    VkDeviceSize& capacity, VkDeviceSize required,
                                    VkBufferUsageFlags usage);

    bool TransitionImageLayout(VkImage image, VkFormat format,
                               VkImageLayout oldLayout, VkImageLayout newLayout);

    bool CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t w, uint32_t h);

    VkCommandBuffer BeginSingleTimeCommands();
    void            EndSingleTimeCommands(VkCommandBuffer cmd);

    bool     CreateTextureFromRGBA(const unsigned char* rgba,
                                   uint32_t width, uint32_t height,
                                   uint32_t& outId);
    void     DestroyTexture(uint32_t textureId);
    void     CreateMSAAColorResources();
    void     DestroyMSAAColorResources();
    VkSampleCountFlagBits GetSampleCountForSamples(int samples) const;
    bool     CreateDepthResources(uint32_t width, uint32_t height,
                                  VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView,
                                  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    void     DestroyDepthResources(VkImage& image, VkDeviceMemory& memory, VkImageView& view);

    IRenderTexture CreateRenderTargetInternal(int width, int height);
    void           DestroyRenderTargetInternal(uint32_t renderTargetId);

    VkShaderModule CreateShaderModule(const std::vector<uint32_t>& spirv);
    bool           ReadBinaryFile(const char* path, std::vector<char>& outData) const;

    void BuildCombinedFrameGeometry();
    bool UploadFrameGeometry(uint32_t frameIndex);

    void AppendQuadToBatch(std::vector<VkBatchVertex>& vertices,
                           std::vector<uint32_t>&      indices,
                           std::vector<VkDrawItem>&    drawItems,
                           VkDescriptorSet             ds,
                           float x0, float y0,
                           float x1, float y1,
                           float x2, float y2,
                           float x3, float y3,
                           float r, float g, float b, float a,
                           float u0 = 0.f, float v0 = 0.f,
                           float u1 = 1.f, float v1 = 1.f);

    void AppendQuad(VkDescriptorSet ds,
                    float x0, float y0,
                    float x1, float y1,
                    float x2, float y2,
                    float x3, float y3,
                    float r, float g, float b, float a,
                    float u0 = 0.f, float v0 = 0.f,
                    float u1 = 1.f, float v1 = 1.f);

    bool RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);

    void FlushBatch();
    void PushQuad(float x, float y, float w, float h, Color color,
                  float u0 = 0.f, float v0 = 0.f,
                  float u1 = 1.f, float v1 = 1.f);
    Vec2 ApplyCameraTransform(Vec2 position) const;
    void EnsureBatchTexture(VkDescriptorSet ds);
    struct Vk3DGeometryBatch {
        std::vector<Vk3DVertex> triangleVertices;
        std::vector<Vk3DVertex> lineVertices;
        std::vector<Vk3DDrawItem> drawItems;
        uint32_t shaderProgramId = 0;
    };

    std::vector<Vk3DVertex>&       GetActive3DTriangleVertices();
    const std::vector<Vk3DVertex>& GetActive3DTriangleVertices() const;
    std::vector<Vk3DVertex>&       GetActive3DLineVertices();
    const std::vector<Vk3DVertex>& GetActive3DLineVertices() const;
    Vk3DVertex               Transform3DVertex(Vec3 position, Color color) const;
    void                     AppendTriangle3D(std::vector<Vk3DVertex>& vertices,
                                              Vec3 a, Vec3 b, Vec3 c, Color color);
    void                     AppendLine3D(std::vector<Vk3DVertex>& vertices,
                                          Vec3 a, Vec3 b, Color color);
    void                     DrawSphereExInternal(Vec3 centerPos, float radius, int rings, int slices, Color color);

    struct GlyphData {
        Rectangle uv{};
        float     advanceX = 0.f;
        float     offsetX  = 0.f;
        float     offsetY  = 0.f;
        int       width    = 0;
        int       height   = 0;
    };

    struct FontData {
        uint32_t atlasTextureId = 0;
        int      baseSize        = 0;
        int      ascent          = 0;
        int      descent         = 0;
        int      lineHeight      = 0;
        int      lineGap         = 0;
        GlyphData glyphs[95]{};
    };

    bool     LoadFontInternal(const char* filePath, int pointSize, FontData& out);
    uint32_t EnsureDefaultFont();
    const FontData* GetFontData(IFont font) const;
    void DrawTextWithFontData(const FontData& fd, const char* text,
                              Vec2 position, float fontSize, float spacing, Color tint);
    Vec2 MeasureTextWithFontData(const FontData& fd, const char* text,
                                 float fontSize, float spacing) const;

    SDL_Window* m_window    = nullptr;
    int         m_width     = 0;
    int         m_height    = 0;
    int         m_targetFps = 60;
    bool        m_vsync     = true;
    bool        m_vsyncExplicitlySet = false;
    float       m_frameTime = 0.f;
    uint64_t    m_lastFrameCounter   = 0;
    bool        m_drawing            = false;
    bool        m_shouldClose        = false;
    bool        m_framebufferResized = false;
    bool        m_frameGeometryPending = false;

    VkInstance       m_instance       = VK_NULL_HANDLE;
    VkSurfaceKHR     m_surface        = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;
    VkQueue          m_presentQueue   = VK_NULL_HANDLE;

    VkSwapchainKHR           m_swapChain            = VK_NULL_HANDLE;
    std::vector<VkImage>     m_swapChainImages;
    std::vector<VkImageView> m_swapChainImageViews;
    std::vector<VkImage>     m_swapChainDepthImages;
    std::vector<VkDeviceMemory> m_swapChainDepthMemories;
    std::vector<VkImageView> m_swapChainDepthImageViews;
    VkFormat                 m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkFormat                 m_depthFormat          = VK_FORMAT_UNDEFINED;
    VkExtent2D               m_swapChainExtent      = {0, 0};

    VkRenderPass m_renderPass          = VK_NULL_HANDLE;
    VkRenderPass m_offscreenRenderPass = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> m_swapChainFramebuffers;

    VkDescriptorSetLayout         m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout         m_descriptorSetLayout3D = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> m_descriptorPools;
    VkDescriptorPool              m_imguiDescriptorPool = VK_NULL_HANDLE;
    VkBuffer                      m_3DDummyBuffer = VK_NULL_HANDLE;
    VkDeviceMemory                 m_3DDummyMemory = VK_NULL_HANDLE;
    void*                          m_3DDummyMapped = nullptr;

    VkPipelineLayout m_pipelineLayout      = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout3D    = VK_NULL_HANDLE;
    VkPipeline       m_pipeline2D          = VK_NULL_HANDLE;
    VkPipeline       m_offscreenPipeline2D = VK_NULL_HANDLE;
    VkPipeline       m_pipeline3DTri       = VK_NULL_HANDLE;
    VkPipeline       m_pipeline3DLines     = VK_NULL_HANDLE;
    VkPipeline       m_offscreenPipeline3DTri   = VK_NULL_HANDLE;
    VkPipeline       m_offscreenPipeline3DLines = VK_NULL_HANDLE;
    bool             m_backfaceCullingEnabled   = false;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;

    std::array<VkFrameData, kVkMaxFramesInFlight> m_frames{};
    uint32_t m_currentFrame = 0;
    uint32_t m_imageIndex   = 0;
    uint32_t m_graphicsQueueFamily = UINT32_MAX;
    uint32_t m_swapChainMinImageCount = 0;
    VkImage               m_msaaColorImage       = VK_NULL_HANDLE;
    VkDeviceMemory        m_msaaColorMemory      = VK_NULL_HANDLE;
    VkImageView           m_msaaColorImageView   = VK_NULL_HANDLE;
    int                   m_requestedMsaaSamples = 1;
    VkSampleCountFlagBits m_msaaSamples          = VK_SAMPLE_COUNT_1_BIT;

    uint32_t m_whiteTextureId = 0;
    uint32_t m_blackTextureId = 0;
    uint32_t m_flatNormalTextureId = 0;
    VkDescriptorSet m_white3DDescriptorSet = VK_NULL_HANDLE;

    uint32_t m_nextShaderProgramId = 1;
    uint32_t m_currentShaderProgramId = 0;
    std::array<bool, 4> m_3DLightEnabled{ true, true, true, true };
    std::unordered_map<uint32_t, VkShaderProgramData> m_shaderPrograms;

    uint32_t                                   m_nextTextureId = 1;
    std::unordered_map<uint32_t, VkTextureData> m_textures;
    std::unordered_map<uint32_t, FontData>      m_fonts;
    uint32_t                                   m_nextFontId = 1;
    uint32_t                                   m_defaultFontId = 0;

    uint32_t                                         m_nextRenderTargetId = 1;
    std::unordered_map<uint32_t, VkRenderTargetData> m_renderTargets;
    uint32_t                                         m_activeRenderTargetId = 0;

    std::vector<VkBatchVertex> m_batchVertices;
    std::vector<uint32_t>      m_batchIndices;
    std::vector<VkDrawItem>    m_batchDrawItems;
    Vk3DGeometryBatch          m_main3DBatch;

    std::vector<VkBatchVertex> m_frameVertices;
    std::vector<uint32_t>      m_frameIndices;
    std::vector<VkDrawItem>    m_frameDrawItems;
    std::vector<VkFramePass>   m_framePasses;
    std::vector<Vk3DVertex>    m_frameTriangleVertices3D;
    std::vector<Vk3DVertex>    m_frameLineVertices3D;
        std::vector<Vk3DDrawItem>  m_frame3DDrawItems;
    uint32_t                   m_frame3DShaderProgramId = 0;

    VkDescriptorSet m_currentDescriptorSet = VK_NULL_HANDLE;
    Color           m_clearColor           = {0, 0, 0, 255};

    Mat4              m_currentMatrix    = Mat4::identity();
    std::vector<Mat4> m_matrixStack;
    Mat4              m_viewMatrix       = Mat4::identity();
    Mat4              m_projectionMatrix = Mat4::identity();

    Camera2D m_camera2D{};
    bool     m_camera2DActive = false;
};

} // namespace qc
