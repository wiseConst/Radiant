#pragma once

#include <Render/Renderers/Renderer.hpp>

namespace Radiant
{

    static constexpr glm::vec3 s_MinPointLightPos{-15, -4, -5};
    static constexpr glm::vec3 s_MaxPointLightPos{15, 14, 5};

    class CombinedRenderer final : public Renderer
    {
      public:
        CombinedRenderer() noexcept;
        ~CombinedRenderer() noexcept final override = default;

        void RenderFrame() noexcept final override;

      private:
        // clustered light culling
        Unique<GfxPipeline> m_LightClustersBuildPipeline{nullptr};
        Unique<GfxPipeline> m_LightClustersDetectActivePipeline{nullptr};
        Unique<GfxPipeline> m_LightClustersAssignmentPipeline{nullptr};

        Unique<GfxPipeline> m_DepthPrePassPipeline{nullptr};
        Unique<GfxPipeline> m_PBRPipeline{nullptr};
        Unique<GfxPipeline> m_FinalPassPipeline{nullptr};

        // Screen-space shadows
        Unique<GfxPipeline> m_SSSPipeline{nullptr};

        // Screen-space ambient occlusion
        Unique<GfxPipeline> m_SSAOPipelineGraphics{nullptr};
        Unique<GfxPipeline> m_SSAOPipelineCompute{nullptr};  // better normal reconstruction
        Unique<GfxPipeline> m_SSAOBoxBlurPipeline{nullptr};

        // Bloom from Call of Duty Advanced Warfare, ACM Siggraph '14,
        Unique<GfxPipeline> m_BloomDownsamplePipelineGraphics{nullptr};
        Unique<GfxPipeline> m_BloomUpsampleBlurPipelineGraphics{nullptr};
        // compute version is optimized
        Unique<GfxPipeline> m_BloomDownsamplePipelineCompute{nullptr};
        Unique<GfxPipeline> m_BloomUpsampleBlurPipelineCompute{nullptr};

        Unique<GfxPipeline> m_EnvMapSkyboxPipeline{nullptr};
        Unique<GfxPipeline> m_DebugTextureViewPipeline{nullptr};

        RenderGraphStatistics m_RenderGraphStats = {};
        Unique<GfxTexture> m_EnvMapTexture{nullptr};

        Shaders::LightData m_LightData = {};
    };

}  // namespace Radiant
