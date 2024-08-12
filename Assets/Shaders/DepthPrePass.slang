// DepthPrePass.slang

#include "../../Source/ShaderDefines.h"

struct PushConstantBlock
{
    float3 scale;
    float3 translation; 
#if !RENDER_FORCE_IGPU
    half4 orientation; // NOTE: quaternions are in range[-1, 1], so on CPU we firstly convert them in range[0, 1] and then quantize into fp16.
#else
    float4 orientation;
#endif
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
#if !RENDER_FORCE_IGPU
    const float3 worldPos = Shaders::RotateByQuat(u_PC.VtxPositions[vertexID].Position * u_PC.scale, u_PC.orientation * 2.0f - 1.0f) + u_PC.translation;
#else
    const float3 worldPos = Shaders::RotateByQuat(u_PC.VtxPositions[vertexID].Position * u_PC.scale, u_PC.orientation) + u_PC.translation;
#endif

    VertexStageOutput output;
    output.sv_position = mul(u_PC.CameraData->ViewProjectionMatrix, float4(worldPos, 1.0f));
    return output;
}