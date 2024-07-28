#pragma once

#include <Render/CoreDefines.hpp>
#include <Render/GfxContext.hpp>

#include <Render/RenderGraph.hpp>
#include <Render/GfxPipeline.hpp>
#include <Render/GfxTexture.hpp>
#include <Render/GfxBuffer.hpp>
#include <Render/GfxShader.hpp>
#include <Render/Camera.hpp>

#include <Render/Renderers/Common/ImGuiRenderer.hpp>

#include <Scene/Scene.hpp>

// NOTE: Used only for input mappings.
#include <GLFW/glfw3.h>

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
        Unique<RenderGraphResourcePool> m_RenderGraphResourcePool{nullptr};
        Unique<RenderGraph> m_RenderGraph{nullptr};
        Unique<Scene> m_Scene{nullptr};
        Shared<Camera> m_MainCamera{nullptr};
        Unique<ImGuiRenderer> m_UIRenderer{nullptr};
        DrawContext m_DrawContext = {};

        vk::Extent2D m_ViewportExtent{};
    };

}  // namespace Radiant
