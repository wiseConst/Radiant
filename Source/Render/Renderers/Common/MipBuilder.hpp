#pragma once

#include <Render/GfxPipeline.hpp>

namespace Radiant
{

    class RenderGraph;
    class GfxContext;
    class MipBuilder final : private Uncopyable, private Unmovable
    {
      public:
        MipBuilder(const Unique<GfxContext>& gfxContext) noexcept : m_GfxContext(gfxContext) { Init(); }
        ~MipBuilder() noexcept;

        void BuildMips(Unique<RenderGraph>& renderGraph) noexcept;

      private:
        const Unique<GfxContext>& m_GfxContext;
        Unique<GfxPipeline> m_MipBuildPipeline{nullptr};

        constexpr MipBuilder() noexcept = delete;
        void Init() noexcept;
    };

}  // namespace Radiant
