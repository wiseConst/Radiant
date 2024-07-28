#pragma once

#include <Render/CoreDefines.hpp>
#include <Render/GfxContext.hpp>
#include <Render/GfxPipeline.hpp>
#include <Render/RenderGraph.hpp>

namespace Radiant
{

    class MipBuilder final : private Uncopyable, private Unmovable
    {
      public:
        MipBuilder(const Unique<GfxContext>& gfxContext) noexcept : m_GfxContext(gfxContext) { Init(); }
        ~MipBuilder() noexcept { m_GfxContext->GetDevice()->WaitIdle(); }

        void BuildMips(Unique<RenderGraph>& renderGraph) noexcept;

      private:
        const Unique<GfxContext>& m_GfxContext;
        Unique<GfxPipeline> m_MipBuildPipeline{nullptr};

        constexpr MipBuilder() noexcept = delete;
        void Init() noexcept;
    };

}  // namespace Radiant
