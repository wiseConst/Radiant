// pbr_bloom_downsample.slang

#include "../../Source/ShaderDefines.h"
#include "FullScreenQuad.slang"

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
    const float4 e = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv).rgba;
    const float3 f = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(2 * x, 0)).rgb;

    const float3 g = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(-2 * x, -2 * y)).rgb;
    const float3 h = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(0, -2 * y)).rgb;
    const float3 i = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(2 * x, -2 * y)).rgb;

    const float3 j = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(-x, y)).rgb;
    const float3 k = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(x, y)).rgb;
    const float3 l = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(-x, -y)).rgb;
    const float3 m = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(x, -y)).rgb;

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
    float4 finalColor = float4(e.rgb * 0.125f, e.a);
    finalColor.rgb += (j + k + l + m) * 0.125f;
    finalColor.rgb += (b + f + h + d) * 0.0625f;
    finalColor.rgb += (a + c + g + i) * 0.03125f;

    return finalColor;
}
