// light_clusters_build.slang

#include "../../../Source/ShaderDefines.h"
#include <light_clusters_defines.hpp>

struct PushConstantBlock
{
    const Shaders::CameraData *CameraData;
    AABB *Clusters;
};
[[vk::push_constant]] PushConstantBlock u_PC;

// Returns the intersection point of an infinite line and a plane.
float3 IntersectSegmentPlane(const float3 startPoint, const float3 endPoint, const Plane plane)
{
    const float3 direction = endPoint - startPoint;
    const float t = (plane.Distance - dot(plane.Normal, startPoint)) / dot(plane.Normal, direction);
    return startPoint + t * direction; // NOTE: This doesn't check if 't' is in range [0, 1]!
}

// NOTES:
// 0) Whole compute shader targets view space where Z is negative.
// 1) Working in reversed Z space(so near = 1.0f, and far = 0.0f).
// 2) Impl details: imagine subdiv scheme 16x8x24 => 3072 clusters, we have to use compute capabilities smarter,
// let's assume workgroup local size(4, 4, 4), it means we have to dispatch
// 3072/(4*4*4) = 48 workgroups instead of 3072(in case 1 thread of workgroup builds cluster)

[shader("compute")]
[numthreads(LIGHT_CLUSTERS_BUILD_WG_SIZE, LIGHT_CLUSTERS_BUILD_WG_SIZE, LIGHT_CLUSTERS_BUILD_WG_SIZE)]
void computeMain(uint3 DTid: SV_DispatchThreadID)
{
    const uint froxelIndex = DTid.x + DTid.y * LIGHT_CLUSTERS_SUBDIVISION_X + DTid.z * LIGHT_CLUSTERS_SUBDIVISION_Y * LIGHT_CLUSTERS_SUBDIVISION_X;
    if (froxelIndex >= LIGHT_CLUSTERS_COUNT) return;
    
    // Calculate the min and max points of a tile in screen space(not kind of screen space tbh).
    const float2 minPosSS = Shaders::s_INV_LIGHT_CLUSTER_SUBDIVISIONS.xy * DTid.xy;
    const float2 maxPosSS = Shaders::s_INV_LIGHT_CLUSTER_SUBDIVISIONS.xy * (DTid.xy + 1) ;

    // Convert them to view space sitting on the near plane.
    const float3 minPosVS = Shaders::ScreenSpaceToView(minPosSS, 1.0f, u_PC.CameraData->InvProjectionMatrix).xyz;
    const float3 maxPosVS = Shaders::ScreenSpaceToView(maxPosSS, 1.0f, u_PC.CameraData->InvProjectionMatrix).xyz;

    const float froxelNearVS = -u_PC.CameraData.zNearFar.x * pow(u_PC.CameraData.zNearFar.y / u_PC.CameraData.zNearFar.x, (float)DTid.z * Shaders::s_INV_LIGHT_CLUSTER_SUBDIVISIONS.z);
    const Plane nearPlane = Plane(float3(0.0f, 0.0f, 1.0f), froxelNearVS);

    const float froxelFarVS = -u_PC.CameraData.zNearFar.x * pow(u_PC.CameraData.zNearFar.y / u_PC.CameraData.zNearFar.x, (float)(DTid.z + 1) * Shaders::s_INV_LIGHT_CLUSTER_SUBDIVISIONS.z);
    const Plane farPlane = Plane(float3(0.0f, 0.0f, 1.0f), froxelFarVS);

    // Find the 4 intersection points from a tile's min/max points to this cluster's near and far planes.
    const float3 viewPos = float3(0.0f);
    const float3 minPointNear = IntersectSegmentPlane(viewPos, minPosVS, nearPlane);
    const float3 minPointFar  = IntersectSegmentPlane(viewPos, minPosVS, farPlane);
    const float3 maxPointNear = IntersectSegmentPlane(viewPos, maxPosVS, nearPlane);
    const float3 maxPointFar = IntersectSegmentPlane(viewPos, maxPosVS, farPlane);
    u_PC.Clusters[froxelIndex] = AABB(min(min(maxPointNear, maxPointFar), min(minPointNear, minPointFar)), max(max(minPointNear, minPointFar), max(maxPointNear, maxPointFar)));
}