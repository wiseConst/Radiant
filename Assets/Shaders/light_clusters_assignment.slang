// cull_light_clusters.slang

#include "../../Source/ShaderDefines.h"

//struct PushConstantBlock
//{
//    const Shaders::CameraData *CameraData;
//    uint32_t DepthTextureID;
//};
//[vk::push_constant] PushConstantBlock u_PC;

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 gid : SV_DispatchThreadID)
{

}