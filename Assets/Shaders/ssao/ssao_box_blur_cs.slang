// ssao_box_blur_cs.slang

#include "../../../Source/ShaderDefines.h"
#include <ssao_defines.hpp>

struct PushConstantBlock
{
    uint SSAOBlurredTextureID;
    uint SSAOTextureID;
    float2 SrcTexelSize; // rcp(SSAOTextureResolution.xy)
};
[vk::push_constant] PushConstantBlock u_PC;

static const uint TILE_SIZE_X = SSAO_WG_SIZE_X + SSAO_BOX_BLUR_SIZE * 2;
static const uint TILE_SIZE_Y = SSAO_WG_SIZE_Y + SSAO_BOX_BLUR_SIZE * 2;
static const uint2 TILE_SIZE = uint2(TILE_SIZE_X, TILE_SIZE_Y);

groupshared float gs_SSAO_CACHE[TILE_SIZE_X * TILE_SIZE_Y];

[numthreads(SSAO_WG_SIZE_X, SSAO_WG_SIZE_Y, 1)]
[shader("compute")]
void computeMain(const uint3 DTid : SV_DispatchThreadID, const uint3 Gid : SV_GroupID, const uint3 GTid : SV_GroupThreadID, const uint GroupIndex : SV_GroupIndex)
{
    const int2 tileUpperLeft = Gid.xy * Shaders::g_SSAO_WG_SIZE - SSAO_BOX_BLUR_SIZE; // beginning of the TILE_SIZE_X*TILE_SIZE_Y group
    for (uint k = GroupIndex; k < TILE_SIZE_X * TILE_SIZE_Y; k += SSAO_WG_SIZE_X * SSAO_WG_SIZE_Y)
    {
        const uint2 pixel = tileUpperLeft + Shaders::unflatten2D(k, TILE_SIZE); // offseted from begin coordinates inside TILE_SIZE_X*TILE_SIZE_Y group by unflattened k
        const float2 uv = (pixel + 0.5f) * u_PC.SrcTexelSize;
        gs_SSAO_CACHE[k] = Shaders::Texture_Heap[u_PC.SSAOTextureID].SampleLevel(uv, 0).r;
    }

    GroupMemoryBarrierWithGroupSync();

    const uint2 baseCoordsGT = GTid.xy + SSAO_BOX_BLUR_SIZE;
    float ao = 0.f;
    [unroll]
    for (int x = -SSAO_BOX_BLUR_SIZE; x <= SSAO_BOX_BLUR_SIZE; ++x)
    {
        for (int y = -SSAO_BOX_BLUR_SIZE; y <= SSAO_BOX_BLUR_SIZE; ++y)
        {
            const uint ssaoSampleIdx = Shaders::flatten2D(baseCoordsGT + int2(x, y), TILE_SIZE);
            ao += gs_SSAO_CACHE[ssaoSampleIdx];
        }
    }
    ao *= Shaders::g_TotalSampleCountInv;
    Shaders::RWImage2D_Heap_R8UNORM[u_PC.SSAOBlurredTextureID][DTid.xy] = ao;
}