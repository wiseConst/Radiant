#pragma once

#include <Core/Core.hpp>

namespace Radiant
{

    static constexpr bool s_bForceGfxValidation = true;

    struct RendererStatistics
    {
        std::atomic<std::uint64_t> DrawCallCount;
        std::atomic<std::uint64_t> ComputeDispatchCount;
        std::atomic<double> RenderGraphBuildTime;  // Milliseconds
    };

}  // namespace Radiant
