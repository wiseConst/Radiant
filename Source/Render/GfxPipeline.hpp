#pragma once

#include <Core/Core.hpp>
#include <Systems/RenderSystem.hpp>
#include <variant>

#define VK_NO_PROTOTYPES
#include "vma/vk_mem_alloc.h"

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
        std::uint32_t MaxRecursionDepth{1};
    };

    struct GfxPipelineDescription
    {
        std::string DebugName{s_DEFAULT_STRING};
        std::variant<std::monostate, GfxGraphicsPipelineOptions, GfxComputePipelineOptions, GfxRayTracingPipelineOptions> PipelineOptions{
            std::monostate{}};
    };

    class GfxPipeline final
    {
      public:
        GfxPipeline(const GfxPipelineDescription& description) noexcept : m_Description(description) {}

      private:
        GfxPipelineDescription m_Description{};
        vk::UniquePipeline m_Handle{};
    };

}  // namespace Radiant
