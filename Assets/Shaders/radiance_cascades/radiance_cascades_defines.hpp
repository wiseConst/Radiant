#pragma once

#ifdef __cplusplus
namespace Radiant
{

    using float2 = glm::vec2;
    using float3 = glm::vec3;

#endif

    struct Point2D
    {
        float2 Position;
        float3 Color;
        float Radius;
    };

#ifndef __cplusplus

    namespace Shaders
    {

        // uint32_t GetRayIndex(const float2 pixelCoord, const uint2 probeDimensions)
        //{
        //     const uint2 probeCoords = pixelCoord % probeDimensions;
        //     return probeCoords.y * probeDimensions.x + probeCoords.x;
        // }

        // float GetRayAngle(const uint32_t rayIndex, const uint2 probeDimensions)
        //{
        //     const uint32_t rayCount = probeDimensions.x * probeDimensions.y;
        //     return rayIndex / rayCount * Shaders::s_PI * 2;
        // }

    }  // namespace Shaders

#endif  // !__cplusplus

#ifdef __cplusplus
}

#endif
