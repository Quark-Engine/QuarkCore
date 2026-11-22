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

#ifndef __QUARK_VK_RENDERER__
#define __QUARK_VK_RENDERER__

#include "../QuarkIRenderer.hpp"
#include "../QuarkFont.hpp"
#include "../QuarkRenderThreadPool.hpp"
#include "QuarkCore/QuarkCore.hpp"
#include "QuarkVkCommandContext.hpp"
#include "QuarkVkCommon.hpp"
#include "QuarkVkDescriptorSetManager.hpp"
#include "QuarkVkDevice.hpp"
#include "QuarkVkFrameManager.hpp"
#include "QuarkVkFramebufferManager.hpp"
#include "QuarkVkGpuAllocator.hpp"
#include "QuarkVkPipeline.hpp"
#include "QuarkVkRenderPass.hpp"
#include "QuarkVkRenderTarget.hpp"
#include "QuarkVkResources.hpp"
#include "QuarkVkShaderCompiler.hpp"
#include "QuarkVkSwapChain.hpp"

#include <SDL3/SDL_vulkan.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace qci
{
    static constexpr size_t VK_MAX_BATCH_QUADS = 4096;
    static constexpr size_t VK_MAX_BATCH_VERTICES = VK_MAX_BATCH_QUADS * 4;
    static constexpr size_t VK_MAX_BATCH_INDICES = VK_MAX_BATCH_QUADS * 6;

    static constexpr size_t VK_MAX_VERTICES_PER_FRAME = VK_MAX_BATCH_VERTICES * 16;
    static constexpr size_t VK_MAX_INDICES_PER_FRAME = VK_MAX_BATCH_INDICES * 16;
    static constexpr VkDeviceSize VK_3D_UNIFORM_FRAME_STRIDE = 4096;
    static constexpr size_t VK_MAX_INSTANCES_PER_FRAME = 4096;

    struct VkInstanceTransformData
    {
        float m_aModelViewProjection[16];
        float m_aModel[16];
    };

    struct VkFrameData
    {
        VkBuffer m_VertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_VertexMemory = VK_NULL_HANDLE;
        VmaAllocation m_VertexAllocation = VK_NULL_HANDLE;
        void* m_pVertexMapped = nullptr;
        VkDeviceSize m_VertexCapacity = 0;

        VkBuffer m_IndexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_IndexMemory = VK_NULL_HANDLE;
        VmaAllocation m_IndexAllocation = VK_NULL_HANDLE;
        void* m_pIndexMapped = nullptr;
        VkDeviceSize m_IndexCapacity = 0;

        VkBuffer m_VertexBuffer3D = VK_NULL_HANDLE;
        VkDeviceMemory m_VertexMemory3D = VK_NULL_HANDLE;
        VmaAllocation m_Vertex3DAllocation = VK_NULL_HANDLE;
        void* m_pVertexMapped3D = nullptr;
        VkDeviceSize m_VertexCapacity3D = 0;
    };

    struct VkCachedTexture
    {
        ITexture m_Texture{};
        int m_References = 0;
    };

    struct VkDrawItem
    {
        uint32_t m_TextureDescSet;
        uint32_t m_ShaderProgramId = 0;
        VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
        uint32_t m_FirstIndex = 0;
        uint32_t m_IndexCount = 0;
        bool m_DebugOverlay = false;
        int m_BlendMode = BLEND_ALPHA;
    };

    struct Vk3DDrawItem
    {
        VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
        uint32_t m_ShaderProgramId = 0;
        uint32_t m_FirstVertex = 0;
        uint32_t m_VertexCount = 0;
        uint32_t m_FirstIndex = 0;
        uint32_t m_IndexCount = 0;
        bool m_Indexed = false;
        bool m_GpuMesh = false;
        bool m_UseInstanceBuffer = false;
        bool m_BackfaceCulling = false;
        uint32_t m_InstanceCount = 1;
        uint32_t m_FirstInstance = 0;
        VkBuffer m_GpuVertexBuffer = VK_NULL_HANDLE;
        VkBuffer m_GpuIndexBuffer = VK_NULL_HANDLE;
        Mat4 m_ModelViewProjection = Mat4::identity();
        Mat4 m_Model = Mat4::identity();
        std::array<float, 4> m_aMaterialColor{1.0f, 1.0f, 1.0f, 1.0f};
    };

    struct VkGpuMeshData
    {
        VkBuffer m_VertexBuffer = VK_NULL_HANDLE;
        VmaAllocation m_VertexAllocation = VK_NULL_HANDLE;
        VkBuffer m_IndexBuffer = VK_NULL_HANDLE;
        VmaAllocation m_IndexAllocation = VK_NULL_HANDLE;
        uint32_t m_IndexCount = 0;
    };

    struct VkLight3D
    {
        Vec3 m_Position{0.0f, 0.0f, 0.0f};
        Vec3 m_Target{0.0f, 0.0f, 0.0f};
        Vec3 m_Color{1.0f, 1.0f, 1.0f};
        float m_Attenuation = 0.08f;
        int m_Type = 1;
        bool m_Enabled = true;
    };

    struct VkFramePass
    {
        uint32_t m_RenderTargetId = 0;
        uint32_t m_FirstDrawItem = 0;
        uint32_t m_DrawItemCount = 0;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        uint32_t m_TriFirstVertex = 0;
        uint32_t m_TriVertexCount = 0;
        uint32_t m_LineFirstVertex = 0;
        uint32_t m_LineVertexCount = 0;
        uint32_t m_First3DDrawItem = 0;
        uint32_t m_DrawItemCount3D = 0;
    };
    struct VkRenderTargetData
    {
        uint32_t m_TextureId = 0;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        VkImageLayout m_ImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        Color m_ClearColor = {0, 0, 0, 255};

        std::vector<VkBatchVertex> m_vVertices;
        std::vector<uint32_t> m_vIndices;
        std::vector<VkDrawItem> m_vDrawItems;
        std::vector<Vk3DVertex> m_vTriangleVertices3D;
        std::vector<uint32_t> m_vTriangleIndices3D;
        std::vector<Vk3DVertex> m_vLineVertices3D;
        std::vector<Vk3DDrawItem> m_vDrawItems3D;
    };

    struct VkMaterialCacheEntry
    {
        VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        uint64_t m_Key = 0;
        std::vector<uint32_t> m_vTextureIds;
    };

    class QuarkVkRenderer final : public IRenderer
    {
    public:
        QuarkVkRenderer() = default;
        ~QuarkVkRenderer() override;

        void Init(SDL_Window* pWindow, int width, int height) override;
        void Shutdown() override;

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
        void DrawGrid(int slices, float spacing, Color color) override;

        void DrawDebugText(const char* pText, int x, int y, int fontSize, Color color) override;
        void DrawText(const char* pText, int x, int y, int fontSize, Color color) override;
        void DrawTextEx(IFont font, const char* pText, Vec2 position, float fontSize, float spacing, Color tint) override;
        Vec2 MeasureTextEx(IFont font, const char* pText, float fontSize, float spacing) override;
        int MeasureText(const char* pText, int fontSize) override;

        void DrawTexture(const ITexture& texture, float x, float y, Color tint) override;
        void DrawTextureV(const ITexture& texture, Vec2 position, Color tint) override;
        void DrawTextureEx(const ITexture& texture, Vec2 position, float rotation, float scale, Color tint) override;
        void DrawTextureRec(const ITexture& texture, Rectangle source, Vec2 position, Color tint) override;
        void DrawTexturePro(ITexture texture, Rectangle source, Rectangle dest, Vec2 origin, float rotation, Color tint) override;
        void DrawTextureTiled(ITexture texture, float scale, Vec2 offset, Color tint) override;
        void DrawTextureNPatch(ITexture texture, NPatchInfo nPatchInfo, Rectangle dest, Vec2 origin, float rotation, Color tint) override;
        ITexture LoadTexture(const char* pFilePath) override;
        ITexture LoadTextureFromImage(const Image& image) override;
        ITexture LoadTextureCubemap(const unsigned char* pRgbaFaces, int faceSize) override;
        ITexture GetRenderTextureTexture(IRenderTexture target) override;
        void UnloadTexture(ITexture& texture) override;
        bool UpdateTexture(const ITexture& texture, const void* pPixels) override;
        bool UpdateTextureRegion(const ITexture& texture, Rectangle region, const void* pPixels) override;
        bool isTextureValid(ITexture& texture) override;
        IRenderTexture LoadRenderTexture(int width, int height) override;
        void UnloadRenderTexture(IRenderTexture target) override;
        bool isRenderTextureValid(IRenderTexture& target) override;
        Image ReadTextureImage(const ITexture& texture) override;
        Image ReadScreenImage() override;
        ITexture GenCheckerTexture(int width, int height, int cellSize, Color colorA, Color colorB) override;

        void BeginTextureMode(IRenderTexture target) override;
        void EndTextureMode() override;
        void SetTextureFilterMode(TextureFilter mode) override;
        void SetTextureFilter(TextureFilter filter) override;
        void SetTextureWrap(int wrap) override;
        void BeginScissorMode(int x, int y, int width, int height) override;
        void EndScissorMode() override;
        void SetBlendMode(int mode) override;

        IFont LoadFont(const char* pFilePath, int fontSize,
                       const int* pCodepoints, int codepointCount) override;
        IFont LoadFontFromMemory(const char* pFileType, const unsigned char* pFileData, int dataSize,
                                 int fontSize, const int* pCodepoints, int codepointCount) override;
        void UnloadFont(IFont& font) override;
        void FillFont(IFont font, Font& out) override;

        void BeginShaderMode(const Shader& shader) override;
        void EndShaderMode() override;
        Shader LoadShader(const char* pVsFileName, const char* pFsFileName) override;
        Shader LoadShaderFromMemory(const char* pVsSource, const char* pFsSource) override;
        void UnloadShader(Shader& shader) override;
        bool isShaderValid(Shader& shader) override;
        int GetShaderLocation(const Shader& shader, const char* pUniformName) override;
        int GetShaderLocation(const Shader& shader, ShaderLocationIndex locIndex) override;
        int GetShaderAttributeLocation(const Shader& shader, const char* pAttribName) override;
        void SetShaderValue(const Shader& shader, int locIndex, float value) override;
        void SetShaderValue(const Shader& shader, int locIndex, int value) override;
        void SetShaderValue(const Shader& shader, int locIndex, const Color& value) override;
        void SetShaderValue(const Shader& shader, int locIndex, const Vec2& value) override;
        void SetShaderValue(const Shader& shader, int locIndex, const Vec3& value) override;
        void SetShaderValue(const Shader& shader, int locIndex, const Vec4& value) override;
        void SetShaderValueMatrix(const Shader& shader, int locIndex, const float* pMat) override;
        void SetShaderValueSampler(const Shader& shader, int locIndex, int textureUnit) override;
        void SetShaderValue(const Shader& shader, int locIndex, const void* pValue, int uniformType) override;
        void SetShaderValueV(const Shader& shader, int locIndex, const void* pValue, int uniformType, int count) override;
        void SetShaderValueMatrix(const Shader& shader, int locIndex, const Matrix& mat) override;
        void SetShaderValueTexture(const Shader& shader, int locIndex, const ITexture& texture) override;
        void SetShaderValueTextureUnit(const Shader& shader, int locIndex, const ITexture& texture, int textureUnit) override;

        void BeginMode2D(const Camera2D& camera) override;
        void EndMode2D() override;
        void BeginMode3D(const Camera3D& camera) override;
        void EndMode3D() override;
        void Set3DLightEnabled(int index, bool enabled) override;
        Camera2D GetCamera2D() const override
        {
            return m_Camera2D;
        }
        bool ShouldClose() const override
        {
            return m_ShouldClose;
        }
        void SetShouldClose(bool v)
        {
            m_ShouldClose = v;
        }

        int GetScreenWidth() const override
        {
            return m_Width;
        }
        int GetScreenHeight() const override
        {
            return m_Height;
        }
        void SetTargetFPS(int fps) override;
        bool SetVSync(bool enabled) override;
        float GetFrameTime() const override
        {
            return m_FrameTime;
        }

        void PushMatrix() override;
        void PopMatrix() override;
        void Translate(const Vec3& translation) override;
        void Rotate(float angle, const Vec3& axis) override;
        void Scale(const Vec3& scale) override;
        void MultMatrix(const Mat4& matrix) override;
        const float* GetMatrixModelview() override;
        const float* GetMatrixProjection() override;
        void EnableBackfaceCulling() override;
        void DisableBackfaceCulling() override;

        Model LoadModel(const char* pFilePath) override;
        void UnloadModel(Model& model) override;
        void DrawModel(const Model& model, const Vec3& position, float scale,
                       float rotationX, float rotationY, float rotationZ) override;
        void DrawModelEx(const Model& model, const Mat4& transform) override;
        void DrawModelEx(const Model& model, const Mat4& transform, Color tint) override;

        void UploadMesh(Mesh& mesh, bool dynamic) override;
        void UpdateMeshBuffer(Mesh& mesh, int index, const void* pData, int dataSize, int offset) override;
        void UnloadMesh(Mesh& mesh) override;
        void DrawMesh(const Mesh& mesh, const Material& material, const Mat4& transform) override;
        void DrawMeshInstanced(const Mesh& mesh, const Material& material, const Mat4* pTransforms, int instances) override;

        void RefreshViewport() override;

        RendererType GetType() const override
        {
            return RendererType::Vulkan;
        }

        VkInstance GetVulkanInstance() const
        {
            return m_Instance;
        }
        VkPhysicalDevice GetVulkanPhysicalDevice() const
        {
            return m_PhysicalDevice;
        }
        VkDevice GetVulkanDevice() const
        {
            return m_Device;
        }
        uint32_t GetVulkanGraphicsQueueFamily() const
        {
            return m_GraphicsQueueFamily;
        }
        VkQueue GetVulkanGraphicsQueue() const
        {
            return m_GraphicsQueue;
        }
        VkDescriptorPool GetVulkanDescriptorPool() const
        {
            return m_VkDescriptorSetManager.ImGuiDescriptorPool();
        }
        VkRenderPass GetVulkanMainRenderPass() const
        {
            return m_VkRenderPass.Get();
        }
        uint32_t GetVulkanMinImageCount() const
        {
            return m_VkSwapChain.GetMinImageCount();
        }
        uint32_t GetVulkanImageCount() const
        {
            return m_VkSwapChain.GetImageCount();
        }
        void SetMSAASamples(int samples);
        VkSampleCountFlagBits GetVulkanMSAASamples() const
        {
            return m_MsaaSamples;
        }
        VkDescriptorSet GetTextureDescriptorSet(uint32_t textureId) const;

        TextureFilter m_TextureFilterMode = TEXTURE_FILTER_BILINEAR;
        int m_TextureWrapMode = TEXTURE_WRAP_REPEAT;
        bool m_ScissorEnabled = false;
        VkRect2D m_ScissorRect{{0, 0}, {0, 0}};

    private:
        void CreateInstance();
        void CreateSurface();
        void PickPhysicalDevice();
        void CreateLogicalDevice();
        void CreateSwapChain();
        void CreateRenderPass();
        void Create3DUniformBuffer();
        void CreateShaderPipelines();
        void CreateFramebuffers();
        void CreateFrameVertexIndexBuffers();
        void CreateWhiteTexture();
        bool UploadStaticGpuMesh(const Mesh& mesh);
        void DestroyStaticGpuMesh(const Mesh& mesh);

        bool AllocateTextureDescriptorSet(VkDescriptorSet& outSet);
        bool Allocate3DDescriptorSet(VkDescriptorSet& outSet, VkDescriptorPool& outPool);
        uint64_t ComputeMaterialCacheKey(const Material& material) const;
        VkDescriptorSet CreateMaterialDescriptorSet(const Material& material);
        void ClearMaterialDescriptorCache();
        void ReplaceMaterialTextureReferences(uint32_t textureId);
        void Update3DDescriptorSet(VkShaderProgramData& program);
        void Write3DShaderUniform(uint32_t shaderId, int locIndex, float value);
        void Write3DShaderUniform(uint32_t shaderId, int locIndex, int value);
        void Write3DShaderUniform(uint32_t shaderId, int locIndex, const void* pData, size_t bytes);

        void RecreateSwapChain();
        void CleanupSwapChain();
        bool RecreateRenderTargetFramebuffers();

        VkQueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
        VkSwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device) const;
        bool IsDeviceSuitable(VkPhysicalDevice device) const;

        QuarkVkDevice m_VkDevice;
        QuarkVkSwapChain m_VkSwapChain;
        QuarkVkPipeline m_VkPipeline;
        QuarkVkRenderTarget m_VkRenderTarget;
        QuarkVkResources m_VkResources;
        QuarkVkCommandContext m_VkCommandContext;
        QuarkVkFrameManager m_VkFrameManager;
        QuarkVkFramebufferManager m_VkFramebufferManager;
        QuarkVkDescriptorSetManager m_VkDescriptorSetManager;
        QuarkVkRenderPass m_VkRenderPass;
        QuarkVkShaderCompiler m_VkShaderCompiler;
        QuarkRenderThreadPool m_ThreadPool;

        VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& vFormats) const;
        VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& vModes) const;
        VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps) const;
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;
        VkFormat FindSupportedFormat(const std::vector<VkFormat>& vCandidates,
                                     VkImageTiling tiling,
                                     VkFormatFeatureFlags features) const;
        VkFormat FindDepthFormat() const;

        bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags props,
                          VkBuffer& outBuffer, VkDeviceMemory& outMemory);
        bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VmaMemoryUsage memoryUsage,
                          VkBuffer& outBuffer, VmaAllocation& outAllocation,
                          VkDeviceMemory& outMemory,
                          VmaAllocationCreateFlags flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                                           VMA_ALLOCATION_CREATE_MAPPED_BIT);
        bool EnsureMappedBufferCapacity(VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped,
                                        VkDeviceSize& capacity, VkDeviceSize required,
                                        VkBufferUsageFlags usage);
        bool EnsureMappedBufferCapacity(VkBuffer& buffer, VkDeviceMemory& memory, VmaAllocation& allocation, void*& mapped,
                                        VkDeviceSize& capacity, VkDeviceSize required,
                                        VkBufferUsageFlags usage);

        VkSampleCountFlagBits GetSampleCountForSamples(int samples) const;

        IRenderTexture CreateRenderTargetInternal(int width, int height);
        void DestroyRenderTargetInternal(uint32_t renderTargetId);

        Shader FinishShaderProgram(uint32_t shaderId);
        bool ReadBinaryFile(const char* pPath, std::vector<char>& vOutData) const;

        void BuildCombinedFrameGeometry();
        bool UploadFrameGeometry(uint32_t frameIndex);

        void AppendQuadToBatch(std::vector<VkBatchVertex>& vVertices,
                               std::vector<uint32_t>& vIndices,
                               std::vector<VkDrawItem>& vDrawItems,
                               VkDescriptorSet ds,
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

        void PushQuad(float x, float y, float w, float h, Color color,
                      float u0 = 0.f, float v0 = 0.f,
                      float u1 = 1.f, float v1 = 1.f);
        Vec2 ApplyCameraTransform(Vec2 position) const;
        void EnsureBatchTexture(VkDescriptorSet ds);
        struct Vk3DGeometryBatch
        {
            std::vector<Vk3DVertex> m_vTriangleVertices;
            std::vector<uint32_t> m_vTriangleIndices;
            std::vector<Vk3DVertex> m_vLineVertices;
            std::vector<Vk3DDrawItem> m_vDrawItems;
            uint32_t m_ShaderProgramId = 0;
        };

        std::vector<Vk3DVertex>& GetActive3DTriangleVertices();
        const std::vector<Vk3DVertex>& GetActive3DTriangleVertices() const;
        std::vector<uint32_t>& GetActive3DTriangleIndices();
        std::vector<Vk3DVertex>& GetActive3DLineVertices();
        const std::vector<Vk3DVertex>& GetActive3DLineVertices() const;
        Vk3DVertex Transform3DVertex(Vec3 position, Color color) const;
        void AppendTriangle3D(std::vector<Vk3DVertex>& vVertices,
                              Vec3 a, Vec3 b, Vec3 c, Color color);
        void AppendLine3D(std::vector<Vk3DVertex>& vVertices,
                          Vec3 a, Vec3 b, Color color);
        void DrawSphereExInternal(Vec3 centerPos, float radius, int rings, int slices, Color color);

        struct GlyphData
        {
            int m_Value = 0;
            Rectangle m_Uv{};
            Rectangle m_Rec{};
            Image m_Image{};
            float m_AdvanceX = 0.f;
            float m_OffsetX = 0.f;
            float m_OffsetY = 0.f;
            int m_Width = 0;
            int m_Height = 0;
        };

        struct FontData
        {
            uint32_t m_AtlasTextureId = 0;
            int m_AtlasWidth = 0;
            int m_AtlasHeight = 0;
            int m_BaseSize = 0;
            int m_Ascent = 0;
            int m_Descent = 0;
            int m_LineHeight = 0;
            int m_LineGap = 0;
            int m_GlyphCount = 0;
            std::vector<GlyphData> m_vGlyphs;
        };

        bool LoadFontInternal(const char* pFilePath, const unsigned char* pFileData, int dataSize,
                              int pointSize, const int* pCodepoints, int codepointCount, FontData& out);
        static int FindGlyph(const FontData& fd, int codepoint);
        uint32_t EnsureDefaultFont();
        const FontData* GetFontData(IFont font) const;
        void DrawTextWithFontData(const FontData& fd, const char* pText,
                                  Vec2 position, float fontSize, float spacing, Color tint);
        Vec2 MeasureTextWithFontData(const FontData& fd, const char* pText,
                                     float fontSize, float spacing) const;

        SDL_Window* m_pWindow = nullptr;
        int m_Width = 0;
        int m_Height = 0;
        int m_TargetFps = 60;
        bool m_Vsync = true;
        bool m_VsyncExplicitlySet = false;
        float m_FrameTime = 0.f;
        uint64_t m_LastFrameCounter = 0;
        bool m_Drawing = false;
        bool m_ShouldClose = false;
        bool m_FramebufferResized = false;
        bool m_FrameGeometryPending = false;

        VkInstance m_Instance = VK_NULL_HANDLE;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
        VkQueue m_PresentQueue = VK_NULL_HANDLE;

        VkFormat m_SwapChainImageFormat = VK_FORMAT_UNDEFINED;
        VkFormat m_DepthFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D m_SwapChainExtent = {0, 0};

        std::vector<VkFramebuffer> m_vSwapChainFramebuffers;

        VkBuffer m_3DDummyBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_3DDummyMemory = VK_NULL_HANDLE;
        VmaAllocation m_3DDummyAllocation = VK_NULL_HANDLE;
        void* m_p3DDummyBaseMapped = nullptr;
        void* m_p3DDummyMapped = nullptr;
        VkBuffer m_3DInstanceBuffer = VK_NULL_HANDLE;
        VmaAllocation m_3DInstanceAllocation = VK_NULL_HANDLE;
        void* m_p3DInstanceMapped = nullptr;
        std::vector<VkInstanceTransformData> m_vFrameInstanceTransforms;
        QuarkVkGpuAllocator m_GpuAllocator;
        std::unordered_map<const float*, VkGpuMeshData> m_GpuMeshes;
        std::unordered_set<const float*> m_DynamicMeshes;

        std::array<VkFrameData, VK_MAX_FRAMES_IN_FLIGHT> m_aFrames{};
        uint32_t m_CurrentFrame = 0;
        uint32_t m_ImageIndex = 0;
        uint32_t m_GraphicsQueueFamily = UINT32_MAX;
        int m_RequestedMsaaSamples = 1;
        VkSampleCountFlagBits m_MsaaSamples = VK_SAMPLE_COUNT_1_BIT;

        uint32_t m_WhiteTextureId = 0;
        uint32_t m_BlackTextureId = 0;
        uint32_t m_FlatNormalTextureId = 0;
        VkDescriptorSet m_White3DDescriptorSet = VK_NULL_HANDLE;

        std::array<bool, 4> m_3DLightEnabled{true, true, true, true};
        std::array<VkLight3D, 4> m_aLights{{{{-3.0f, 4.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, 0.08f, 1, true},
                                           {{3.0f, 3.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 0.08f, 1, true},
                                           {{-3.0f, 3.0f, -3.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.08f, 1, true},
                                           {{3.0f, 3.0f, -3.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 0.08f, 1, true}}};
        std::unordered_map<uint64_t, VkMaterialCacheEntry> m_MaterialCache;

        std::unordered_map<std::string, VkCachedTexture> m_TextureCache;
        std::unordered_map<uint32_t, std::string> m_TextureCacheKeys;
        std::unordered_map<uint32_t, FontData> m_Fonts;
        uint32_t m_NextFontId = 1;
        uint32_t m_DefaultFontId = 0;

        uint32_t m_NextRenderTargetId = 1;
        std::unordered_map<uint32_t, VkRenderTargetData> m_RenderTargets;
        std::vector<uint32_t> m_vRenderTargetOrder;
        uint32_t m_ActiveRenderTargetId = 0;

        std::vector<VkBatchVertex> m_vBatchVertices;
        std::vector<uint32_t> m_vBatchIndices;
        std::vector<VkDrawItem> m_vBatchDrawItems;
        Vk3DGeometryBatch m_Main3DBatch;

        std::vector<VkBatchVertex> m_vFrameVertices;
        std::vector<uint32_t> m_vFrameIndices;
        std::vector<VkDrawItem> m_vFrameDrawItems;
        std::vector<VkFramePass> m_vFramePasses;
        std::vector<Vk3DVertex> m_vFrameTriangleVertices3D;
        std::vector<Vk3DVertex> m_vFrameLineVertices3D;
        std::vector<Vk3DDrawItem> m_vFrame3DDrawItems;
        uint32_t m_Frame3DShaderProgramId = 0;

        VkDescriptorSet m_CurrentDescriptorSet = VK_NULL_HANDLE;
        bool m_DrawingDebugOverlay = false;
        Color m_ClearColor = {0, 0, 0, 255};

        Mat4 m_CurrentMatrix = Mat4::identity();
        std::vector<Mat4> m_vMatrixStack;
        Mat4 m_ViewMatrix = Mat4::identity();
        Mat4 m_ProjectionMatrix = Mat4::identity();
        Vec3 m_ViewPos{2.0f, 4.0f, 6.0f};

        Camera2D m_Camera2D{};
        bool m_Camera2DActive = false;
    };
} // qci
#endif // __QUARK_VK_RENDERER__
