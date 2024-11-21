// csm_defines.hpp

#ifdef __cplusplus
#pragma once

namespace Radiant
{

#endif

    namespace Shaders
    {

#define SSAO_BOX_BLUR_SIZE 2
        static const int32_t g_SamplesInOneDimension = 2 * SSAO_BOX_BLUR_SIZE + 1;
        static const float g_TotalSampleCountInv     = 1.0f / (g_SamplesInOneDimension * g_SamplesInOneDimension);

#define SSAO_WG_SIZE_X 16
#define SSAO_WG_SIZE_Y 16
#define USE_THREAD_GROUP_TILING_X 0

#ifndef __cplusplus

        static constexpr uint2 g_SSAO_WG_SIZE = uint2(SSAO_WG_SIZE_X, SSAO_WG_SIZE_Y);

        static constexpr uint32_t g_SampleCount = 8;
        static constexpr float g_SampleCountInv = 1.0f / g_SampleCount;

        static const float g_SampleRadius   = 0.25f;
        static const float g_SampleBias     = 0.025f;
        static const uint8_t g_SSAOStrength = 1;

#endif

    }  // namespace Shaders

#ifdef __cplusplus
}
#endif
