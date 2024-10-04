// setup_csm.slang

#include "../../../Source/ShaderDefines.h"
#include <csm_defines.hpp>

// The idea is each thread is a cascade, 
// so we dispatch only one workgroup at all.
// [numthreads(SHADOW_MAP_CASCADE_COUNT, 1, 1)]
// groupshared float cascadeSplits[4]

struct PushConstantBlock
{
    const Shaders::CameraData *CameraData;
    const Shaders::DepthBounds *DepthBounds;
    Shaders::CascadedShadowMapsData *CSMData;
};

float4x4 glm_perspective()
{

}

float4x4 glm_lookAt()
{

}

[numthreads(1, 1, 1)]
[shader("compute")]
void computeMain()
{

    for (uint32_t cascadeIndex = 0; cascadeIndex < SHADOW_MAP_CASCADE_COUNT; ++cascadeIndex)
    {

    }
}
