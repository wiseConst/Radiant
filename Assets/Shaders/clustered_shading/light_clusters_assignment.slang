// light_clusters_assignment.slang

#include "../../../Source/ShaderDefines.h"
#include <light_clusters_defines.hpp>

struct PushConstantBlock
{
    const Shaders::CameraData *CameraData;
    const AABB *Clusters;
    Shaders::LightClusterList *LightClusterList;
    const Shaders::LightData *LightData;
#if LIGHT_CLUSTERS_SPLIT_DISPATCHES
    uint PointLightBatchOffset;
    uint PointLightBatchCount;
#endif
#if LIGHT_CLUSTERS_DETECT_ACTIVE
    const Shaders::LightClusterActiveList *ActiveLightClusters;
#endif
};
[vk::push_constant] PushConstantBlock u_PC;

bool TestSphereAABB(Sphere sphere, AABB aabb)
{
    const float3 closestPt = clamp(sphere.Origin, aabb.Min, aabb.Max);
    const float3 dist = closestPt - sphere.Origin;
    return dot(dist, dist) <= sphere.Radius * sphere.Radius;
}

#if !LIGHT_CLUSTERS_SPLIT_DISPATCHES
static const uint g_PointLightBatchCount = (MAX_POINT_LIGHT_COUNT + LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS - 1) / LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS;
#endif

// TODO: SpotLights, AreaLights culling

groupshared Sphere lds_PointLightData[LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS];

// NOTE: Although it depends on light count, but current WG_SIZE = 64 is the optimal I profiled.
[shader("compute")]
[numthreads(LIGHT_CLUSTERS_ASSIGNMENT_WG_SIZE, 1, 1)]
void computeMain(uint3 DTid: SV_DispatchThreadID, uint GroupIndex : SV_GroupIndex)
{
    const uint froxelIndex = DTid.x;
#if LIGHT_CLUSTERS_SPLIT_DISPATCHES
    // Only first dispatch clears bitmasks
    if (u_PC.PointLightBatchOffset == 0) {
        for (uint bitmaskIndex = 0; bitmaskIndex < LIGHT_CLUSTERS_POINT_LIGHT_BITMASK_ARRAY_SIZE; ++bitmaskIndex)
        {
            u_PC.LightClusterList[froxelIndex].PointLightBitmasks[bitmaskIndex] = 0;
        }
    }
#else
    // Clear bitmasks
    for (uint bitmaskIndex = 0; bitmaskIndex < LIGHT_CLUSTERS_POINT_LIGHT_BITMASK_ARRAY_SIZE; ++bitmaskIndex)
    {
        u_PC.LightClusterList[froxelIndex].PointLightBitmasks[bitmaskIndex] = 0;
    }
#endif

#if LIGHT_CLUSTERS_DETECT_ACTIVE
    const uint froxelBitmaskIndex = uint(froxelIndex * Shaders::s_INV_LIGHT_CLUSTERS_WORD_SIZE);
    const uint froxelBit = 1u << (froxelIndex % LIGHT_CLUSTERS_WORD_SIZE);
    const bool bIsClusterActive = (u_PC.ActiveLightClusters.ActiveClusters[froxelBitmaskIndex] & froxelBit) > 0;
#else
    const bool bIsClusterActive = true;
#endif

#if LIGHT_CLUSTERS_SPLIT_DISPATCHES
    uint globalLightOffset = u_PC.PointLightBatchOffset;
    const uint pointLightBatchCount = (u_PC.PointLightBatchCount + LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS - 1) / LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS;
    #else
    uint globalLightOffset = 0;
    #endif

#if LIGHT_CLUSTERS_SPLIT_DISPATCHES
    for (uint lightBatchIndex = 0; lightBatchIndex < pointLightBatchCount && globalLightOffset < u_PC.LightData->PointLightCount; ++lightBatchIndex)
#else
    for (uint lightBatchIndex = 0; lightBatchIndex < g_PointLightBatchCount && globalLightOffset < u_PC.LightData->PointLightCount; ++lightBatchIndex)
#endif
    {
        for (uint i = GroupIndex; i < LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS && 
                                    (globalLightOffset + i) < u_PC.LightData->PointLightCount; i += LIGHT_CLUSTERS_ASSIGNMENT_WG_SIZE) {
            Sphere plWS = u_PC.LightData->PointLights[globalLightOffset + i].sphere;
            plWS.Origin = mul(u_PC.CameraData->ViewMatrix, float4(plWS.Origin, 1.0f)).xyz;
            lds_PointLightData[i] = plWS;
        }
        GroupMemoryBarrierWithGroupSync();

#if LIGHT_CLUSTERS_SPLIT_DISPATCHES
        const uint writtenPointLightCount = min(LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS, u_PC.PointLightBatchCount - LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS * lightBatchIndex);
#else
        const uint writtenPointLightCount = min(LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS, u_PC.LightData->PointLightCount - LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS * lightBatchIndex);
#endif
        for (uint lightIndex = 0; lightIndex < writtenPointLightCount && bIsClusterActive; ++lightIndex)
        {
            Sphere plVS = lds_PointLightData[lightIndex];
            if (!TestSphereAABB(plVS, u_PC.Clusters[froxelIndex])) continue;

            const uint finalLightIndex = globalLightOffset + lightIndex;
            const uint bitmaskIndex = uint(finalLightIndex * Shaders::s_INV_LIGHT_CLUSTERS_WORD_SIZE);
            const uint lightBit = 1u << (finalLightIndex % LIGHT_CLUSTERS_WORD_SIZE);
            u_PC.LightClusterList[froxelIndex].PointLightBitmasks[bitmaskIndex] |= lightBit;
        }

        globalLightOffset += writtenPointLightCount;
        GroupMemoryBarrierWithGroupSync();
    }
}