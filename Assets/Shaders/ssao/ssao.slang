// ssao.slang

#include "../../../Source/ShaderDefines.hpp"
#include "../common/FullScreenQuad.slang"
#include <ssao_defines.hpp>

struct PushConstantBlock
{
    const Shaders::CameraData *CameraData;
    uint32_t DepthTextureID;
};
[vk::push_constant] PushConstantBlock u_PC;


[shader("fragment")]
float fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    const uint2 rotationUV = uint2(fsInput.uv * u_PC.CameraData->FullResolution) % 4;
    const float3 noise = normalize(Shaders::GetRotationVector(rotationUV.y * 4 + rotationUV.x) * 2.0f - 1.0f);
    
    const float3 viewPos = Shaders::ScreenSpaceToView(fsInput.uv, Shaders::Texture_Heap[u_PC.DepthTextureID].Sample(fsInput.uv).r, u_PC.CameraData->InvProjectionMatrix).xyz;
    const float3 N = normalize(cross(ddx(viewPos), ddy(viewPos)));
    const float3 T = normalize(noise - N * dot(N, noise));
    const float3 B = cross(N, T);
    const float3x3 TBN = transpose(float3x3(T, B, N));

    float ao = 0.0f;
    [unroll]
    for (uint32_t i = 0; i < Shaders::g_SampleCount; ++i)
    {
        const float3 sample = viewPos + mul(TBN, Shaders::GetHemisphereSample8(i)) * Shaders::g_SampleRadius;

        float4 offsetUV = mul(u_PC.CameraData->ProjectionMatrix, float4(sample, 1.0f));
        offsetUV.xy = (offsetUV.xy / offsetUV.w) * 0.5f + 0.5f;

        const float sampledDepth = Shaders::ScreenSpaceDepthToView( Shaders::Texture_Heap[u_PC.DepthTextureID].Sample(offsetUV.xy).r, u_PC.CameraData->ProjectionMatrix);
        const float rangeCheck = smoothstep(0.0f, 1.0f, Shaders::g_SampleRadius / abs(viewPos.z - sampledDepth));
        ao += step(sample.z + Shaders::g_SampleBias, sampledDepth) * rangeCheck;
    }
    ao *= Shaders::g_SampleCountInv;
    return pow(ao, Shaders::g_SSAOStrength);
}
