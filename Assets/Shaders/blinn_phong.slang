// blinn_phong.slang

#include "../../Source/ShaderDefines.h"

struct FragmentStageInput
{ 
    float3 Normal;
    float4 Color;
};

struct VertexStageOutput
{
    FragmentStageInput FSInput : FragmentStageInput;
    float4 sv_position : SV_Position;
};

struct PushConstantBlock
{
    float4x4 ViewProjectionMatrix;
    const VertexPosition *VtxPositions;
    const VertexAttribute *VtxAttributes;
};

[vk::push_constant] PushConstantBlock u_PC;

[shader("vertex")]
VertexStageOutput vertexMain(uint32_t vertexID: SV_VertexID)
{
    VertexStageOutput output;

    output.FSInput.Color = u_PC.VtxAttributes[vertexID].Color;
    output.FSInput.Normal = u_PC.VtxAttributes[vertexID].Normal;
    output.sv_position = mul(u_PC.ViewProjectionMatrix, float4(u_PC.VtxPositions[vertexID].Position, 1.0)); 
    return output;
}

[shader("fragment")]
float4 fragmentMain(FragmentStageInput fsInput: FragmentStageInput) : SV_Target
{
    const float3 N = normalize(fsInput.Normal);
    const float3 L = -normalize(float3(0.5f, 5.0f, 6.0f));
    const float NdotL = max(dot(L, N), 0.f);

    return float4(fsInput.Color.xyz * NdotL, fsInput.Color.w);
}