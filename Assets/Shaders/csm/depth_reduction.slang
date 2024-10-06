// depth_reduction.slang

#include "../../../Source/ShaderDefines.hpp"
#include <csm_defines.hpp>

struct PushConstantBlock
{
    uint DepthTextureID;
    float2 SrcTexelSize;
    const Shaders::CameraData *CameraData;
    Shaders::DepthBounds *DepthBounds;
};
[[vk::push_constant]] PushConstantBlock u_PC;

[numthreads(DEPTH_REDUCTION_WG_SIZE_X, DEPTH_REDUCTION_WG_SIZE_Y, 1)]
[shader("compute")]
void computeMain(const uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= u_PC.CameraData.FullResolution.xy)) return;
    const float2 uv = DTid.xy * u_PC.SrcTexelSize;

    const float sampledDepth = Shaders::Texture_Heap[u_PC.DepthTextureID].SampleLevel(uv, 0).r;
    const float positiveDepthVS = -Shaders::ScreenSpaceDepthToView(sampledDepth, u_PC.CameraData->ProjectionMatrix);

    const float minWarpDepth = WaveActiveMin(positiveDepthVS);
    const float maxWarpDepth = WaveActiveMax(positiveDepthVS);

    InterlockedMin(u_PC.DepthBounds.MinMaxZ.x, asuint(minWarpDepth));
    InterlockedMax(u_PC.DepthBounds.MinMaxZ.y, asuint(maxWarpDepth));
}