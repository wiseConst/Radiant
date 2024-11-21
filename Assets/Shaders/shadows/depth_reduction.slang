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

groupshared uint gs_MinDepth;
groupshared uint gs_MaxDepth;

// A bit(0.01) slower idk why
#define USE_WAVE_OPS 0

[numthreads(DEPTH_REDUCTION_WG_SIZE_X, DEPTH_REDUCTION_WG_SIZE_Y, 1)]
[shader("compute")]
void computeMain(const uint3 DTid: SV_DispatchThreadID, const uint GroupIndex : SV_GroupIndex)
{
    if (any(DTid.xy >= u_PC.CameraData.FullResolution.xy)) return;
    const float2 uv = DTid.xy * u_PC.SrcTexelSize;

    const float sampledDepth = Shaders::Texture_Heap[u_PC.DepthTextureID].SampleLevel(uv, 0).r;

    if (GroupIndex == 0)
    {
        gs_MinDepth = 0xffffffff;
        gs_MaxDepth = 0;
    }

    GroupMemoryBarrierWithGroupSync();

    // Don't include far-plane (i.e. no geometry). Reversed depth buffer
    if (sampledDepth != 0.0f)
    {
        const uint uDepth = asuint(sampledDepth);
#if USE_WAVE_OPS
        const uint waveMinDepth = WaveActiveMin(uDepth);
        const uint waveMaxDepth = WaveActiveMax(uDepth);
        if (WaveIsFirstLane())
        {
            InterlockedMin(gs_MinDepth, waveMinDepth);
            InterlockedMax(gs_MaxDepth, waveMaxDepth);
        }
#else
        InterlockedMin(gs_MinDepth, uDepth);
        InterlockedMax(gs_MaxDepth, uDepth);
#endif
    }

    // Don't forget to wait for other warps.
    GroupMemoryBarrierWithGroupSync();

    if (GroupIndex == 0)
    {
        InterlockedMin(u_PC.DepthBounds.MinMaxZ.x, gs_MinDepth);
        InterlockedMax(u_PC.DepthBounds.MinMaxZ.y, gs_MaxDepth);
    }
}
