#pragma once

#include <Core/Core.hpp>
#include <ShaderDefines.h>

namespace Radiant
{

    static constexpr bool s_bForceGfxValidation        = true;
    static constexpr bool s_bForceIGPU                 = true;
    static constexpr std::uint8_t s_BufferedFrameCount = 2;

    struct RendererStatistics
    {
        std::atomic<std::uint64_t> DrawCallCount;
        std::atomic<std::uint64_t> ComputeDispatchCount;
        std::atomic<double> RenderGraphBuildTime;  // Milliseconds
        double GPUTime;                            // Milliseconds
    };

    enum class EResourceState : std::uint8_t
    {
        RESOURCE_STATE_UNDEFINED,        // Init
        RESOURCE_STATE_GENERAL,          // Mostly used by compute shaders for storage images
        RESOURCE_STATE_PRE_INITIALIZED,  // For images used by host only
        RESOURCE_STATE_VERTEX_BUFFER,
        RESOURCE_STATE_INDEX_BUFFER,
        RESOURCE_STATE_SHADER_RESOURCE,
        RESOURCE_STATE_STORAGE_BUFFER,
        RESOURCE_STATE_RENDER_TARGET,  // Color attachments
        RESOURCE_STATE_DEPTH_READ,
        RESOURCE_STATE_DEPTH_WRITE,
        RESOURCE_STATE_PRESENT,  // Used for present into swapchain
        RESOURCE_STATE_INDIRECT_ARGUMENT,
        RESOURCE_STATE_COPY_SOURCE,
        RESOURCE_STATE_COPY_DESTINATION,
        RESOURCE_STATE_RESOLVE_SOURCE,
        RESOURCE_STATE_RESOLVE_DESTINATION,
        RESOURCE_STATE_ACCELERATION_STRUCTURE,
        RESOURCE_STATE_ACCELERATION_STRUCTURE_BUILD_INPUT,
        RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
    };

}  // namespace Radiant
