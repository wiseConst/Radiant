#pragma once

#include <Render/Renderers/Renderer.hpp>

namespace Radiant
{

    namespace AW2
    {

        struct AlanWake2Renderer final : public Renderer
        {
          public:
            AlanWake2Renderer() noexcept;
            ~AlanWake2Renderer() noexcept final override = default;

            void RenderFrame() noexcept final override;

          private:
            Unique<GfxPipeline> m_MSTriPipeline{nullptr};
            Unique<GfxPipeline> m_HZBPipeline{nullptr};
        };

    }  // namespace AW2

}  // namespace Radiant
