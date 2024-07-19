// shaders.slang

#include "../../Source/ShaderDefines.h"

// Output of the vertex shader, and input to the fragment shader.
struct CoarseVertex
{
    float3 color;
};

struct VertexStageOutput
{
    CoarseVertex    coarseVertex    : CoarseVertex;
    float4          sv_position     : SV_Position;
};

static const float3 g_Vertices[3] = { float3(0.5, -0.5, -1.0), float3(0.0, 0.5, -1.0), float3(-0.5, -0.5, -1.0) };
static const float3 g_Colors[3] = { float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(0.0, 0.0, 1.0) };

struct PushConstantBlock {
    float4x4 ProjectionMatrix;
    float4x4 ViewMatrix;
};
[[vk::push_constant]] PushConstantBlock u_PC;

[shader("vertex")]
VertexStageOutput vertexMain(uint32_t vertexID : SV_VertexID)
{
    VertexStageOutput output;

    output.coarseVertex.color = g_Colors[vertexID];
    output.sv_position = mul(u_PC.ProjectionMatrix, mul(u_PC.ViewMatrix, float4(g_Vertices[vertexID], 1.0)));

    return output;
}


[shader("fragment")]
float4 fragmentMain(
    CoarseVertex coarseVertex : CoarseVertex) : SV_Target
{
    float3 color = coarseVertex.color;

    return float4(color, 1.0);
}