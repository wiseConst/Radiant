// csm_defines.hpp

#ifdef __cplusplus
#pragma once

namespace Radiant
{

    using float4x4 = glm::mat4;
    using uint2    = glm::uvec2;

#endif

    namespace Shaders
    {

#define SHADOW_MAP_CASCADE_SIZE 2048
#define SHADOW_MAP_CASCADE_COUNT 4
        struct CascadedShadowMapsData
        {
            float4x4 ViewProjectionMatrix[SHADOW_MAP_CASCADE_COUNT];
            float CascadeSplits[SHADOW_MAP_CASCADE_COUNT];
        };

        struct DepthBounds
        {
            uint2 MinMaxZ;  // encoded in hlsl as asuint(), decode as asfloat()
            // float2 MinMaxZ;
        };

#define DEPTH_REDUCTION_WG_SIZE_X 16
#define DEPTH_REDUCTION_WG_SIZE_Y 16

    }  // namespace Shaders

#ifdef __cplusplus
}
#endif
