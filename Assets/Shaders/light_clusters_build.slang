// build_light_clusters.slang

#include "../../Source/ShaderDefines.h"

struct PushConstantBlock
{
    const Shaders::CameraData *CameraData;
    AABB *Clusters;
    uint3 WorkGroupsNum;
};
[vk::push_constant] PushConstantBlock u_PC;

// Returns the intersection point of an infinite line and a plane.
float3 IntersectSegmentPlane(const float3 startPoint, const float3 endPoint, const Plane plane)
{
    const float3 direction = endPoint - startPoint;
    const float t = (plane.Distance - dot(plane.Normal, startPoint)) / dot(plane.Normal, direction);
    return startPoint + t * direction; // NOTE: This doesn't check if 't' is in range [0, 1]!
}

// NOTES:
// 1) Working in reversed Z space(so near = 1.0f, and far = 0.0f).
// 2) For now!!! We dispatch 1 workgroup that has only 1 fucking thread that computes our froxel
// 3) For now!!! u_PC.WorkGroupsNum is the same as Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS

[shader("compute")]
//[numthreads(LIGHT_CLUSTER_SUBDIVISON_SIZE_X, LIGHT_CLUSTER_SUBDIVISON_SIZE_Y, LIGHT_CLUSTER_SUBDIVISON_SIZE_Z)]
[numthreads(1, 1, 1)]
void computeMain(uint3 DTid: SV_DispatchThreadID, uint3 Gid : SV_GroupID)
{
    //const uint lid = WaveGetLaneIndex();

    const float3 viewPos = float3(0.0f);

    // How many pixels in XY does a froxel use
    const float2 froxelSize = u_PC.CameraData->FullResolution / u_PC.WorkGroupsNum.xy;

    // calculate the min and max points of a tile in screen space
    const float2 minPosSS = froxelSize * Gid.xy;
    const float2 maxPosSS = froxelSize * (Gid.xy + 1);

    // convert them to view space sitting on the near plane
    const float3 minPosVS = Shaders::ScreenSpaceToView(minPosSS * u_PC.CameraData->InvFullResolution, 1.0f, u_PC.CameraData->InvProjectionMatrix).xyz;
    const float3 maxPosVS = Shaders::ScreenSpaceToView(maxPosSS * u_PC.CameraData->InvFullResolution, 1.0f, u_PC.CameraData->InvProjectionMatrix).xyz;

    const float froxelNearVS = -u_PC.CameraData.zNearFar.y * pow(u_PC.CameraData.zNearFar.x / u_PC.CameraData.zNearFar.y, (float)Gid.z / (float)u_PC.WorkGroupsNum.z);
    const Plane nearPlane = Plane(float3(0.0f, 0.0f, 1.0f), froxelNearVS);
 
    const float froxelFarVS  = -u_PC.CameraData.zNearFar.y * pow(u_PC.CameraData.zNearFar.x / u_PC.CameraData.zNearFar.y, (float)(Gid.z + 1) / (float)u_PC.WorkGroupsNum.z);
    const Plane farPlane = Plane(float3(0.0f, 0.0f, 1.0f), froxelFarVS);
    
    // Find the 4 intersection points from a tile's min/max points to this cluster's near and far planes
    const float3 minPointNear = IntersectSegmentPlane(viewPos, minPosVS, nearPlane);
    const float3 minPointFar  = IntersectSegmentPlane(viewPos, minPosVS, farPlane);
    const float3 maxPointNear = IntersectSegmentPlane(viewPos, maxPosVS, nearPlane);
    const float3 maxPointFar  = IntersectSegmentPlane(viewPos, maxPosVS, farPlane);

    // const uint froxelIndex = Gid.z * u_PC.WorkGroupsNum.x * u_PC.WorkGroupsNum.y + Gid.y * u_PC.WorkGroupsNum.x + Gid.x;
    const uint froxelIndex = Gid.z * Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS.x * Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS.y + Gid.y * Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS.x + Gid.x;
    u_PC.Clusters[froxelIndex] = AABB(min(min(maxPointNear, maxPointFar), min(minPointNear, minPointFar)), max(max(minPointNear, minPointFar), max(maxPointNear, maxPointFar)));
}