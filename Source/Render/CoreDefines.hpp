#pragma once

#include <Core/Core.hpp>

namespace Radiant
{

    struct RendererStatistics
    {
        std::atomic<std::uint64_t> DrawCallCount;
        std::atomic<std::uint64_t> ComputeDispatchCount;
        std::atomic<double> RenderGraphBuildTime;  // Milliseconds
    };

}  // namespace Radiant
