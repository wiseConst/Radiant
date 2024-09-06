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
        static constexpr uint3 s_LIGHT_CLUSTER_SUBDIVISIONS = uint3(16, 8, 24);  // uint3(24, 12, 32);
        static constexpr uint32_t s_LIGHT_CLUSTER_COUNT =
            s_LIGHT_CLUSTER_SUBDIVISIONS.x * s_LIGHT_CLUSTER_SUBDIVISIONS.y * s_LIGHT_CLUSTER_SUBDIVISIONS.z;

#ifndef __cplusplus
        static constexpr float3 s_INV_LIGHT_CLUSTER_SUBDIVISIONS = 1.0f / s_LIGHT_CLUSTER_SUBDIVISIONS;
#endif

        // LIGHT_CLUSTERS_WORD_SIZE should match sizeof ActivePointLightBits type
#define LIGHT_CLUSTERS_WORD_SIZE 32
#define LIGHT_CLUSTERS_POINT_LIGHT_BITMASK_ARRAY_SIZE ((MAX_POINT_LIGHT_COUNT + LIGHT_CLUSTERS_WORD_SIZE - 1) / LIGHT_CLUSTERS_WORD_SIZE)

#ifndef __cplusplus
        static constexpr float s_INV_LIGHT_CLUSTERS_WORD_SIZE = 1.0f / (float)LIGHT_CLUSTERS_WORD_SIZE;
#endif
        // Same as Michal Drobot does 2017 pptx, so Nth bit means Nth light is inside cluster
        struct LightClusterList
        {
            uint32_t PointLightBitmasks[LIGHT_CLUSTERS_POINT_LIGHT_BITMASK_ARRAY_SIZE];
        };

#define LIGHT_CLUSTERS_BUILD_WG_SIZE 4
#define LIGHT_CLUSTERS_ASSIGNMENT_WG_SIZE 64
#define LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS 2048

    }  // namespace Shaders

#ifdef __cplusplus
}
#endif
