// FullScreenQuad.slang

#include "../../Assets/Shaders/FullScreenQuadUtilShader.slang"
#include "../../Source/ShaderDefines.h"

struct FSInput
{
    float2 uv;
};

struct TestPushConstantBlock
{
    uint32_t ImageIndex;
};

[vk::push_constant] TestPushConstantBlock u_PC;

[shader("fragment")]
float4 fragmentMain(FSInput fsInput: FSInput) : SV_Target
{
    return Shaders::Texture_Heap[u_PC.ImageIndex].SampleLevel(fsInput.uv, 0).rgba;
}