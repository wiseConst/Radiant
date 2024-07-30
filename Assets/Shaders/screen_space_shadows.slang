// screen_space_shadows.slang

#include "../../Source/ShaderDefines.h"

struct PushConstantBlock
{
    const CameraData *CameraData;
    uint32_t DepthTextureID;
    uint32_t SSSTextureID;
};

[vk::push_constant] PushConstantBlock u_PC;

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 threadID : SV_DispatchThreadID)
{

}
