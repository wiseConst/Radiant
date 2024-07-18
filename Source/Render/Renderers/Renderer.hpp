#pragma once

#include <Render/CoreDefines.hpp>
#include <Render/GfxContext.hpp>

#include <Render/RenderGraph/RenderGraph.hpp>
#include <Render/GfxPipeline.hpp>
#include <Render/GfxTexture.hpp>
#include <Render/GfxBuffer.hpp>
#include <Render/GfxShader.hpp>
#include <Render/Camera.hpp>

#include <Scene/Scene.hpp>

namespace Radiant
{

    class Renderer : private Uncopyable, private Unmovable
    {
      public:
        explicit Renderer() noexcept;
        virtual ~Renderer() noexcept;

        virtual bool BeginFrame() noexcept  = 0;
        virtual void RenderFrame() noexcept = 0;
        virtual void EndFrame() noexcept    = 0;

        void UpdateMainCamera(const float deltaTime) noexcept;

      protected:
        Unique<GfxContext> m_GfxContext{nullptr};
        Unique<RenderGraph> m_RenderGraph{nullptr};
        Unique<Scene> m_Scene{nullptr};
        Shared<Camera> m_MainCamera{nullptr};

        vk::Extent2D m_ViewportExtent{};
    };

}  // namespace Radiant
