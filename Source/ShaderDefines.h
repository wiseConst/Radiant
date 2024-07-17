#ifdef __cplusplus

#pragma once

#endif

namespace Shaders
{
    static const uint32_t s_MAX_BINDLESS_IMAGES   = 1 << 16;
    static const uint32_t s_MAX_BINDLESS_TEXTURES = 1 << 16;
    static const uint32_t s_MAX_BINDLESS_SAMPLERS = 1 << 11;

#ifndef __cplusplus

    //[[vk::binding(0, 0)]] RWTexture2D<float4> img4s[];
    //[[vk::binding(0, 1)]] RWTexture2D<float2> img2s[];
    //[[vk::binding(0, 2)]] RWByteAddressBuffer bufs;

#endif

}  // namespace Shaders
