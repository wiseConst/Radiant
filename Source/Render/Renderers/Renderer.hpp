#pragma once

#include <Render/CoreDefines.hpp>
#include <Render/GfxContext.hpp>

#include <Render/RenderGraph/RenderGraph.hpp>
#include <Render/GfxPipeline.hpp>
#include <Render/GfxTexture.hpp>
#include <Render/GfxBuffer.hpp>
#include <Render/GfxShader.hpp>

#include <Scene/Scene.hpp>

namespace Radiant
{

    class Renderer : private Uncopyable, private Unmovable
    {
      public:
        explicit Renderer() noexcept : m_GfxContext(MakeUnique<GfxContext>()) {}
        virtual ~Renderer() noexcept = default;

        virtual bool BeginFrame() noexcept  = 0;
        virtual void RenderFrame() noexcept = 0;
        virtual void EndFrame() noexcept    = 0;

      protected:
        Unique<GfxContext> m_GfxContext{nullptr};
        Unique<RenderGraph> m_RenderGraph{nullptr};
        Unique<Scene> m_Scene{nullptr};
    };

}  // namespace Radiant
