// final.slang

#include <../../Source/ShaderDefines.hpp> 
#include <common/FullScreenQuad.slang>
// #include <anti_aliasing/fxaa.slang>

struct PushConstantBlock
{
    float2 SrcTexelSize;
    uint MainPassTextureID;
    uint BloomTextureID;
};
[vk::push_constant] PushConstantBlock u_PC;

void BloomContribution(inout float3 srcColor, const float2 uv)
{
    const float3 bloomColor = Shaders::Texture_Heap[u_PC.BloomTextureID].Sample(uv).rgb;
    srcColor = lerp(srcColor, bloomColor, Shaders::s_BASE_REFLECTIVITY);
}

// Tonemap(HDR->LDR)
void ToneMap(inout float3 srcColor, const Shaders::EToneMapMode toneMapMode)
{
    switch (toneMapMode)
    {
    case Shaders::EToneMapMode::TONE_MAP_MODE_REINHARD: srcColor = Shaders::TonemapReinhard(srcColor); break;
    case Shaders::EToneMapMode::TONE_MAP_MODE_UNCHARTED2: srcColor = Shaders::ToneMapUncharted2(srcColor); break;
    case Shaders::EToneMapMode::TONE_MAP_MODE_ACES: srcColor = Shaders::TonemapACES(srcColor); break;
    }
}

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    float3 srcColor = Shaders::Texture_Heap[u_PC.MainPassTextureID].Sample(fsInput.uv).rgb;

    ToneMap(srcColor, Shaders::EToneMapMode::TONE_MAP_MODE_ACES);

    // TODO: in order to integrate FXAA, input color should be LDR, so combine with bloom, tonemap and then add fxaa + gamma corr
 //   FXAA_Pixel_Shader_Main(srcColor, Shaders::Texture_Heap[u_PC.MainPassTextureID], fsInput.uv, u_PC.SrcTexelSize);
   
    // Gamma correction
    srcColor = Shaders::Linear2sRGB(float4(srcColor, 1.0f)).xyz;

    return float4(srcColor, 1.0f);
}