// csm_defines.hpp

#ifdef __cplusplus
#pragma once

namespace Radiant
{

    using float4x4 = glm::mat4;

#endif

    namespace Shaders
    {

#define SHADOW_MAP_DIMENSIONS 2048
#define SHADOW_MAP_CASCADE_COUNT 4
        struct CSMCascadeData
        {
            float4x4 ViewProjectionMatrix;
        };

    }  // namespace Shaders

#ifdef __cplusplus
}
#endif
