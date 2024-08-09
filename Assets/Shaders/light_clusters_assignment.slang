// cull_light_clusters.slang

#include "../../Source/ShaderDefines.h"

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

#define LIGHT_CLUSTER_CULL_WG_SIZE 128

[shader("compute")]
[numthreads(LIGHT_CLUSTER_CULL_WG_SIZE, 1, 1)]
void computeMain(uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID)
{
    const uint froxelIndex = Gid.x * LIGHT_CLUSTER_CULL_WG_SIZE + GTid.x;
    u_PC.LightClusterList[froxelIndex].PointLightCount = 0;

    for (uint32_t i = 0; i < u_PC.LightData->PointLightCount; ++i)
    {
        Sphere plWS = u_PC.LightData->PointLights[i].sphere;
        Sphere plVS = Sphere(mul(u_PC.CameraData->ViewMatrix, float4(plWS.Origin, 1.0f)).xyz, plWS.Radius);
        if (!TestSphereAABB(plVS, u_PC.Clusters[froxelIndex])) continue;

        u_PC.LightClusterList[froxelIndex].PointLightIndices[u_PC.LightClusterList[froxelIndex].PointLightCount] = i;
        ++u_PC.LightClusterList[froxelIndex].PointLightCount;
    }
}