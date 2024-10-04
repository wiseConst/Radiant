#pragma once

#include <Render/RenderGraph.hpp>
#include <Render/GfxContext.hpp>
#include <Render/GfxPipeline.hpp>
#include <Render/GfxShader.hpp>
#include <Render/Camera.hpp>

#include <Render/Renderers/Common/ImGuiRenderer.hpp>
#include <Render/Renderers/Common/DebugRenderer.hpp>

#include <Core/CoreTypes.hpp>
#include <Scene/Scene.hpp>
#include <Scene/Mesh.hpp>

// NOTE: Used only for input mappings.
#include <GLFW/glfw3.h>

namespace Radiant
{

    class Renderer : private Uncopyable, private Unmovable
    {
      public:
        explicit Renderer() noexcept;
        virtual ~Renderer() noexcept;

        bool BeginFrame() noexcept;
        virtual void RenderFrame() noexcept = 0;
        void EndFrame() noexcept;

        void UpdateMainCamera(const f32 deltaTime) noexcept;

      protected:
        Unique<GfxContext> m_GfxContext{nullptr};
        Unique<RenderGraphResourcePool> m_RenderGraphResourcePool{nullptr};
        Unique<RenderGraph> m_RenderGraph{nullptr};
        Unique<Scene> m_Scene{nullptr};
        Shared<Camera> m_MainCamera{nullptr};
        Unique<ImGuiRenderer> m_UIRenderer{nullptr};
        Unique<DebugRenderer> m_DebugRenderer{nullptr};
        ImGuiUtils::ProfilersWindow m_ProfilerWindow = {};
        DrawContext m_DrawContext                    = {};

        vk::Extent2D m_ViewportExtent{};
    };

}  // namespace Radiant
