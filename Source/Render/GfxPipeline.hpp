#pragma once

#include <Core/Core.hpp>
#include <variant>

namespace Radiant
{

    struct GfxGraphicsPipelineOptions
    {
    };

    struct GfxComputePipelineOptions
    {
    };

    struct GfxRayTracingPipelineOptions
    {
    };

    struct GfxPipelineDescription
    {
        std::string DebugName{s_DEFAULT_STRING};
        std::variant<std::monostate, GfxGraphicsPipelineOptions, GfxComputePipelineOptions, GfxRayTracingPipelineOptions> PipelineOptions{
            std::monostate{}};
    };

    class GfxPipeline
    {
      public:
      protected:
        GfxPipelineDescription m_Description{};

        GfxPipeline(const GfxPipelineDescription& description) noexcept : m_Description(description) {}

      private:
        constexpr GfxPipeline() noexcept = delete;
    };

}  // namespace Radiant
