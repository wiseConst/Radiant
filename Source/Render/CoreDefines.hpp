#pragma once

#include <Core/Core.hpp>
#include <ShaderDefines.h>

namespace Radiant
{
    static constexpr bool s_bForceGfxValidation        = true;
    static constexpr bool s_bForceIGPU                 = true;
    static constexpr std::uint8_t s_BufferedFrameCount = 2;

    enum class ECommandBufferType : std::uint8_t
    {
        COMMAND_BUFFER_TYPE_GENERAL            = BIT(0),
        COMMAND_BUFFER_TYPE_ASYNC_COMPUTE      = BIT(1),
        COMMAND_BUFFER_TYPE_DEDICATED_TRANSFER = BIT(2),
    };

    using ExtraBufferFlags = std::uint32_t;
    enum EExtraBufferFlag : ExtraBufferFlags
    {
        EXTRA_BUFFER_FLAG_ADDRESSABLE = BIT(0),
        EXTRA_BUFFER_FLAG_DEVICE_LOCAL =
            BIT(1) | EXTRA_BUFFER_FLAG_ADDRESSABLE,  // NOTE: Implies both device memory and buffer device address(GPU virtual address)
        EXTRA_BUFFER_FLAG_MAPPED = BIT(2),           // Implies host memory
    };

    using ResourceStateFlags = std::uint32_t;
    enum EResourceState : ResourceStateFlags
    {
        RESOURCE_STATE_UNDEFINED                          = 0,  // Init state
        RESOURCE_STATE_VERTEX_BUFFER                      = BIT(0),
        RESOURCE_STATE_INDEX_BUFFER                       = BIT(1),
        RESOURCE_STATE_UNIFORM_BUFFER                     = BIT(2),
        RESOURCE_STATE_VERTEX_SHADER_RESOURCE             = BIT(3),
        RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE           = BIT(4),
        RESOURCE_STATE_COMPUTE_SHADER_RESOURCE            = BIT(5),
        RESOURCE_STATE_STORAGE_BUFFER                     = BIT(6),
        RESOURCE_STATE_RENDER_TARGET                      = BIT(7),
        RESOURCE_STATE_DEPTH_READ                         = BIT(8),
        RESOURCE_STATE_DEPTH_WRITE                        = BIT(9),
        RESOURCE_STATE_INDIRECT_ARGUMENT                  = BIT(11),
        RESOURCE_STATE_COPY_SOURCE                        = BIT(12),
        RESOURCE_STATE_COPY_DESTINATION                   = BIT(13),
        RESOURCE_STATE_RESOLVE_SOURCE                     = BIT(14),
        RESOURCE_STATE_RESOLVE_DESTINATION                = BIT(15),
        RESOURCE_STATE_ACCELERATION_STRUCTURE             = BIT(16),
        RESOURCE_STATE_ACCELERATION_STRUCTURE_BUILD_INPUT = BIT(17),
        RESOURCE_STATE_READ                               = BIT(18),
        RESOURCE_STATE_WRITE                              = BIT(19),
    };

    enum class EAlphaMode : std::uint8_t
    {
        ALPHA_MODE_OPAQUE,  // The alpha value is ignored, and the rendered output is fully opaque.
        ALPHA_MODE_MASK,   // The rendered output is either fully opaque or fully transparent depending on the alpha value and the specified
                           // alphaCutoff value;
        ALPHA_MODE_BLEND,  // The alpha value is used to composite the source and destination areas. The rendered output is combined with
                           // the background using the normal painting operation (i.e. the Porter and Duff over operator).
    };

}  // namespace Radiant
