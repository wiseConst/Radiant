// bloom_downsample_compute.slang

#include "../../../Source/ShaderDefines.h"
#include <bloom_defines.hpp>

// This shader performs downsampling on a texture,
// as taken from Call Of Duty method, presented at ACM Siggraph 2014.
// This particular method was customly designed to eliminate
// "pulsating artifacts and temporal stability issues".

// Remember to add bilinear minification filter for this texture!
// Remember to use a floating-point texture format (for HDR)!
// Remember to use edge clamping for this texture!
struct PushConstantBlock
{
    uint SrcTextureID;
    uint DstTextureID;
    uint MipLevel;
    float2 SrcTexelSize; // rcp(SrcTextureResolution)
};
[[vk::push_constant]] PushConstantBlock u_PC;

static const uint TILE_SIZE_X = BLOOM_WG_SIZE_X + Shaders::s_DOWNSAMPLE_TILE_BORDER * 2;
static const uint TILE_SIZE_Y = BLOOM_WG_SIZE_Y + Shaders::s_DOWNSAMPLE_TILE_BORDER * 2;
static const uint2 TILE_SIZE = uint2(TILE_SIZE_X, TILE_SIZE_Y);
groupshared float3 gs_SRC_TEXTURE_CACHE[TILE_SIZE_X * TILE_SIZE_Y];

