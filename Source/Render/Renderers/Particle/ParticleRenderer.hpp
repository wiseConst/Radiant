#pragma once

#include <Render/Renderers/Renderer.hpp>

namespace Radiant
{

    class ParticleRenderer final : public Renderer
    {
      public:
        ParticleRenderer() noexcept;
        ~ParticleRenderer() noexcept final override = default;

        void RenderFrame() noexcept final override;

      private:
    };

}  // namespace Radiant
