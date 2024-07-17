#pragma once

#include <Render/Renderers/Renderer.hpp>

namespace Radiant
{

    class ForwardBlinnPhongRenderer final : public Renderer
    {
      public:
        ForwardBlinnPhongRenderer() noexcept : Renderer() { Init(); }
        ~ForwardBlinnPhongRenderer() noexcept final override { Shutdown(); }

        bool BeginFrame() noexcept final override;
        void RenderFrame() noexcept final override;
        void EndFrame() noexcept final override;

      private:
        Unique<GfxPipeline> m_TriPipeline{nullptr};
        vk::Extent2D m_ViewportExtent{};

        void Init() noexcept;
        void Shutdown() noexcept;
    };

}  // namespace Radiant
