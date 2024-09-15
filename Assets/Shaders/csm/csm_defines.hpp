// csm_defines.hpp

#ifdef __cplusplus
#pragma once

namespace Radiant
{

    using float4x4 = glm::mat4;

#endif

    namespace Shaders
    {

#define SHADOW_MAP_CASCADE_SIZE 1024
#define SHADOW_MAP_CASCADE_COUNT 4
        struct CascadedShadowMapsData
        {
            float4x4 ViewProjectionMatrix[SHADOW_MAP_CASCADE_COUNT];
        };

    }  // namespace Shaders

#ifdef __cplusplus
}
#endif
