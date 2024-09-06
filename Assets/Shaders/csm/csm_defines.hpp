// csm_defines.hpp

#ifdef __cplusplus
#pragma once

namespace Radiant
{

using float4x4 = glm::mat4;

#endif

namespace Shaders
{

    #define CSM_CASCADE_COUNT 4
    struct CSMCascadeData
    {
        float4x4 ViewProjectionMatrix;
    };


}

#ifdef __cplusplus
}
#endif