// ssao_box_blur.slang

#include "../../Source/ShaderDefines.h"
#include "../../Assets/Shaders/FullScreenQuad.slang"

struct PushConstantBlock
{
    uint SSAOTextureID;
};
[vk::push_constant] PushConstantBlock u_PC;

static const int32_t g_BlurSize = 2;

[shader("fragment")]
float fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    uint width = 1, height = 1;
    Shaders::Texture_Heap[u_PC.SSAOTextureID].GetDimensions(width, height);
    const float2 rcpTexDim = 1.0f / float2(width, height);

    float ao = 0.f;
    uint accumulatedCount = 0;
    [unroll]
    for (int x = -g_BlurSize; x <= g_BlurSize; ++x)
    {
        for (int y = -g_BlurSize; y <= g_BlurSize; ++y)
        {
            const float2 offsetUV = rcpTexDim * float2(x, y);
            ao += Shaders::Texture_Heap[u_PC.SSAOTextureID].Sample(fsInput.uv + offsetUV).r;
            ++accumulatedCount;
        }
    }
    ao /= (float)accumulatedCount;
    return ao;
}