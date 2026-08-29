#ifndef __QUARK_VK_PIPELINE__
#define __QUARK_VK_PIPELINE__

#include "QuarkVkCommon.hpp"

namespace qc {

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

class QuarkVkPipeline {
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
    bool BackfaceCulling() const { return m_backfaceCullingEnabled; }

    VkPipelineLayout GetLayout2D() const { return m_layout2D; }
    VkPipelineLayout GetLayout3D() const { return m_layout3D; }

    VkPipeline Get2D() const { return m_pipeline2D; }
    VkPipeline GetOffscreen2D() const { return m_offscreenPipeline2D; }
    VkPipeline Get3DTri() const { return m_pipeline3DTri; }
    VkPipeline Get3DLines() const { return m_pipeline3DLines; }
    VkPipeline GetOffscreen3DTri() const { return m_offscreenPipeline3DTri; }
    VkPipeline GetOffscreen3DLines() const { return m_offscreenPipeline3DLines; }

    VkPipeline Create2DPipeline(VkRenderPass renderPass,
                                VkShaderModule vertexModule = VK_NULL_HANDLE,
                                VkShaderModule fragmentModule = VK_NULL_HANDLE);
    VkPipeline Create3DPipeline(VkRenderPass renderPass,
                                VkPrimitiveTopology topology,
                                VkShaderModule vertexModule = VK_NULL_HANDLE,
                                VkShaderModule fragmentModule = VK_NULL_HANDLE);

private:
    void EnsureLayouts();

    VkDevice             m_device              = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_textureSetLayout   = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_materialSetLayout  = VK_NULL_HANDLE;
    VkRenderPass          m_renderPass         = VK_NULL_HANDLE;
    VkRenderPass          m_offscreenRenderPass = VK_NULL_HANDLE;
    VkSampleCountFlagBits m_msaaSamples        = VK_SAMPLE_COUNT_1_BIT;
    bool                  m_backfaceCullingEnabled = false;

    VkPipelineLayout m_layout2D            = VK_NULL_HANDLE;
    VkPipelineLayout m_layout3D            = VK_NULL_HANDLE;

    VkPipeline m_pipeline2D                = VK_NULL_HANDLE;
    VkPipeline m_offscreenPipeline2D       = VK_NULL_HANDLE;
    VkPipeline m_pipeline3DTri             = VK_NULL_HANDLE;
    VkPipeline m_pipeline3DLines           = VK_NULL_HANDLE;
    VkPipeline m_offscreenPipeline3DTri    = VK_NULL_HANDLE;
    VkPipeline m_offscreenPipeline3DLines  = VK_NULL_HANDLE;
};

} // namespace qc

#endif // __QUARK_VK_PIPELINE__