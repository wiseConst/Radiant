// FullScreenClearPass.slang

#include "../../Source/ShaderDefines.h" 
#include "FullScreenQuad.slang"

//struct PushConstantBlock
//{
//    uint32_t MainPassTextureID;
//    uint32_t BloomTextureID;
//};
//[vk::push_constant] PushConstantBlock u_PC;

//[shader("fragment")]
//float4 fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
//{
//    const float4 bloomColor = float4(Shaders::Texture_Heap[u_PC.BloomTextureID].Sample(fsInput.uv).rgb, 1.0f);
//    float4 finalColor = lerp(Shaders::Texture_Heap[u_PC.MainPassTextureID].Sample(fsInput.uv), bloomColor, 0.04f);
//    // Reinhard tone map(HDR->LDR)
//    finalColor.rgb = finalColor.rgb / (finalColor.rgb + float3(1.0f));

//    // Gamma correction
//    finalColor.rgb = Shaders::Linear2sRGB(float4(finalColor.rgb, 1.0f)).xyz;

//    return finalColor;
//}