// final.slang

#include "../../Source/ShaderDefines.hpp" 
#include "FullScreenQuad.slang"

struct PushConstantBlock
{
    uint MainPassTextureID;
    uint BloomTextureID;
};
[vk::push_constant] PushConstantBlock u_PC;

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    const float3 bloomColor = Shaders::Texture_Heap[u_PC.BloomTextureID].Sample(fsInput.uv).rgb;
    float3 finalColor = lerp(Shaders::Texture_Heap[u_PC.MainPassTextureID].Sample(fsInput.uv).rgb, bloomColor, 0.04f);

    // Tonemap(HDR->LDR)
    finalColor.rgb =
        //    Shaders::TonemapReinhard(finalColor.rgb);
        Shaders::TonemapACES(finalColor.rgb);
   // Shaders::ToneMapUncharted2(finalColor.rgb)
    ;

    // Gamma correction
    finalColor.rgb = Shaders::Linear2sRGB(float4(finalColor.rgb, 1.0f)).xyz;

    return float4(finalColor, 1.0f);
}