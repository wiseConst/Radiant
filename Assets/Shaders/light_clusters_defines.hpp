// light_clusters_defines.hpp

#ifdef __cplusplus

namespace Radiant
{

    using uint3 = glm::uvec3;
#endif

    namespace Shaders
    {

        // Clustered Shading, DOOM2016 subdivision scheme 3072 clusters(16x8x24), depth slices logarithmic (Tiago Sousa).
        static constexpr uint3 s_LIGHT_CLUSTER_SUBDIVISIONS = uint3(16, 8, 24);
        static constexpr uint32_t s_LIGHT_CLUSTER_COUNT =
            s_LIGHT_CLUSTER_SUBDIVISIONS.x * s_LIGHT_CLUSTER_SUBDIVISIONS.y * s_LIGHT_CLUSTER_SUBDIVISIONS.z;

        struct LightClusterList
        {
            uint32_t PointLightCount;
            uint32_t PointLightIndices[MAX_POINT_LIGHT_COUNT];
        };

#define LIGHT_CLUSTERS_BUILD_WG_SIZE 4
#define LIGHT_CLUSTERS_ASSIGNMENT_WG_SIZE 32

    }  // namespace Shaders

#ifdef __cplusplus
}
#endif
