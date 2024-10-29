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

// For NV gpus this optimization somewhy doesn't work:
// https://github.com/shader-slang/slang/issues/4983

#define USE_INTERLOCKED_MIN_MAX_PER_WORKGROUP 0
#if USE_INTERLOCKED_MIN_MAX_PER_WORKGROUP
groupshared uint2 gs_MinMaxZ;
#endif

[numthreads(DEPTH_REDUCTION_WG_SIZE_X, DEPTH_REDUCTION_WG_SIZE_Y, 1)]
[shader("compute")]
void computeMain(const uint3 DTid: SV_DispatchThreadID
#if USE_INTERLOCKED_MIN_MAX_PER_WORKGROUP
, const uint GroupIndex : SV_GroupIndex
#endif
)
{
    if (DTid.x >= u_PC.CameraData.FullResolution.x || DTid.y >= u_PC.CameraData.FullResolution.y) return;
    const float2 uv = DTid.xy * u_PC.SrcTexelSize;

    const float sampledDepth = Shaders::Texture_Heap[u_PC.DepthTextureID].SampleLevel(uv, 0).r;
    const float positiveDepthVS = -Shaders::ScreenSpaceDepthToView(sampledDepth, u_PC.CameraData->ProjectionMatrix);

    const float minWarpDepth = WaveActiveMin(positiveDepthVS);
    const float maxWarpDepth = WaveActiveMax(positiveDepthVS);

#if USE_INTERLOCKED_MIN_MAX_PER_WORKGROUP
    if (GroupIndex == 0)
    {
        gs_MinMaxZ.x = 0xffffffff;
        gs_MinMaxZ.y = 0;
    }

    GroupMemoryBarrierWithGroupSync();
#endif

    if(WaveIsFirstLane())
    {
#if USE_INTERLOCKED_MIN_MAX_PER_WORKGROUP
        InterlockedMin(gs_MinMaxZ.x, asuint(minWarpDepth));
        InterlockedMax(gs_MinMaxZ.y, asuint(maxWarpDepth));
#else
        InterlockedMin(u_PC.DepthBounds.MinMaxZ.x, asuint(minWarpDepth));
        InterlockedMax(u_PC.DepthBounds.MinMaxZ.y, asuint(maxWarpDepth));
#endif
    }

#if USE_INTERLOCKED_MIN_MAX_PER_WORKGROUP
    GroupMemoryBarrierWithGroupSync();

    if (GroupIndex == 0)
    {
        InterlockedMin(u_PC.DepthBounds.MinMaxZ.x, gs_MinMaxZ.x);
        InterlockedMax(u_PC.DepthBounds.MinMaxZ.y, gs_MinMaxZ.y);
    }
#endif
}