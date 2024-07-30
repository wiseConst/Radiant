#pragma once

#include <Render/Renderers/Renderer.hpp>

namespace Radiant
{

    class ForwardRenderer final : public Renderer
    {
      public:
        ForwardRenderer() noexcept;
        ~ForwardRenderer() noexcept final override = default;

        bool BeginFrame() noexcept final override;
        void RenderFrame() noexcept final override;
        void EndFrame() noexcept final override;

      private:
        Unique<GfxPipeline> m_PBRPipeline{nullptr};
        Unique<GfxPipeline> m_DepthPrePassPipeline{nullptr};
        Unique<GfxPipeline> m_SSSPipeline{nullptr};
        Unique<GfxPipeline> m_SSAOPipeline{nullptr};
        RenderGraphStatistics m_RenderGraphStats = {};
    };

}  // namespace Radiant
