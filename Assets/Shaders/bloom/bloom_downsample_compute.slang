// bloom_downsample_compute.slang

#include "../../../Source/ShaderDefines.h"

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

#define BLOOM_DOWNSAMPLE_WG_SIZE_X 8
#define BLOOM_DOWNSAMPLE_WG_SIZE_Y 8
#define BLOOM_DOWNSAMPLE_RADIUS 3

float RGBToLuminance(const float3 sRGBColor)
{
    return dot(sRGBColor, float3(0.299f, 0.587f, 0.114f));
}

float KarisAverage(const float3 color)
{
    // Formula is 1 / (1 + luma)
    const float luma = RGBToLuminance(Shaders::Linear2sRGB(float4(color, 0.0f)).rgb);
    return 1.0f / (1.0f + luma);
}

//groupshared float3 gs_texture_cache[(BLOOM_DOWNSAMPLE_WG_SIZE_X + 2) * (BLOOM_DOWNSAMPLE_WG_SIZE_Y + 2)];

[numthreads(BLOOM_DOWNSAMPLE_WG_SIZE_X, BLOOM_DOWNSAMPLE_WG_SIZE_Y, 1)]
[shader("compute")]
void computeMain(uint3 DTid: SV_DispatchThreadID, uint Gid: SV_GroupID, uint d : SV_GroupThreadID)
{ 
   // for(uint i = )

    // Take 13 samples around current texel:
    // a - b - c
    // - j - k -
    // d - e - f
    // - l - m -
    // g - h - i
    // === ('e' is the current texel) ===
    const float x = u_PC.SrcTexelSize.x;
    const float y = u_PC.SrcTexelSize.y;

    const float3 a = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(0, 0));
    const float3 b = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(x, 0));
    const float3 c = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(2 * x, 0));

    const float3 d = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(0, y));
    const float3 e = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(x, y));
    const float3 f = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(2 * x, y));

    const float3 g = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(0, 2 * y));
    const float3 h = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(x, 2 * y));
    const float3 i = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(2 * x, 2 * y));

    const float3 j = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(0.5 * x, 0.5 * y));
    const float3 k = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(0.5 * x, 1.5 * y));
    const float3 l = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(0.5 * x, 1.5 * y));
    const float3 m = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(1.5 * x, 1.5 * y));
    
    float3 finalColor = 0.0f;
    if (u_PC.MipLevel == 0)
    {
        // NOTE: Fix firefly effect show in 166 slide in pptx https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare

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
        const float kw[5] = { KarisAverage(groups[0]), KarisAverage(groups[1]), KarisAverage(groups[2]),
                              KarisAverage(groups[3]), KarisAverage(groups[4]) };

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
    Shaders::RWImage2D_Heap_F3[u_PC.DstTextureID][DTid.xy] = finalColor;
}