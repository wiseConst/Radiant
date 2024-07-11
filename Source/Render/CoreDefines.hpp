#pragma once

#include <Core/Core.hpp>

namespace Radiant
{

    struct RendererCapabilities
    {
    };

    struct RendererStatistics
    {
        std::atomic<std::uint64_t> DrawCallCount;
        std::atomic<std::uint64_t> ComputeDispatchCount;
    };

}  // namespace Radiant
