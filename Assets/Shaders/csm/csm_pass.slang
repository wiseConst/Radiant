// csm_pass.slang

#include <../../../Source/ShaderDefines.hpp>
#include <csm_defines.hpp>

struct PushConstantBlock
{
    float3 scale;
    float3 translation;
    float4 orientation;
    uint cascadeIndex;
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
    return VSOutput(mul(u_PC.CSMData.ViewProjectionMatrix[u_PC.cascadeIndex], float4(worldPos, 1.0f)));
}

[earlydepthstencil]
[shader("fragment")]
float4 fragmentMain(): SV_Target
{
    return float4(1, 0, 0, 1);
}
