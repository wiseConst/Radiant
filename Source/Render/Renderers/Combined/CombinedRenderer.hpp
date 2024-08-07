#pragma once

#include <Render/Renderers/Renderer.hpp>

namespace Radiant
{

    class CombinedRenderer final : public Renderer
    {
      public:
        CombinedRenderer() noexcept;
        ~CombinedRenderer() noexcept final override = default;

        void RenderFrame() noexcept final override;

      private:
        Unique<GfxPipeline> m_LightClustersBuildPipeline{nullptr};
        Unique<GfxPipeline> m_LightClustersAssignmentPipeline{nullptr};
        Unique<GfxPipeline> m_DepthPrePassPipeline{nullptr};
        Unique<GfxPipeline> m_PBRPipeline{nullptr};
        Unique<GfxPipeline> m_SSSPipeline{nullptr};
        Unique<GfxPipeline> m_SSAOPipeline{nullptr};
        Unique<GfxPipeline> m_SSAOBoxBlurPipeline{nullptr};
        RenderGraphStatistics m_RenderGraphStats = {};
        glm::vec3 m_SunDirection{0.0f, 0.8f, 0.5f};
    };

}  // namespace Radiant
