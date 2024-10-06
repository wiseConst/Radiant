// csm_defines.hpp

#ifdef __cplusplus
#pragma once

namespace Radiant
{

    using float4x4 = glm::mat4;
    using uint2    = glm::uvec2;
    using uint     = u32;

#endif

    namespace Shaders
    {

#define SHADOW_MAP_CASCADE_SIZE 1024
#define SHADOW_MAP_CASCADE_COUNT 4

#define SHADOW_MAP_ATLAS_SIDE_SIZE ((SHADOW_MAP_CASCADE_COUNT + 2 - 1) / 2)
#define SHADOW_MAP_ATLAS_SIZE (SHADOW_MAP_CASCADE_SIZE * SHADOW_MAP_ATLAS_SIDE_SIZE)
        struct CascadedShadowMapsData
        {
            float4x4 ViewProjectionMatrix[SHADOW_MAP_CASCADE_COUNT];
            float CascadeSplits[SHADOW_MAP_CASCADE_COUNT];  // From zNear up to latest cascade split, zFar isn't taken into account here.
        };

        struct DepthBounds
        {
            uint2 MinMaxZ;  // encoded in hlsl as asuint(), decode as asfloat()
            // float2 MinMaxZ;
        };

        // Assuming upper left corner as (0, 0), right bottom as (SHADOW_MAP_ATLAS_SIZE, SHADOW_MAP_ATLAS_SIZE).
        uint2 CalculateCSMTextureAtlasOffsets(const uint cascadeIndex)
        {
            return uint2(SHADOW_MAP_CASCADE_SIZE) *
                   uint2(cascadeIndex % SHADOW_MAP_ATLAS_SIDE_SIZE, cascadeIndex / SHADOW_MAP_ATLAS_SIDE_SIZE);
        }

#ifndef __cplusplus
        void AdjustTextureCoordinatesToShadowTextureAtlas(inout float2 projCoords, const uint cascadeIndex)
        {
            projCoords.xy = (projCoords.xy * float2(SHADOW_MAP_CASCADE_SIZE)) / float2(SHADOW_MAP_ATLAS_SIZE);
            projCoords.x += ((cascadeIndex % SHADOW_MAP_CASCADE_COUNT) * SHADOW_MAP_CASCADE_SIZE) / float(SHADOW_MAP_ATLAS_SIZE);
            projCoords.y += ((cascadeIndex / SHADOW_MAP_CASCADE_COUNT) * SHADOW_MAP_CASCADE_SIZE) / float(SHADOW_MAP_ATLAS_SIZE);
        }

#endif  // !__cplusplus

#define DEPTH_REDUCTION_WG_SIZE_X 16
#define DEPTH_REDUCTION_WG_SIZE_Y 16

    }  // namespace Shaders

#ifdef __cplusplus
}
#endif
