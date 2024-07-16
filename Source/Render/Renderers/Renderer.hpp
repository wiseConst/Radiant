#pragma once

#include <Core/Core.hpp>
#include <Render/CoreDefines.hpp>

#include <Systems/RenderSystem.hpp>

namespace Radiant
{

    class Renderer : private Uncopyable, private Unmovable
    {
      public:
        explicit Renderer() noexcept : m_RenderSystem(MakeUnique<RenderSystem>()) {}
        virtual ~Renderer() noexcept { m_RenderSystem.reset(); }

        virtual bool BeginFrame() noexcept = 0;
        virtual void EndFrame() noexcept   = 0;

      protected:
        Unique<RenderSystem> m_RenderSystem{nullptr};
    };

}  // namespace Radiant
