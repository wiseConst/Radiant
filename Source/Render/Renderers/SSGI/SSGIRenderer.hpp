#pragma once

#include <Render/Renderers/Renderer.hpp>

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
        Unique<GfxPipeline> m_CirclePipeline{nullptr};
    };

}  // namespace Radiant
