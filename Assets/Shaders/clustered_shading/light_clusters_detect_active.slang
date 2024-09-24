// light_clusters_detect_active.slang

#include "../../../Source/ShaderDefines.h"
#include <light_clusters_defines.hpp>

struct PushConstantBlock
{
    uint DepthTextureID;
    const Shaders::CameraData *CameraData;
    const AABB *Clusters;
    Shaders::LightClusterActiveList *ActiveLightClusters;
};
[[vk::push_constant]] PushConstantBlock u_PC;

// NOTE: Each thread processes all clusters along Z axis.
// process clusters v pryamougol'nikax, so you read X*Y times depth texture and don't read it X*Y*Z.

// peredelat' nado, 

[numthreads(LIGHT_CLUSTERS_DETECT_ACTIVE_WG_SIZE_X, LIGHT_CLUSTERS_DETECT_ACTIVE_WG_SIZE_Y, 1)]
[shader("compute")]
void computeMain(const uint3 DTid: SV_DispatchThreadID, const uint3 Gid : SV_GroupID)
{
    const float2 uv = DTid.xy * u_PC.CameraData.InvFullResolution;
    const float depthVS = Shaders::ScreenSpaceDepthToView(Shaders::Texture_Heap[u_PC.DepthTextureID].SampleLevel(uv, 0).r, u_PC.CameraData.DepthUnpackConsts);

    // TODO: Add in rendergraph ability ClearOnExecute() in c++ side.
    for (uint z = 0; z < LIGHT_CLUSTERS_SUBDIVISION_Z; ++z)
    {
        // TODO: remap Z, cuz it sucks
        const uint froxelIndex = Shaders::flatten3D( uint3(DTid.xy, z), uint3(LIGHT_CLUSTERS_SUBDIVISION_X, LIGHT_CLUSTERS_SUBDIVISION_Y, LIGHT_CLUSTERS_SUBDIVISION_Z));
        if (froxelIndex >= LIGHT_CLUSTERS_COUNT) continue;
        const AABB cluster = u_PC.Clusters[froxelIndex];

        //if(Gid ==0)
        //{
        //   printf("DTid: (%u, %u), z: %u, froxel index: %u, depthVS: %f, maxZ: %f\n", DTid.x, DTid.y, z, froxelIndex, depthVS, cluster.Max.z);
        //}

        if (cluster.Max.z >= depthVS)
        {
            const uint froxelBitmaskIndex = uint(froxelIndex * Shaders::s_INV_LIGHT_CLUSTERS_WORD_SIZE);
            const uint froxelBit = 1u << (froxelIndex % LIGHT_CLUSTERS_WORD_SIZE);
            u_PC.ActiveLightClusters.ActiveClusters[froxelBitmaskIndex] |= froxelBit;
        }
    }
}