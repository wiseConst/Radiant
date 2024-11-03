// hzb.slang

#include <../../../Source/ShaderDefines.hpp>
#include <aw2_defines.hpp>

struct PushConstantBlock
{
    uint SrcTextureID;
    uint DstTextureID;
    uint SamplerID;
    float2 SrcTextureSizeRcp;
};
[[vk::push_constant]] PushConstantBlock u_PC;

[numthreads(HZB_WG_SIZE, HZB_WG_SIZE, 1)]
[shader("compute")]
void computeMain(const uint3 DTid : SV_DispatchThreadID)
{
    // Sampler is set up to do min reduction, so this computes the minimum depth of a 2x2 texel quad
    const float2 uv = (float2(DTid.xy) + float2(0.5f)) * u_PC.SrcTextureSizeRcp;
    const float depth = Shaders::SampledImage_Heap[u_PC.SrcTextureID].SampleLevel(Shaders::Sampler_Heap[u_PC.SamplerID], uv, 0.0f).r;

    Shaders::RWImage2D_Heap_R32F[u_PC.DstTextureID][DTid.xy] = depth;
}