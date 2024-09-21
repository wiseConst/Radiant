// bloom_upsample_blur_compute.slang

#include "../../../Source/ShaderDefines.h"
#include <bloom_defines.hpp>

// This shader performs upsampling on a texture,
// as taken from Call Of Duty method, presented at ACM Siggraph 2014.

// Remember to add bilinear minification filter for this texture!
// Remember to use a floating-point texture format (for HDR)!
// Remember to use edge clamping for this texture!
struct PushConstantBlock
{
    uint SrcTextureID;
    uint DstTextureID;
    uint MipLevel;             // I need this to prevent loading first mip level, since its unitialized!
    float2 SrcTexelSize;       // rcp(SrcTextureResolution)
};
[[vk::push_constant]] PushConstantBlock u_PC;

static const uint TILE_SIZE_X = BLOOM_WG_SIZE_X + Shaders::s_UPSAMPLE_BLUR_TILE_BORDER * 2;
static const uint TILE_SIZE_Y = BLOOM_WG_SIZE_Y + Shaders::s_UPSAMPLE_BLUR_TILE_BORDER * 2;
static const uint2 TILE_SIZE = uint2(TILE_SIZE_X, TILE_SIZE_Y);
groupshared float3 gs_SRC_TEXTURE_CACHE[TILE_SIZE_X * TILE_SIZE_Y];

[numthreads(BLOOM_WG_SIZE_X, BLOOM_WG_SIZE_Y, 1)]
[shader("compute")]
void computeMain(uint3 DTid: SV_DispatchThreadID, const uint3 Gid: SV_GroupID, const uint3 GTid: SV_GroupThreadID, const uint GroupIndex: SV_GroupIndex)
{
    const int2 tileUpperLeft = Gid.xy * Shaders::g_BLOOM_WG_SIZE - Shaders::s_UPSAMPLE_BLUR_TILE_BORDER;
    [loop]
    for (uint t = GroupIndex; t < TILE_SIZE_X * TILE_SIZE_Y; t += BLOOM_WG_SIZE_X * BLOOM_WG_SIZE_Y)
    {
        const uint2 pixelCorner = tileUpperLeft + Shaders::unflatten2D(t, TILE_SIZE);
        const float2 uv = (pixelCorner + 0.5f) * u_PC.SrcTexelSize; // + 0.5f to make UV match pixel center
        gs_SRC_TEXTURE_CACHE[t] = Shaders::Texture_Heap[u_PC.SrcTextureID].SampleLevel(uv, 0).rgb;
    }
    GroupMemoryBarrierWithGroupSync();

    // Take 9 samples around current texel:
    // a - b - c
    // d - e - f
    // g - h - i
    // === ('e' is the current texel) ===
    const uint2 baseTileIdx = GTid.xy + Shaders::s_UPSAMPLE_BLUR_TILE_BORDER;
    const uint gsCoords[9] = {
        Shaders::flatten2D(baseTileIdx - int2(Shaders::s_UPSAMPLE_BLUR_TILE_BORDER), TILE_SIZE),    // a
        Shaders::flatten2D(baseTileIdx - int2(0, Shaders::s_UPSAMPLE_BLUR_TILE_BORDER), TILE_SIZE), // b
        Shaders::flatten2D(baseTileIdx + int2(Shaders::s_UPSAMPLE_BLUR_TILE_BORDER, -Shaders::s_UPSAMPLE_BLUR_TILE_BORDER), TILE_SIZE), // c

        Shaders::flatten2D(baseTileIdx - int2(Shaders::s_UPSAMPLE_BLUR_TILE_BORDER, 0), TILE_SIZE), // d
        Shaders::flatten2D(baseTileIdx, TILE_SIZE),                                                 // e
        Shaders::flatten2D(baseTileIdx + int2(Shaders::s_UPSAMPLE_BLUR_TILE_BORDER, 0), TILE_SIZE), // f

        Shaders::flatten2D(baseTileIdx + int2(-Shaders::s_UPSAMPLE_BLUR_TILE_BORDER, Shaders::s_UPSAMPLE_BLUR_TILE_BORDER), TILE_SIZE), // g
        Shaders::flatten2D(baseTileIdx + int2(0, Shaders::s_UPSAMPLE_BLUR_TILE_BORDER), TILE_SIZE), // h
        Shaders::flatten2D(baseTileIdx + int2(Shaders::s_UPSAMPLE_BLUR_TILE_BORDER), TILE_SIZE),    // i
    };

    const float3 a = gs_SRC_TEXTURE_CACHE[gsCoords[0]];
    const float3 b = gs_SRC_TEXTURE_CACHE[gsCoords[1]];
    const float3 c = gs_SRC_TEXTURE_CACHE[gsCoords[2]];

    const float3 d = gs_SRC_TEXTURE_CACHE[gsCoords[3]];
    const float3 e = gs_SRC_TEXTURE_CACHE[gsCoords[4]];
    const float3 f = gs_SRC_TEXTURE_CACHE[gsCoords[5]];

    const float3 g = gs_SRC_TEXTURE_CACHE[gsCoords[6]];
    const float3 h = gs_SRC_TEXTURE_CACHE[gsCoords[7]];
    const float3 i = gs_SRC_TEXTURE_CACHE[gsCoords[8]];

    // Apply weighted distribution, by using a 3x3 tent filter:
    //  1   | 1 2 1 |
    // -- * | 2 4 2 |
    // 16   | 1 2 1 |
    float3 finalColor = e * 4;
    finalColor += (b + d + f + h) * 2;
    finalColor += (a + c + g + i);
    finalColor *= 0.0625f;

    float3 srcColor = float3(0.0f);
    if (u_PC.MipLevel > 0)
    {
        srcColor = Shaders::RWImage2D_Heap_RGBA32F[u_PC.DstTextureID].Load(DTid.xy).rgb;
    }

    Shaders::RWImage2D_Heap_RGBA32F[u_PC.DstTextureID][DTid.xy] = float4(finalColor + srcColor, 1.0f);
}