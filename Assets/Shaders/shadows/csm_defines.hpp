// csm_defines.hpp

#ifdef __cplusplus
#pragma once

namespace Radiant
{

    using float4x4 = glm::mat4;
    using uint2    = glm::uvec2;
    using uint     = u32;

#endif

    namespace Shaders
    {

#define SHADOW_MAP_CASCADE_SIZE 2048
#define SHADOW_MAP_CASCADE_COUNT 4

        struct CascadedShadowMapsData
        {
            float4x4 ViewProjectionMatrix[SHADOW_MAP_CASCADE_COUNT];
            float CascadeSplits[SHADOW_MAP_CASCADE_COUNT];  // From zNear up to latest cascade split, zFar isn't taken into account here.
            float2 MinMaxCascadeDistance;
        };

#define DEPTH_REDUCTION_WG_SIZE_X 16
#define DEPTH_REDUCTION_WG_SIZE_Y 16

        struct DepthBounds
        {
            uint2 MinMaxZ;  // encoded in hlsl as asuint(), decode as asfloat()
        };

#define SHADOWS_SETUP_WG_SIZE_X 32

#ifndef __cplusplus

        // PCSS(Percentage-Closer Soft Shadows)
#define PCF_SHADOWS_ENABLE 1
#define PCF_RADIUS 4
#define PCF_SAMPLE_COUNT (PCF_RADIUS * PCF_RADIUS)

        float PCF_Filter(Sampler2DArray shadowMap2DArray, const uint cascadeIndex, const float2 uv, const float zReceiver, const float bias,
                         const float2 filterRadiusUV)
        {
            float shadow = 0.0f;
            for (uint i = 0; i < PCF_SAMPLE_COUNT; ++i)
            {
                const float sampledDepth = shadowMap2DArray.Sample(float3(uv + GetPoissonSample(i) * filterRadiusUV, cascadeIndex)).r;
                const float fOccluded    = (zReceiver + bias) < sampledDepth ? 1.0 : 0.0;
                shadow += fOccluded;
            }
            shadow /= (float)PCF_SAMPLE_COUNT;
            return shadow;
        }

        // Specifies how many samples are used for the blocker search step.
        // Multiple samples are required to avoid holes
        // in the penumbra due to missing blockers.
#define PCSS_BLOCKER_SEARCH_SAMPLES_COUNT 16u

#define PCSS_ZNEAR 0.01f

        // reversed z: penumbraSize = 1.0f - penumbraSize or how???
        float EstimatePenumbraSize(const float zReceiver, const float zBlocker)  // Parallel plane estimation
        {
            return (zReceiver - zBlocker) / zBlocker;
        }

        void FindBlocker(out float avgBlockerDepth, out float blockerCount, const float2 uv, const float zReceiver, const float lightSizeUV,
                         Sampler2DArray shadowMap2DArray, const uint cascadeIndex)
        {
            // This uses similar triangles to compute what
            // area of the shadow map we should search.
            const float searchWidth = lightSizeUV * (zReceiver - PCSS_ZNEAR) / zReceiver;

            float blockerSum = 0.0f;
            blockerCount     = 0.0f;
            for (uint i = 0; i < PCSS_BLOCKER_SEARCH_SAMPLES_COUNT; ++i)
            {
                const float sampledDepth = shadowMap2DArray.Sample(float3(uv + GetPoissonSample(i) * lightSizeUV, cascadeIndex)).r;
                if (sampledDepth < zReceiver)
                {
                    blockerSum += sampledDepth;
                    blockerCount += 1.0f;
                }
            }

            avgBlockerDepth = blockerSum / blockerCount;
        }

        float PCSS(Sampler2DArray shadowMap2DArray, const uint cascadeIndex, const float lightSizeUV, const float3 projCoords)
        {
            const float2 uv       = projCoords.xy;
            const float zReceiver = projCoords.z;  // Assumed to be view-space Z.

            // STEP 1: Blocker Search
            float avgBlockerDepth = 0.0f;
            float blockerCount    = 0.0f;
            FindBlocker(avgBlockerDepth, blockerCount, uv, zReceiver, lightSizeUV, shadowMap2DArray, cascadeIndex);

            // There are no occluders so early out (this saves filtering)
            if (blockerCount < 1.0f) return 0.0f;

            // STEP 2: Estimate Penumbra Size
            const float penumbraSize   = EstimatePenumbraSize(zReceiver, avgBlockerDepth);
            const float filterRadiusUV = penumbraSize * lightSizeUV * PCSS_ZNEAR / zReceiver;

            // STEP 3: Filtering
            return PCF_Filter(shadowMap2DArray, cascadeIndex, uv, zReceiver, /* bias */ 0.0f, filterRadiusUV);
        }

#endif

    }  // namespace Shaders

#ifdef __cplusplus
}
#endif
