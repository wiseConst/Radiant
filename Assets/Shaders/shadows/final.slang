// final.slang

#include "../../../Source/ShaderDefines.hpp"
#include "../FullScreenQuad.slang"

struct PushConstantBlock
{
    uint MainPassTextureID;
};
[vk::push_constant] PushConstantBlock u_PC;

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    float3 finalColor = Shaders::Texture_Heap[u_PC.MainPassTextureID].Sample(fsInput.uv).rgb;

    // Tonemap(HDR->LDR)
    finalColor.rgb = Shaders::TonemapACES(finalColor.rgb);

    // Gamma correction
    finalColor.rgb = Shaders::Linear2sRGB(float4(finalColor.rgb, 1.0f)).xyz;

    return float4(finalColor, 1.0f);
}