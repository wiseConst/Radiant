// depth_reduction.slang

#include "../../../Source/ShaderDefines.h"
#include <csm_defines.hpp>

struct PushConstantBlock
{
    uint DepthTextureID;
    float2 SrcTexelSize;
    float2 DepthUnpackConsts;
    Shaders::DepthBounds *DepthBounds;
};
[[vk::push_constant]] PushConstantBlock u_PC;

[numthreads(DEPTH_REDUCTION_WG_SIZE_X, DEPTH_REDUCTION_WG_SIZE_Y, 1)]
[shader("compute")]
void computeMain(const uint3 DTid : SV_DispatchThreadID)
{
    uint depthMin = 0xFFFFFFFF;
    uint depthMax = 0;

    const float2 uv = DTid.xy * u_PC.SrcTexelSize;
    const float sampledDepth = Shaders::Texture_Heap[u_PC.DepthTextureID].SampleLevel(uv, 0).r;
    const float depthVS = Shaders::ScreenSpaceDepthToView(sampledDepth, u_PC.DepthUnpackConsts);

    const float minWarpDepth = WaveActiveMin(depthVS);
    const float maxWarpDepth = WaveActiveMax(depthVS);

    InterlockedMin(u_PC.DepthBounds.MinMaxZ.x, asuint(minWarpDepth));
    InterlockedMax(u_PC.DepthBounds.MinMaxZ.y, asuint(maxWarpDepth));
}