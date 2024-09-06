// light_clusters_assignment.slang

#include "../../../Source/ShaderDefines.h"
#include <light_clusters_defines.hpp>

struct PushConstantBlock
{
    const Shaders::CameraData *CameraData;
    const AABB *Clusters;
    Shaders::LightClusterList *LightClusterList;
    const Shaders::LightData *LightData;
};
[vk::push_constant] PushConstantBlock u_PC;

bool TestSphereAABB(Sphere sphere, AABB aabb)
{
    const float3 closestPt = clamp(sphere.Origin, aabb.Min, aabb.Max);
    const float3 dist = closestPt - sphere.Origin;
    return dot(dist, dist) <= sphere.Radius * sphere.Radius;
}

static const uint g_PointLightBatchCount = (MAX_POINT_LIGHT_COUNT + LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS - 1) / LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS;
// TODO: SpotLights, AreaLights culling

groupshared Sphere lds_PointLightData[LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS];

// NOTE: Although it depends on light count, but current WG_SIZE = 64 is the optimal I profiled.
[shader("compute")]
[numthreads(LIGHT_CLUSTERS_ASSIGNMENT_WG_SIZE, 1, 1)]
void computeMain(uint3 DTid: SV_DispatchThreadID, uint GroupIndex : SV_GroupIndex)
{
    const uint froxelIndex = DTid.x;
    // Clear bitmasks
    for (uint bitmaskIndex = 0; bitmaskIndex < LIGHT_CLUSTERS_POINT_LIGHT_BITMASK_ARRAY_SIZE; ++bitmaskIndex)
    {
        u_PC.LightClusterList[froxelIndex].PointLightBitmasks[bitmaskIndex] = 0;
    }

    uint globalLightOffset = 0;
    for (uint lightBatchIndex = 0; lightBatchIndex < g_PointLightBatchCount && globalLightOffset < u_PC.LightData->PointLightCount; ++lightBatchIndex)
    {
        for (uint i = GroupIndex; i < LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS && 
                                    (globalLightOffset + i) < u_PC.LightData->PointLightCount; i += LIGHT_CLUSTERS_ASSIGNMENT_WG_SIZE) {
            Sphere plWS = u_PC.LightData->PointLights[globalLightOffset + i].sphere;
            plWS.Origin = mul(u_PC.CameraData->ViewMatrix, float4(plWS.Origin, 1.0f)).xyz;
            lds_PointLightData[i] = plWS;
        }
        GroupMemoryBarrierWithGroupSync();

        const uint writtenPointLightCount = min(LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS, u_PC.LightData->PointLightCount - LIGHT_CLUSTERS_MAX_SHARED_POINT_LIGHTS * lightBatchIndex);
        for (uint lightIndex = 0; lightIndex < writtenPointLightCount; ++lightIndex)
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