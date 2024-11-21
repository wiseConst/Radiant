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
#define SHADOW_MAP_TEXEL_SIZE (1.0f / (float)SHADOW_MAP_CASCADE_SIZE)

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
#define SHADOWS_ZNEAR 0.1f
#define SHADOWS_ZFAR_OFFSET 7.5f

#define SHADOWS_PCSS_ENABLE 1
#define SHADOWS_COMPUTE_RECEIVER_PLANE_DEPTH_BIAS 1

#ifndef __cplusplus

        // http://the-witness.net/news/2013/09/shadow-mapping-summary-part-1/
        float2 GetShadowOffsets(const float NdotL)
        {
            const float cos_alpha      = saturate(NdotL);
            const float offset_scale_N = sqrt(1.0 - cos_alpha * cos_alpha);  // sin(acos(L·N))
            const float offset_scale_L = offset_scale_N / cos_alpha;         // tan(acos(L·N))
            return float2(offset_scale_N, min(2.0, offset_scale_L));
        }

        float2 ComputeReceiverPlaneDepthBias(float3 texCoordDX, float3 texCoordDY)
        {
            float2 biasUV;
            biasUV.x = texCoordDY.y * texCoordDX.z - texCoordDX.y * texCoordDY.z;
            biasUV.y = texCoordDX.x * texCoordDY.z - texCoordDY.x * texCoordDX.z;
            biasUV *= 1.0f / ((texCoordDX.x * texCoordDY.y) - (texCoordDX.y * texCoordDY.x));
            return biasUV;
        }

        // https://github.com/diharaw/area-light-shadows/blob/master/src/shader/mesh_fs.glsl
        // https://www.gamedev.net/articles/programming/graphics/contact-hardening-soft-shadows-made-fast-r4906/
        // https://http.download.nvidia.com/developer/presentations/2005/SIGGRAPH/Percentage_Closer_Soft_Shadows.pdf
        // https://developer.download.nvidia.com/shaderlibrary/docs/shadow_PCSS.pdf
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
                const float sampledDepth =
                    shadowMap2DArray.Sample(float3(uv + GetVogelDisk16(i) * filterRadiusUV, cascadeIndex)).r;
                shadow += step(zReceiver + bias, sampledDepth);
            }
            shadow *= rcp(PCF_SAMPLE_COUNT);
            return shadow;
        }

        // Specifies how many samples are used for the blocker search step.
        // Multiple samples are required to avoid holes in the penumbra due to missing blockers.
#define PCSS_BLOCKER_SEARCH_SAMPLES_COUNT 16u
#define PCSS_ZNEAR 0.001f

        float EstimatePenumbraSize(const float zReceiver, const float zBlocker)  // Parallel plane estimation
        {
            return (zReceiver - zBlocker) / zBlocker;
        }

        float EstimateSearchWidth(const float zReceiver)
        {
            return (zReceiver - PCSS_ZNEAR) / zReceiver;
        }

        // NOTE: There are 2 ways: either specify lightSize as lightSizeUV in range[0, 1],
        // or specify as default lightSize in any range, but multiply by SHADOW_MAP_TEXEL_SIZE so you stay in shadow_map_texels.
        // abs() cuz of reversed Z

        // Returns average blocker depth in the search region, as well as the number of found blockers.
        // Blockers are defined as shadow-map samples between the surface point and the light.
        void FindBlocker(out float avgBlockerDepth, out float blockerCount, const float2 uv, const float zReceiver, const float lightSize,
                         Sampler2DArray shadowMap2DArray, const uint cascadeIndex, const float bias)
        {
            // This uses similar triangles to compute what area of the shadow map we should search.
            const float searchWidth = lightSize * abs(EstimateSearchWidth(zReceiver));

            avgBlockerDepth = 0.0f;
            blockerCount    = 0.0f;
            for (uint i = 0; i < PCSS_BLOCKER_SEARCH_SAMPLES_COUNT; ++i)
            {
                const float2 uvOffset    = GetVogelDisk16(i) * searchWidth * SHADOW_MAP_TEXEL_SIZE;
                const float sampledDepth = shadowMap2DArray.Sample(float3(uv + uvOffset, cascadeIndex)).r;
                const float occluded     = step(zReceiver + bias, sampledDepth);

                avgBlockerDepth += sampledDepth * occluded;
                blockerCount += occluded;
            }

            avgBlockerDepth /= blockerCount;
        }

        float PCSS(Sampler2DArray shadowMap2DArray, const uint cascadeIndex, const float lightSize, const float3 projCoords, float bias)
        {
            const float2 uv       = projCoords.xy;
            const float zReceiver = projCoords.z;

#if SHADOWS_COMPUTE_RECEIVER_PLANE_DEPTH_BIAS
            const float2 receiverPlaneDepthBias = ComputeReceiverPlaneDepthBias(ddx_fine(projCoords), ddy_fine(projCoords));

            // Static depth biasing to make up for incorrect fractional sampling on the shadow map grid.
            const float fractionalSamplingError = dot(float2(SHADOW_MAP_TEXEL_SIZE), abs(receiverPlaneDepthBias));
            bias += min(fractionalSamplingError, 0.01f);
#endif

            // STEP 1: Blocker Search
            float avgBlockerDepth = 0.0f;
            float blockerCount    = 0.0f;
            FindBlocker(avgBlockerDepth, blockerCount, uv, zReceiver, lightSize, shadowMap2DArray, cascadeIndex, bias);

            // There are no occluders so early out (this saves filtering)
            if (blockerCount < 1.0f) return 0.0f;

            // STEP 2: Estimate Penumbra Size
            float penumbraSize = lightSize * EstimatePenumbraSize(zReceiver, avgBlockerDepth);
            penumbraSize *= penumbraSize;  // Squaring removes need of abs(EstimatePenumbraSize), as well as gives more softness.
            const float filterRadiusUV = penumbraSize * SHADOW_MAP_TEXEL_SIZE;

            // STEP 3: Filtering
            return PCF_Filter(shadowMap2DArray, cascadeIndex, uv, zReceiver, bias, filterRadiusUV);
        }

        float SampleShadowMapArray(Sampler2DArray shadowMap2DArray, const uint cascadeIndex, const Shaders::CascadedShadowMapsData* csmData,
                                   const float3 fragPosWS, const float NdotL, const float lightSize)
        {
            const float4 fragPosLS = mul(csmData.ViewProjectionMatrix[cascadeIndex], float4(fragPosWS, 1.0f));
            float3 projCoords      = fragPosLS.xyz / fragPosLS.w;
            projCoords.xy          = projCoords.xy * 0.5 + 0.5;

            float shadow = 0.0f;
            if (Shaders::is_saturated(projCoords))
            {
                const float bias         = max(0.0005f, 0.005f * (1.0f - NdotL)) / csmData.CascadeSplits[cascadeIndex];
                const float currentDepth = projCoords.z;  // get depth of current fragment from light's perspective

#if SHADOWS_PCSS_ENABLE
                shadow = Shaders::PCSS(shadowMap2DArray, cascadeIndex, lightSize, projCoords, bias);
#elif PCF_SHADOWS_ENABLE
                shadow = Shaders::PCF_Filter(shadowMap2DArray, cascadeIndex, projCoords.xy, currentDepth, bias, SHADOW_MAP_TEXEL_SIZE);
#else
                const float sampledDepth = shadowMap2DArray.Sample(float3(projCoords.xy, cascadeIndex)).r;
                shadow += step(currentDepth + bias, sampledDepth);
#endif
            }
            return shadow;
        }

#endif

    }  // namespace Shaders

#ifdef __cplusplus
}
#endif
