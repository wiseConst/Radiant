// build_light_clusters.slang

#include "../../Source/ShaderDefines.h"

struct PushConstantBlock
{
    const Shaders::CameraData *CameraData;
    const AABB *Clusters;
    uint3 WorkGroupsNum;
};
[vk::push_constant] PushConstantBlock u_PC;

[shader("compute")]
[numthreads(LIGHT_CLUSTER_SUBDIVISON_SIZE_X, LIGHT_CLUSTER_SUBDIVISON_SIZE_Y, LIGHT_CLUSTER_SUBDIVISON_SIZE_Z)]
void computeMain(uint3 tid: SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    const uint tileIndex = gid.x + gid.y * u_PC.WorkGroupsNum.x + gid.z * u_PC.WorkGroupsNum.y * u_PC.WorkGroupsNum.z; 


   // u_PC.Clusters[tileIndex] = aa

}