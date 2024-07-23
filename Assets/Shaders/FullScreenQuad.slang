// FullScreenQuad.slang

#include "../../Assets/Shaders/FullScreenQuadUtilShader.slang"

struct FSInput
{
    float2 uv;
};

[shader("fragment")]
float4 fragmentMain(FSInput fsInput: FSInput) : SV_Target
{
    return float4(0.5f, 0.25f, 1.0f, 1.0f);
}