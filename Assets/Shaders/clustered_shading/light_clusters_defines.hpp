// light_clusters_defines.hpp

#ifdef __cplusplus
#pragma once

namespace Radiant
{

    using uint3 = glm::uvec3;
#endif

    namespace Shaders
    {

        // Clustered Shading. DOOM2016 subdivision scheme 3072 clusters(16x8x24), depth slices logarithmic (Tiago Sousa).
#define LIGHT_CLUSTERS_SUBDIVISION_X 16u
#define LIGHT_CLUSTERS_SUBDIVISION_Y 16u
#define LIGHT_CLUSTERS_SUBDIVISION_Z 64u
#define LIGHT_CLUSTERS_COUNT (LIGHT_CLUSTERS_SUBDIVISION_X * LIGHT_CLUSTERS_SUBDIVISION_Y * LIGHT_CLUSTERS_SUBDIVISION_Z)

        // NOTE: This results in a LIGHT_CLUSTERS_BUILD_WG_SIZE^3 workgroup size.
#define LIGHT_CLUSTERS_BUILD_WG_SIZE 4

#ifndef __cplusplus
        static constexpr float3 s_INV_LIGHT_CLUSTER_SUBDIVISIONS =
            1.0f / uint3(LIGHT_CLUSTERS_SUBDIVISION_X, LIGHT_CLUSTERS_SUBDIVISION_Y, LIGHT_CLUSTERS_SUBDIVISION_Z);
#endif

        // LIGHT_CLUSTERS_WORD_SIZE should match sizeof (PointLightBitmasks && ActiveClusters) type * 8(since size in bits).
#define LIGHT_CLUSTERS_WORD_SIZE 32  // Bits count.
        static constexpr float s_INV_LIGHT_CLUSTERS_WORD_SIZE = 1.0f / (float)LIGHT_CLUSTERS_WORD_SIZE;

#define LIGHT_CLUSTERS_POINT_LIGHT_BITMASK_ARRAY_SIZE ((MAX_POINT_LIGHT_COUNT + LIGHT_CLUSTERS_WORD_SIZE - 1) / LIGHT_CLUSTERS_WORD_SIZE)
#define LIGHT_CLUSTERS_SPOT_LIGHT_BITMASK_ARRAY_SIZE ((MAX_SPOT_LIGHT_COUNT + LIGHT_CLUSTERS_WORD_SIZE - 1) / LIGHT_CLUSTERS_WORD_SIZE)
        // Same as Michal Drobot does 2017 pptx, so Nth bit means Nth light is inside cluster
        struct LightClusterList
        {
            uint32_t PointLightBitmasks[LIGHT_CLUSTERS_POINT_LIGHT_BITMASK_ARRAY_SIZE];
          //  uint32_t SpotLightBitmasks[LIGHT_CLUSTERS_SPOT_LIGHT_BITMASK_ARRAY_SIZE];
        };

#ifndef __cplusplus
        // NOTE: Assuming zPosVS is negative, so we negate it in function to become positive.
        uint GetLightClusterDepthSlice(const float zPosVS, const float scale, const float bias)
        {
            return uint(max(log2(-zPosVS) * scale + bias, 0.0f));
        }

        uint GetLightClusterIndex(const float2 fragCoord, const float2 fullResolution, const uint slice)
        {
            // Locating which cluster this fragment is part of.
            const float2 froxelSize = fullResolution * Shaders::s_INV_LIGHT_CLUSTER_SUBDIVISIONS.xy;
            const uint3 froxelID    = uint3(uint2(fragCoord.xy / froxelSize), slice);
            return Shaders::flatten3D(froxelID,
                                      uint3(LIGHT_CLUSTERS_SUBDIVISION_X, LIGHT_CLUSTERS_SUBDIVISION_Y, LIGHT_CLUSTERS_SUBDIVISION_Z));
        }
#endif

#define LIGHT_CLUSTERS_ASSIGNMENT_WG_SIZE 32
#define LIGHT_CLUSTERS_LIGHTS_LOAD_PER_THREAD 6  // best case I profiled is (log2(WG_SIZE) + 1)
#define LIGHT_CLUSTERS_MAX_SHARED_LIGHTS (LIGHT_CLUSTERS_LIGHTS_LOAD_PER_THREAD * LIGHT_CLUSTERS_ASSIGNMENT_WG_SIZE)

#define LIGHT_CLUSTERS_SPLIT_DISPATCHES 1
        // NOTE: This count defines how many lights can be processed in a single workgroup, better to be a multiple of shared light count!
#define LIGHT_CLUSTERS_MAX_BATCH_LIGHT_COUNT 5120

#define LIGHT_CLUSTERS_DETECT_ACTIVE 0
#define LIGHT_CLUSTERS_DETECT_ACTIVE_WG_SIZE_X LIGHT_CLUSTERS_SUBDIVISION_X
#define LIGHT_CLUSTERS_DETECT_ACTIVE_WG_SIZE_Y LIGHT_CLUSTERS_SUBDIVISION_Y
#define LIGHT_CLUSTERS_ACTIVE_CLUSTERS_BITMASK_ARRAY_SIZE ((LIGHT_CLUSTERS_COUNT + LIGHT_CLUSTERS_WORD_SIZE - 1) / LIGHT_CLUSTERS_WORD_SIZE)
        struct LightClusterActiveList
        {
            uint32_t ActiveClusters[LIGHT_CLUSTERS_ACTIVE_CLUSTERS_BITMASK_ARRAY_SIZE];
        };

    }  // namespace Shaders

#ifdef __cplusplus
}
#endif
