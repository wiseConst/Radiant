// ssao_box_blur.slang

#include <../../../Source/ShaderDefines.hpp>
#include <../FullScreenQuad.slang>
#include <ssao_defines.hpp>

struct PushConstantBlock
{
    uint SSAOTextureID;
    float2 SrcTexelSize; // rcp(SSAOTextureResolution.xy)
};
[vk::push_constant] PushConstantBlock u_PC;

[shader("fragment")]
float fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    float ao = 0.f;
    [unroll]
    for (int x = -SSAO_BOX_BLUR_SIZE; x <= SSAO_BOX_BLUR_SIZE; ++x)
    {
        for (int y = -SSAO_BOX_BLUR_SIZE; y <= SSAO_BOX_BLUR_SIZE; ++y)
        {
            const float2 offsetUV = float2(x, y) * u_PC.SrcTexelSize;
            ao += Shaders::Texture_Heap[u_PC.SSAOTextureID].Sample(fsInput.uv + offsetUV).r;
        }
    }
    ao *= Shaders::g_TotalSampleCountInv;
    return ao;
}