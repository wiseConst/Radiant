#pragma once

#include <Core/Core.hpp>
#include <ShaderDefines.h>

namespace Radiant
{
    static constexpr bool s_bForceGfxValidation        = true;
    static constexpr bool s_bForceIGPU                 = true;
    static constexpr std::uint8_t s_BufferedFrameCount = 2;

    using ExtraBufferFlags = std::uint32_t;
    enum EExtraBufferFlag : ExtraBufferFlags
    {
        EXTRA_BUFFER_FLAG_ADDRESSABLE  = BIT(0),
        EXTRA_BUFFER_FLAG_DEVICE_LOCAL = BIT(1) | EXTRA_BUFFER_FLAG_ADDRESSABLE,  // Default memory - host, but this
                                                                                  // flag implies device memory
                                                                                  // and buffer device address(GPU virtual address)
        EXTRA_BUFFER_FLAG_MAPPED = BIT(2),
    };

    using ResourceStateFlags = std::uint8_t;
    enum EResourceState : ResourceStateFlags
    {
        RESOURCE_STATE_UNDEFINED,  // Init
        RESOURCE_STATE_GENERAL,    // Mostly used by compute shaders for storage images
        RESOURCE_STATE_VERTEX_BUFFER,
        RESOURCE_STATE_INDEX_BUFFER,
        RESOURCE_STATE_VERTEX_SHADER_RESOURCE,
        RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE,
        RESOURCE_STATE_COMPUTE_SHADER_RESOURCE,
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
    };

}  // namespace Radiant
