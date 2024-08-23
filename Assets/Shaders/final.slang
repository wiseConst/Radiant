// final.slang

#include "../../Source/ShaderDefines.h" 
#include "../../Assets/Shaders/FullScreenQuad.slang"

struct PushConstantBlock
{
    uint32_t MainPassTextureID;
};
[vk::push_constant] PushConstantBlock u_PC;

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    float4 finalColor =  Shaders::Texture_Heap[u_PC.MainPassTextureID].Sample(fsInput.uv);
    // Reinhard tone map(HDR->LDR)
    finalColor.rgb = finalColor.rgb / (finalColor.rgb + float3(1.0f));

    // Gamma correction
    finalColor.rgb = Shaders::Linear2sRGB(float4(finalColor.rgb, 1.0f)).xyz;

    return finalColor;
}