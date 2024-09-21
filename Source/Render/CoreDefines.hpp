#pragma once

#include <Core/Core.hpp>
#include <ShaderDefines.h>

namespace Radiant
{
    static constexpr u8 s_BufferedFrameCount = 2;
    static_assert(s_BufferedFrameCount > 0);

    static constexpr bool s_bUseResourceMemoryAliasing = true;
    static constexpr bool s_bForceGfxValidation        = true;
    static constexpr bool s_bRequireRayTracing         = false;
    static constexpr bool s_bRequireMeshShading        = false;
    static constexpr bool s_bShaderDebugPrintf         = false;  // it disables performance metrics for NSight!

    enum class ECommandBufferTypeBits : u8
    {
        COMMAND_BUFFER_TYPE_GENERAL_BIT            = BIT(0),
        COMMAND_BUFFER_TYPE_ASYNC_COMPUTE_BIT      = BIT(1),
        COMMAND_BUFFER_TYPE_DEDICATED_TRANSFER_BIT = BIT(2),
    };

    using ExtraBufferFlags = u32;
    enum EExtraBufferFlagBits : ExtraBufferFlags
    {
        EXTRA_BUFFER_FLAG_ADDRESSABLE_BIT = BIT(0),  // Should express buffer device address?
        EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT =
            BIT(1) | EXTRA_BUFFER_FLAG_ADDRESSABLE_BIT,  // NOTE: Implies both device memory and buffer device address(GPU virtual address)
        EXTRA_BUFFER_FLAG_HOST_BIT          = BIT(2),    // Implies host(CPU) memory
        EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT = BIT(3) | EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT |
                                              EXTRA_BUFFER_FLAG_HOST_BIT,  // NOTE: Implies memory that can be used by both CPU and GPU!
    };

    using ResourceCreateFlags = u8;
    enum EResourceCreateBits : ResourceCreateFlags
    {
        RESOURCE_CREATE_EXPOSE_MIPS_BIT   = BIT(0),  // Create MipChain of image views?
        RESOURCE_CREATE_GENERATE_MIPS_BIT = BIT(1),  // Used only for mesh textures, doesn't create MipChain of image views!
        RESOURCE_CREATE_RENDER_GRAPH_MEMORY_CONTROLLED_BIT = BIT(2),  // Means resource can be only created but no be bind to any memory.
        RESOURCE_CREATE_FORCE_NO_RESOURCE_MEMORY_ALIASING_BIT =
            BIT(3),  // Create resource & bind to memory, this flag is needed cuz RESOURCE_CREATE_RENDER_GRAPH_MEMORY_CONTROLLED_BIT being
                     // set by render graph, so we can disable memory aliasing
    };

    using ResourceStateFlags = u32;
    enum EResourceStateBits : ResourceStateFlags
    {
        RESOURCE_STATE_UNDEFINED                              = 0,
        RESOURCE_STATE_VERTEX_BUFFER_BIT                      = BIT(0),
        RESOURCE_STATE_INDEX_BUFFER_BIT                       = BIT(1),
        RESOURCE_STATE_UNIFORM_BUFFER_BIT                     = BIT(2),
        RESOURCE_STATE_VERTEX_SHADER_RESOURCE_BIT             = BIT(3),
        RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT           = BIT(4),
        RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT            = BIT(5),
        RESOURCE_STATE_STORAGE_BUFFER_BIT                     = BIT(6),
        RESOURCE_STATE_RENDER_TARGET_BIT                      = BIT(7),
        RESOURCE_STATE_DEPTH_READ_BIT                         = BIT(8),
        RESOURCE_STATE_DEPTH_WRITE_BIT                        = BIT(9),
        RESOURCE_STATE_INDIRECT_ARGUMENT_BIT                  = BIT(10),
        RESOURCE_STATE_COPY_SOURCE_BIT                        = BIT(11),
        RESOURCE_STATE_COPY_DESTINATION_BIT                   = BIT(12),
        RESOURCE_STATE_RESOLVE_SOURCE_BIT                     = BIT(13),
        RESOURCE_STATE_RESOLVE_DESTINATION_BIT                = BIT(14),
        RESOURCE_STATE_ACCELERATION_STRUCTURE_BIT             = BIT(15),
        RESOURCE_STATE_ACCELERATION_STRUCTURE_BUILD_INPUT_BIT = BIT(16),
        RESOURCE_STATE_READ_BIT                               = BIT(17),
        RESOURCE_STATE_WRITE_BIT                              = BIT(18),
    };

    enum class EAlphaMode : u8
    {
        ALPHA_MODE_OPAQUE,  // The alpha value is ignored, and the rendered output is fully opaque.
        ALPHA_MODE_MASK,   // The rendered output is either fully opaque or fully transparent depending on the alpha value and the specified
                           // alphaCutoff value;
        ALPHA_MODE_BLEND,  // The alpha value is used to composite the source and destination areas. The rendered output is combined with
                           // the background using the normal painting operation (i.e. the Porter and Duff over operator).
    };

}  // namespace Radiant
