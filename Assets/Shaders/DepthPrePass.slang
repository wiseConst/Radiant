// DepthPrePass.slang

#include "../../Source/ShaderDefines.h"

struct PushConstantBlock
{
    float3 scale;
    float3 translation;
    half4 orientation;
    const Shaders::CameraData *CameraData;
    const VertexPosition *VtxPositions;
};
[vk::push_constant] PushConstantBlock u_PC;

struct VertexStageOutput
{
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VertexStageOutput vertexMain(uint32_t vertexID: SV_VertexID)
{
    const float3 worldPos = Shaders::RotateByQuat(u_PC.VtxPositions[vertexID].Position * u_PC.scale, u_PC.orientation * 2.0f - 1.0f) + u_PC.translation;
    
    VertexStageOutput output;
    output.sv_position = mul(u_PC.CameraData->ViewProjectionMatrix, float4(worldPos, 1.0f));
    return output;
}