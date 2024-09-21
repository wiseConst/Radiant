#pragma once

#include <Render/Renderers/Renderer.hpp>

#include <radiance_cascades/radiance_cascades_defines.hpp>

namespace Radiant
{

    // NOTE: Radiance Cascades
    class SSGIRenderer final : public Renderer
    {
      public:
        SSGIRenderer() noexcept;
        ~SSGIRenderer() noexcept final override = default;

        void RenderFrame() noexcept final override;

      private:
        Unique<GfxPipeline> m_Point2DPipeline{nullptr};
        Unique<GfxPipeline> m_FullScreenClearPassPipeline{nullptr};

        std::vector<Point2D> m_Points;
        f32 m_PointRadius{5.0f};
    };

}  // namespace Radiant
