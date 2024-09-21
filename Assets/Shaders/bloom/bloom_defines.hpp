// bloom_defines.hpp

#ifdef __cplusplus
#pragma once

namespace Radiant
{

    using float2 = glm::vec2;

#endif

    namespace Shaders
    {
#define BLOOM_WG_SIZE_X 16
#define BLOOM_WG_SIZE_Y 16

#ifndef __cplusplus
        static const uint2 g_BLOOM_WG_SIZE = uint2(BLOOM_WG_SIZE_X, BLOOM_WG_SIZE_Y);

        static const uint s_DOWNSAMPLE_TILE_BORDER    = 2;
        static const uint s_UPSAMPLE_BLUR_TILE_BORDER = 1;

        float RGBToLuminance(const float3 colorSRGB)
        {
            return dot(colorSRGB, float3(0.299f, 0.587f, 0.114f));
        }

        float KarisAverage(const float3 color)
        {
            // Formula is 1 / (1 + luma)
            const float luma = RGBToLuminance(Shaders::Linear2sRGB(float4(color, 0.0f)).rgb);
            return 1.0f / (1.0f + luma);
        }
#endif
    }  // namespace Shaders

#ifdef __cplusplus
}
#endif
