#pragma once

#include <Render/Renderers/Renderer.hpp>
#include "AW2World.hpp"

namespace Radiant
{

    struct AlanWake2Renderer final : public Renderer
    {
      public:
        AlanWake2Renderer() noexcept;
        ~AlanWake2Renderer() noexcept final override = default;

        void RenderFrame() noexcept final override;

      private:
        Unique<GfxPipeline> m_MSTriPipeline{nullptr};

        Unique<GfxPipeline> m_ShadeMainPipeline{nullptr};

        AW2World m_World{};
    };

}  // namespace Radiant
