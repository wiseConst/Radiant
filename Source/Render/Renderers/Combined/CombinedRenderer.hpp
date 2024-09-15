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
        Unique<GfxPipeline> m_LightClustersBuildPipeline{nullptr};
        Unique<GfxPipeline> m_LightClustersDetectActivePipeline{nullptr};
        Unique<GfxPipeline> m_LightClustersAssignmentPipeline{nullptr};

        Unique<GfxPipeline> m_DepthPrePassPipeline{nullptr};
        Unique<GfxPipeline> m_PBRPipeline{nullptr};
        Unique<GfxPipeline> m_FinalPassPipeline{nullptr};

        Unique<GfxPipeline> m_SSSPipeline{nullptr};

        Unique<GfxPipeline> m_SSAOPipeline{nullptr};
        Unique<GfxPipeline> m_SSAOBoxBlurPipeline{nullptr};

        Unique<GfxPipeline> m_BloomDownsamplePipeline{nullptr};
        Unique<GfxPipeline> m_BloomUpsampleBlurPipeline{nullptr};

        Unique<GfxPipeline> m_BloomDownsamplePipelineOptimized{nullptr};
        Unique<GfxPipeline> m_BloomUpsampleBlurPipelineOptimized{nullptr};

        RenderGraphStatistics m_RenderGraphStats = {};

        Shaders::LightData m_LightData = {};
    };

}  // namespace Radiant
