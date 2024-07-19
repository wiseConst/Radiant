#pragma once

#include <Render/Renderers/Renderer.hpp>

namespace Radiant
{

    class ForwardBlinnPhongRenderer final : public Renderer
    {
      public:
        ForwardBlinnPhongRenderer() noexcept;
        ~ForwardBlinnPhongRenderer() noexcept final override = default;

        bool BeginFrame() noexcept final override;
        void RenderFrame() noexcept final override;
        void EndFrame() noexcept final override;

      private:
        Unique<GfxPipeline> m_BlinnPhongPipeline{nullptr};
        Unique<GfxTexture> m_DepthTexture{nullptr};
    };

}  // namespace Radiant
