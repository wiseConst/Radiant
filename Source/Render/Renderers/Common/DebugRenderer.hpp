#pragma once

#include <Core/Core.hpp>
#include <Render/GfxPipeline.hpp>
#include <Render/RenderGraphDefines.hpp>

namespace Radiant
{

    class RenderGraph;
    class GfxContext;
    class DebugRenderer final : private Uncopyable, private Unmovable
    {
      public:
        DebugRenderer(const Unique<GfxContext>& gfxContext) noexcept : m_GfxContext(gfxContext) { Init(); }
        ~DebugRenderer() noexcept;

        // NOTE: It's read-modify-write call, so it'll return new aliased name.
        std::string DrawTextureView(const vk::Extent2D& viewportExtent, Unique<RenderGraph>& renderGraph,
                                    const std::vector<std::string>& textureNames, const std::string& backBufferSrcName) noexcept;

        FORCEINLINE void HotReload() noexcept { m_DebugTextureViewPipeline->HotReload(); }

      private:
        const Unique<GfxContext>& m_GfxContext;
        Unique<GfxPipeline> m_DebugTextureViewPipeline{nullptr};

        std::vector<RGResourceID> m_DebugTextureViewsPassData;

        constexpr DebugRenderer() noexcept = delete;
        void Init() noexcept;
    };

}  // namespace Radiant
