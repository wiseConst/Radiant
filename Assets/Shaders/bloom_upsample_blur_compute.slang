// bloom_upsample_blur_compute.slang

#include "../../Source/ShaderDefines.h"

// This shader performs upsampling on a texture,
// as taken from Call Of Duty method, presented at ACM Siggraph 2014.

// Remember to add bilinear minification filter for this texture!
// Remember to use a floating-point texture format (for HDR)!
// Remember to use edge clamping for this texture!
struct PushConstantBlock
{
    uint SrcTextureID;
    uint DstTextureID;
    uint MipLevel; // I need this to prevent loading first mip level, since its unitialized!
    float2 SampleFilterRadius; // NOTE: Make sure aspect ratio is taken into account!
};
[[vk::push_constant]] PushConstantBlock u_PC;

#define BLOOM_UPSAMPLE_BLUR_WG_SIZE_X 8
#define BLOOM_UPSAMPLE_BLUR_WG_SIZE_Y 4

[numthreads(BLOOM_UPSAMPLE_BLUR_WG_SIZE_X, BLOOM_UPSAMPLE_BLUR_WG_SIZE_Y, 1)]
[shader("compute")]
void computeMain(uint3 DTid : SV_DispatchThreadID)
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
    const float3 a = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(-x, y));
    const float3 b = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(0, y));
    const float3 c = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(x, y));

    const float3 d = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(-x, 0));
    const float3 e = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy));
    const float3 f = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(x, 0));

    const float3 g = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(-x, -y));
    const float3 h = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(0, -y));
    const float3 i = Shaders::RWImage2D_Heap_F3[u_PC.SrcTextureID].Load(int2(DTid.xy) + int2(x, -y));

    // Apply weighted distribution, by using a 3x3 tent filter:
    //  1   | 1 2 1 |
    // -- * | 2 4 2 |
    // 16   | 1 2 1 |
    float3 finalColor = e * 4;
    finalColor += (b + d + f + h) * 2;
    finalColor += (a + c + g + i);
    finalColor *= 0.0625f;

    float3 srcColor = float3(0.0f);
    if (u_PC.MipLevel != 0) srcColor =  Shaders::RWImage2D_Heap_F3[u_PC.DstTextureID].Load(float2(DTid.xy));

    Shaders::RWImage2D_Heap_F3[u_PC.DstTextureID][DTid.xy] = finalColor + srcColor;
}