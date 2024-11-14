// csm_pass.slang

#include <../../../Source/ShaderDefines.hpp>
#include <csm_defines.hpp>

struct PushConstantBlock
{
    float3 scale;
    float3 translation;
    float4 orientation;
    const Shaders::CascadedShadowMapsData *CSMData;
    const VertexPosition *VtxPositions;
};
[vk::push_constant] PushConstantBlock u_PC;

struct VSOutput
{
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VSOutput vertexMain(uint32_t vertexID: SV_VertexID)
{
    const float3 worldPos = Shaders::RotateByQuat(u_PC.VtxPositions[vertexID].Position * u_PC.scale, u_PC.orientation) + u_PC.translation;
    return VSOutput(float4(worldPos, 1.0f));
}

struct GSOutput
{
    float4 sv_position : SV_Position;
    uint sv_layer_index : SV_RenderTargetArrayIndex;
}

#define CSM_GS_OUT_VERTEX_COUNT 3

[maxvertexcount(CSM_GS_OUT_VERTEX_COUNT)]
[instance(SHADOW_MAP_CASCADE_COUNT)]
[shader("geometry")]
void geometryMain(triangle VSOutput input[CSM_GS_OUT_VERTEX_COUNT], 
                    inout TriangleStream<GSOutput> triStream, 
                    const uint invocationID : SV_GSInstanceID)
{
    [unroll(CSM_GS_OUT_VERTEX_COUNT)]
    for (uint i = 0; i < CSM_GS_OUT_VERTEX_COUNT; ++i)
    {
        GSOutput output;
        output.sv_position = mul(u_PC.CSMData.ViewProjectionMatrix[invocationID], input[i].sv_position);
        output.sv_layer_index = invocationID;
        triStream.Append(output); 
    }

    // No need to call RestartStrip() since we're not using strips. Triangle_List happening here.
    // triStream.RestartStrip();
}

[earlydepthstencil]
[shader("fragment")]
float4 fragmentMain(): SV_Target
{
    return float4(1, 1, 1, 1);
}
