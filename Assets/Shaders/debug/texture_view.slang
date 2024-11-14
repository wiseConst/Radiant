// texture_view.slang

#include <../../../Source/ShaderDefines.hpp>

struct PushConstantBlock
{
    uint TextureID;
    uint LayerIndex;
    uint MipIndex;
    float4 MinMax;
};
[[vk::push_constant]] PushConstantBlock u_PC;

struct VSOutput
{
    float2 uv;
    float4 sv_position : SV_Position;
};

static const float2 g_TexCoords[4] = { float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f), float2(0.0f, 1.0f) };

[shader("vertex")]
VSOutput vertexMain(const uint vtxID : SV_VertexID)
{
    VSOutput output;
    output.uv = g_TexCoords[vtxID];
    output.sv_position.xy = (u_PC.MinMax.xy + g_TexCoords[vtxID] * (u_PC.MinMax.zw - u_PC.MinMax.xy)) * 2.0f - float2(1.0f);
    output.sv_position.zw = 1.0f;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    if (u_PC.LayerIndex != 0)
        return float4(Shaders::Texture_Array_Heap[u_PC.TextureID].SampleLevel(float3(fsInput.uv, u_PC.LayerIndex), u_PC.MipIndex).rgb, 1.0f);
    
    return float4(Shaders::Texture_Heap[u_PC.TextureID].SampleLevel(fsInput.uv, u_PC.MipIndex).rgb, 1.0f);
}