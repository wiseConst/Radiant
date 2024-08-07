// DepthPrePass.slang

#include "../../Source/ShaderDefines.h"

struct PushConstantBlock
{
    float4x4 ModelMatrix;
    const Shaders::CameraData *CameraData;
    const VertexPosition *VtxPositions;
};

struct VertexStageOutput
{
    float4 sv_position : SV_Position;
};

[vk::push_constant] PushConstantBlock u_PC;

[shader("vertex")]
VertexStageOutput vertexMain(uint32_t vertexID: SV_VertexID)
{
    const float4 worldPos = mul(u_PC->ModelMatrix, float4(u_PC->VtxPositions[vertexID].Position, 1.0f));

    VertexStageOutput output;
    output.sv_position = mul(u_PC.CameraData->ViewProjectionMatrix, worldPos);
    return output;
}