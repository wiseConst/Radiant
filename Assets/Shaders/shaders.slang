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
}
[[vk::push_constant]] PushConstantBlock u_PC;

[shader("vertex")]
VertexStageOutput vertexMain(uint32_t vertexID : SV_VertexID)
{
    VertexStageOutput output;

    output.coarseVertex.color = g_Colors[vertexID];
    output.sv_position =
        // float4(g_Vertices[vertexID], 1.0);
        mul(mul(float4(g_Vertices[vertexID], 1.0), u_PC.ViewMatrix), u_PC.ProjectionMatrix);


        //  mul(u_PC.ProjectionMatrix, float4(g_Vertices[vertexID], 1.0)); // idk why but this produces g_Vertices * projMatr in renderdoc!!
    //    mul(float4(g_Vertices[vertexID], 1.0), u_PC.ProjectionMatrix); // but this produces  projMatr * g_Vertices in renderdoc
        
    return output;
}


[shader("fragment")]
float4 fragmentMain(
    CoarseVertex coarseVertex : CoarseVertex) : SV_Target
{
    float3 color = coarseVertex.color;

    return float4(color, 1.0);
}