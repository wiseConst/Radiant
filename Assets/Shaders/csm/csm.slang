// csm.slang

#include "../../../Source/ShaderDefines.h"
#include <csm_defines.hpp>

struct VSOutput
{
    float4 sv_position : SV_Position;
};

struct PushConstantBlock
{
    float3 scale;
    float3 translation;
    float4 orientation;
    const VertexPosition *VtxPositions;
};
[vk::push_constant] PushConstantBlock u_PC;

[shader("vertex")]
VSOutput vertexMain()
{
    VSOutput output;

    return output;
}