// final.slang

#include "../../Source/ShaderDefines.hpp" 
#include "FullScreenQuad.slang"

struct PushConstantBlock
{
    uint MainPassTextureID;
    uint BloomTextureID;
};
[vk::push_constant] PushConstantBlock u_PC;

float3 TonemapACES(const float3 x) {
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 TonemapReinhard(const float3 x)
{
    return x / (x + float3(1.0f));
}

float3 ToneMapUncharted2(float3 x)
{
    const float A = 0.15f;
    const float B = 0.50f;
    const float C = 0.10f;
    const float D = 0.20f;
    const float E = 0.02f;
    const float F = 0.30f;
    const float W = 11.2f;
    const float exposure = 2.0f;

    x *= exposure;
    x = ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
    const float white = ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;
    return x / white;
}

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    const float3 bloomColor = Shaders::Texture_Heap[u_PC.BloomTextureID].Sample(fsInput.uv).rgb;
    float3 finalColor = lerp(Shaders::Texture_Heap[u_PC.MainPassTextureID].Sample(fsInput.uv).rgb, bloomColor, 0.04f);

    // Tonemap(HDR->LDR)
    finalColor.rgb =
     //    TonemapReinhard(finalColor.rgb);
   //    TonemapACES(finalColor.rgb);
    ToneMapUncharted2(finalColor.rgb)
    ;

    // Gamma correction
    finalColor.rgb = Shaders::Linear2sRGB(float4(finalColor.rgb, 1.0f)).xyz;

    return float4(finalColor, 1.0f);
}