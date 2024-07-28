#pragma once

#include <Render/CoreDefines.hpp>
#include <Render/GfxContext.hpp>
#include <Render/GfxPipeline.hpp>
#include <Render/RenderGraph.hpp>

#include <imgui.h>

namespace Radiant
{

    class ImGuiRenderer final : private Uncopyable, private Unmovable
    {
      public:
        ImGuiRenderer(const Unique<GfxContext>& gfxContext) noexcept : m_GfxContext(gfxContext) { Init(); }
        ~ImGuiRenderer() noexcept;

        void RenderFrame(const vk::Extent2D& viewportExtent, Unique<RenderGraph>& renderGraph, const std::string& backbufferName,
                         std::function<void()>&& uiFunc) noexcept;

      private:
        const Unique<GfxContext>& m_GfxContext;
        vk::UniqueDescriptorPool m_ImGuiPool{};

        struct ImGuiPassData
        {
            RGResourceID BackbufferTexture;
        } m_ImGuiPassData = {};

        void Init() noexcept;
    };

}  // namespace Radiant
