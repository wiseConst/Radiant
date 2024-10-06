// pbr_bloom_downsample.slang

#include "../../../Source/ShaderDefines.hpp"
#include "../FullScreenQuad.slang"
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
    uint32_t SrcTextureID;
    uint32_t mipLevel;
    float2 SrcTexelSize; // rcp(SrcTextureResolution)
};
[vk::push_constant] PushConstantBlock u_PC;

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    // Take 13 samples around current texel:
    // a - b - c
    // - j - k -
    // d - e - f
    // - l - m -
    // g - h - i
    // === ('e' is the current texel) ===
    const float x = u_PC.SrcTexelSize.x;
    const float y = u_PC.SrcTexelSize.y;

    const float3 a = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(-2 * x, 2 * y)).rgb;
    const float3 b = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(0, 2 * y)).rgb;
    const float3 c = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(2 * x, 2 * y)).rgb;

    const float3 d = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(-2 * x, 0)).rgb;
    const float3 e = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv).rgb;
    const float3 f = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(2 * x, 0)).rgb;

    const float3 g = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(-2 * x, -2 * y)).rgb;
    const float3 h = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(0, -2 * y)).rgb;
    const float3 i = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(2 * x, -2 * y)).rgb;

    const float3 j = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(-x, y)).rgb;
    const float3 k = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(x, y)).rgb;
    const float3 l = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(-x, -y)).rgb;
    const float3 m = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(x, -y)).rgb;

    float3 finalColor = 0.0f;
    if (u_PC.mipLevel == 0)
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
    return float4(finalColor, 1.0f);
}
