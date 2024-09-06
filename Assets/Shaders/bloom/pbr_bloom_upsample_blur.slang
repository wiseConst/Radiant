// pbr_bloom_upsample_blur.slang

#include "../../../Source/ShaderDefines.h"
#include "../FullScreenQuad.slang"

// This shader performs upsampling on a texture,
// as taken from Call Of Duty method, presented at ACM Siggraph 2014.

// Remember to add bilinear minification filter for this texture!
// Remember to use a floating-point texture format (for HDR)!
// Remember to use edge clamping for this texture!
struct PushConstantBlock
{
    uint32_t SrcTextureID;
    float2 SampleFilterRadius; // NOTE: Make sure aspect ratio is taken into account!
};
[vk::push_constant] PushConstantBlock u_PC;

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{ 
    // The filter kernel is applied with a radius, specified in texture
    // coordinates, so that the radius will vary across mip resolutions.
    const float x = u_PC.SampleFilterRadius.x;
    const float y = u_PC.SampleFilterRadius.y;

    // Take 9 samples around current texel:
    // a - b - c
    // d - e - f
    // g - h - i
    // === ('e' is the current texel) ===
    const float3 a = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(-x, y)).rgb;
    const float3 b = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(0, y)).rgb;
    const float3 c = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(x, y)).rgb;

    const float3 d = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(-x, 0)).rgb;
    const float3 e = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv).rgb;
    const float3 f = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(x, 0)).rgb;

    const float3 g = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(-x, -y)).rgb;
    const float3 h = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(0, -y)).rgb;
    const float3 i = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(fsInput.uv + float2(x, -y)).rgb;

    // Apply weighted distribution, by using a 3x3 tent filter:
    //  1   | 1 2 1 |
    // -- * | 2 4 2 |
    // 16   | 1 2 1 |
    float3 finalColor = e * 4;
    finalColor += (b + d + f + h) * 2;
    finalColor += (a + c + g + i);
    finalColor *= 0.0625f;

    return float4(finalColor, 1.0f);
}