#ifndef __QUARK_VK_PIPELINE__
#define __QUARK_VK_PIPELINE__

#include "QuarkVkCommon.hpp"

namespace qci
{
    struct VkBatchVertex
    {
        float m_X, m_Y;
        float m_U, m_V;
        float m_R, m_G, m_B, m_A;
    };

    struct Vk3DVertex
    {
        float m_X, m_Y, m_Z, m_W;
        float m_U, m_V;
        float m_R, m_G, m_B, m_A;
        float m_Nx, m_Ny, m_Nz, m_Nw;
        float m_Wx, m_Wy, m_Wz, m_Ww;
        float m_Tx, m_Ty, m_Tz, m_Tw;
    };

    struct VkGpuMeshVertex
    {
        float m_X, m_Y, m_Z, m_W;
        float m_U, m_V;
        float m_R, m_G, m_B, m_A;
        float m_Nx, m_Ny, m_Nz, m_Nw;
        float m_Wx, m_Wy, m_Wz, m_Ww;
        float m_Tx, m_Ty, m_Tz, m_Tw;
    };
    static_assert(sizeof(VkGpuMeshVertex) == sizeof(Vk3DVertex),
                  "GPU-resident and frame 3D vertices must share the same pipeline layout.");

    struct Vk3DPushConstants
    {
        float m_aLightPositions[4][4];
        float m_aLightColors[4][4];
        float m_aTimeData[4];
        float m_aLightEnabled[4];
    };

    struct VkGpuMeshPushConstants
    {
        float m_aModelViewProjection[16];
        float m_aModel[16];
        float m_aMaterialColor[4];
        uint32_t m_ObjectSpaceTransform = 1;
        uint32_t m_UseInstanceBuffer = 0;
        uint32_t m_aPadding[2]{};
    };
    static_assert(sizeof(VkGpuMeshPushConstants) == sizeof(Vk3DPushConstants),
                  "GPU mesh push constants must fit the 3D pipeline layout range.");

    struct VkPushConstants2D
    {
        float m_ScreenWidth;
        float m_ScreenHeight;
    };

    class QuarkVkPipeline
    {
    public:
        void Initialize(VkDevice device,
                        VkDescriptorSetLayout textureSetLayout,
                        VkDescriptorSetLayout materialSetLayout);
        void CreatePipelines(VkRenderPass renderPass,
                             VkRenderPass offscreenRenderPass,
                             VkSampleCountFlagBits msaaSamples);
        void DestroyPipelines();
        void Shutdown();

        void SetBackfaceCulling(bool enabled);

        bool BackfaceCulling() const
        {
            return m_BackfaceCullingEnabled;
        }

        void SetBlendMode(int mode);

        int GetBlendMode() const
        {
            return m_BlendMode;
        }

        VkPipelineLayout GetLayout2D() const
        {
            return m_Layout2D;
        }

        VkPipelineLayout GetLayout3D() const
        {
            return m_Layout3D;
        }

        VkPipeline Get2D(int blendMode = BLEND_ALPHA) const
        {
            if ((blendMode == BLEND_ADDITIVE || blendMode == BLEND_ADD_COLORS) &&
                m_Pipeline2DAdditive != VK_NULL_HANDLE)
            {
                return m_Pipeline2DAdditive;
            }
            return m_Pipeline2D;
        }

        VkPipeline GetOffscreen2D(int blendMode = BLEND_ALPHA) const
        {
            if ((blendMode == BLEND_ADDITIVE || blendMode == BLEND_ADD_COLORS) &&
                m_OffscreenPipeline2DAdditive != VK_NULL_HANDLE)
            {
                return m_OffscreenPipeline2DAdditive;
            }
            return m_OffscreenPipeline2D;
        }

        VkPipeline Get3DTri() const
        {
            return Get3DTri(m_BackfaceCullingEnabled);
        }

        VkPipeline Get3DTri(bool culled) const
        {
            return culled ? m_Pipeline3DTriCulled : m_Pipeline3DTri;
        }

        VkPipeline Get3DLines() const
        {
            return m_Pipeline3DLines;
        }

        VkPipeline GetOffscreen3DTri() const
        {
            return GetOffscreen3DTri(m_BackfaceCullingEnabled);
        }

        VkPipeline GetOffscreen3DTri(bool culled) const
        {
            return culled ? m_OffscreenPipeline3DTriCulled : m_OffscreenPipeline3DTri;
        }

        VkPipeline GetOffscreen3DLines() const
        {
            return m_OffscreenPipeline3DLines;
        }

        VkPipeline Create2DPipeline(VkRenderPass renderPass,
                                    VkShaderModule vertexModule = VK_NULL_HANDLE,
                                    VkShaderModule fragmentModule = VK_NULL_HANDLE,
                                    int blendMode = BLEND_ALPHA);
        VkPipeline Create3DPipeline(VkRenderPass renderPass,
                                    VkPrimitiveTopology topology,
                                    VkShaderModule vertexModule = VK_NULL_HANDLE,
                                    VkShaderModule fragmentModule = VK_NULL_HANDLE,
                                    bool gpuMeshTransform = false,
                                    bool backfaceCulling = false);

    private:
        void EnsureLayouts();

        VkDevice m_Device = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_TextureSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_MaterialSetLayout = VK_NULL_HANDLE;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkRenderPass m_OffscreenRenderPass = VK_NULL_HANDLE;
        VkSampleCountFlagBits m_MsaaSamples = VK_SAMPLE_COUNT_1_BIT;
        bool m_BackfaceCullingEnabled = false;
        int m_BlendMode = BLEND_ALPHA;

        VkPipelineLayout m_Layout2D = VK_NULL_HANDLE;
        VkPipelineLayout m_Layout3D = VK_NULL_HANDLE;

        VkPipeline m_Pipeline2D = VK_NULL_HANDLE;
        VkPipeline m_Pipeline2DAdditive = VK_NULL_HANDLE;
        VkPipeline m_OffscreenPipeline2D = VK_NULL_HANDLE;
        VkPipeline m_OffscreenPipeline2DAdditive = VK_NULL_HANDLE;
        VkPipeline m_Pipeline3DTri = VK_NULL_HANDLE;
        VkPipeline m_Pipeline3DTriCulled = VK_NULL_HANDLE;
        VkPipeline m_Pipeline3DLines = VK_NULL_HANDLE;
        VkPipeline m_OffscreenPipeline3DTri = VK_NULL_HANDLE;
        VkPipeline m_OffscreenPipeline3DTriCulled = VK_NULL_HANDLE;
        VkPipeline m_OffscreenPipeline3DLines = VK_NULL_HANDLE;
    };
} // qci
#endif // __QUARK_VK_PIPELINE__