[numthreads(BLOOM_WG_SIZE_X, BLOOM_WG_SIZE_Y, 1)]
[shader("compute")]
void computeMain(uint3 DTid: SV_DispatchThreadID, const uint3 Gid: SV_GroupID, const uint3 GTid : SV_GroupThreadID, const uint GroupIndex : SV_GroupIndex)
{
    const int2 tileUpperLeft = Gid.xy * Shaders::g_BLOOM_WG_SIZE - Shaders::s_DOWNSAMPLE_TILE_BORDER;
    [loop]
    for (uint t = GroupIndex; t < TILE_SIZE_X * TILE_SIZE_Y; t += BLOOM_WG_SIZE_X * BLOOM_WG_SIZE_Y)
    {
        const uint2 pixelCorner = tileUpperLeft + Shaders::unflatten2D(t, TILE_SIZE);
        const float2 uv = (pixelCorner + 0.5f) * u_PC.SrcTexelSize; // + 0.5f to make UV match pixel center
        gs_SRC_TEXTURE_CACHE[t] = Shaders::Texture_Heap[u_PC.SrcTextureID].SampleLevel(uv, 0).rgb;
    }
    GroupMemoryBarrierWithGroupSync();

    // Take 13 samples around current texel:
    // a - b - c
    // - j - k -
    // d - e - f
    // - l - m -
    // g - h - i
    // === ('e' is the current texel) ===
    const uint2 baseTileIdx = GTid.xy + Shaders::s_DOWNSAMPLE_TILE_BORDER;
    const uint gsCoords[13] = {
        Shaders::flatten2D(baseTileIdx - int2(Shaders::s_DOWNSAMPLE_TILE_BORDER), TILE_SIZE),    // a
        Shaders::flatten2D(baseTileIdx - int2(0, Shaders::s_DOWNSAMPLE_TILE_BORDER), TILE_SIZE), // b
        Shaders::flatten2D(baseTileIdx + int2(Shaders::s_DOWNSAMPLE_TILE_BORDER, -Shaders::s_DOWNSAMPLE_TILE_BORDER), TILE_SIZE), // c

        Shaders::flatten2D(baseTileIdx - int2(int(Shaders::s_DOWNSAMPLE_TILE_BORDER * 0.5f)), TILE_SIZE), // j
        Shaders::flatten2D(baseTileIdx + int2(int(Shaders::s_DOWNSAMPLE_TILE_BORDER * 0.5f), -int(Shaders::s_DOWNSAMPLE_TILE_BORDER * 0.5f)), TILE_SIZE), // k

        Shaders::flatten2D(baseTileIdx - int2(Shaders::s_DOWNSAMPLE_TILE_BORDER, 0), TILE_SIZE), // d
        Shaders::flatten2D(baseTileIdx, TILE_SIZE),                                              // e
        Shaders::flatten2D(baseTileIdx + int2(Shaders::s_DOWNSAMPLE_TILE_BORDER, 0), TILE_SIZE), // f

        Shaders::flatten2D(baseTileIdx + int2(-int(Shaders::s_DOWNSAMPLE_TILE_BORDER * 0.5f), int(Shaders::s_DOWNSAMPLE_TILE_BORDER * 0.5f)), TILE_SIZE),  // l
        Shaders::flatten2D(baseTileIdx + int2(int(Shaders::s_DOWNSAMPLE_TILE_BORDER * 0.5f)), TILE_SIZE), // m

        Shaders::flatten2D(baseTileIdx + int2(-Shaders::s_DOWNSAMPLE_TILE_BORDER, Shaders::s_DOWNSAMPLE_TILE_BORDER), TILE_SIZE),       // g    
        Shaders::flatten2D(baseTileIdx + int2(0, Shaders::s_DOWNSAMPLE_TILE_BORDER), TILE_SIZE),        // h 
        Shaders::flatten2D(baseTileIdx + int2(Shaders::s_DOWNSAMPLE_TILE_BORDER, 0), TILE_SIZE), // i
    };

    const float3 a = gs_SRC_TEXTURE_CACHE[gsCoords[0]];
    const float3 b = gs_SRC_TEXTURE_CACHE[gsCoords[1]];
    const float3 c = gs_SRC_TEXTURE_CACHE[gsCoords[2]];

    const float3 j = gs_SRC_TEXTURE_CACHE[gsCoords[3]];
    const float3 k = gs_SRC_TEXTURE_CACHE[gsCoords[4]];

    const float3 d = gs_SRC_TEXTURE_CACHE[gsCoords[5]];
    const float3 e = gs_SRC_TEXTURE_CACHE[gsCoords[6]];
    const float3 f = gs_SRC_TEXTURE_CACHE[gsCoords[7]];

    const float3 l = gs_SRC_TEXTURE_CACHE[gsCoords[8]];
    const float3 m = gs_SRC_TEXTURE_CACHE[gsCoords[9]];

    const float3 g = gs_SRC_TEXTURE_CACHE[gsCoords[10]];
    const float3 h = gs_SRC_TEXTURE_CACHE[gsCoords[11]];
    const float3 i = gs_SRC_TEXTURE_CACHE[gsCoords[12]];
    
    float3 finalColor = 0.0f;
    if (u_PC.MipLevel == 0)
    {
        // NOTE: Fix firefly effect shown in 166 slide in pptx https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare

        // We are reading mip 0, so we need to apply Karis average to each block
        // of 4 samples to prevent fireflies (very bright subpixels, leads to pulsating
        // artifacts).
        const float3 groups[5] = {
            (a + b + d + e) * 0.25f,
            (b + c + e + f) * 0.25f,
            (d + e + g + h) * 0.25f,
            (e + f + h + i) * 0.25f,
            (j + k + l + m) * 0.25f,
        };
        const float kw[5] = { Shaders::KarisAverage(groups[0]), Shaders::KarisAverage(groups[1]), Shaders::KarisAverage(groups[2]),
                              Shaders::KarisAverage(groups[3]), Shaders::KarisAverage(groups[4]) };

        // NOTE: Energy conserving, renormalizing the Karis average weights themselves to avoid the bloom becoming noticeably darker.
        finalColor = (kw[0] * groups[0] + kw[1] * groups[1] + kw[2] * groups[2] + kw[3] * groups[3] + kw[4] * groups[4]) / (kw[0] + kw[1] + kw[2] + kw[3] + kw[4]);
    } else {
        // Apply weighted distribution:
        // 0.5 + 0.125 + 0.125 + 0.125 + 0.125 = 1
        // a,b,d,e * 0.125
        // b,c,e,f * 0.125
        // d,e,g,h * 0.125
        // e,f,h,i * 0.125
        // j,k,l,m * 0.5
        // This shows 5 square areas that are being sampled. But some of them overlap,
        // so to have an energy preserving downsample we need to make some adjustments.
        // The weights are the distributed, so that the sum of j,k,l,m (e.g.)
        // contribute 0.5 to the final color output. The code below is written
        // to effectively yield this sum. We get:
        // 0.125*5 + 0.03125*4 + 0.0625*4 = 1
        finalColor = e * 0.125f;
        finalColor += (j + k + l + m) * 0.125f;
        finalColor += (b + f + h + d) * 0.0625f;
        finalColor += (a + c + g + i) * 0.03125f;
    }

    // NOTE: To get rid of black boxes.
    finalColor = max(finalColor, Shaders::s_KINDA_SMALL_NUMBER);
    Shaders::RWImage2D_Heap_RGBA32F[u_PC.DstTextureID][DTid.xy] = float4(finalColor, 1.0f);
}