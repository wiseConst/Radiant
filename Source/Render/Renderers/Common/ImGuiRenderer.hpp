#pragma once

#include <imgui.h>
#include <vulkan/vulkan.hpp>

namespace Radiant
{

    class GfxContext;
    class RenderGraph;
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

        void Init() noexcept;
    };

}  // namespace Radiant
