// csm.slang

#include "../../Source/ShaderDefines.h"


struct VSOutput
{
    float4 sv_position : SV_Position;
};

struct PushConstantBlock
{
    
};
[vk::push_constant] PushConstantBlock u_PC;

[shader("vertex")]
VSOutput vertexMain()
{
    VSOutput output;

    return output;
}