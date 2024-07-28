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
        Unique<GfxBuffer> m_CameraSSBO{nullptr};
    };

}  // namespace Radiant
