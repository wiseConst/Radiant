#pragma once

#include <Render/Renderers/Renderer.hpp>

namespace Radiant
{

    class ForwardBlinnPhongRenderer final : public Renderer
    {
      public:
        ForwardBlinnPhongRenderer() noexcept : Renderer() {}

        bool BeginFrame() noexcept final override;
        void RenderFrame() noexcept final override;
        void EndFrame() noexcept final override;

      private:
    };

}  // namespace Radiant
