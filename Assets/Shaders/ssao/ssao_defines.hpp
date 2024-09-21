// csm_defines.hpp

#ifdef __cplusplus
#pragma once

namespace Radiant
{

#endif

    namespace Shaders
    {

#define SSAO_WG_SIZE_X 16
#define SSAO_WG_SIZE_Y 16
#define USE_THREAD_GROUP_TILING_X 0

#ifndef __cplusplus

        static constexpr uint2 g_SSAO_WG_SIZE = uint2(SSAO_WG_SIZE_X, SSAO_WG_SIZE_Y);

        // Precalculated hemisphere kernel(TBN space)
        static const uint8_t KERNEL_SIZE          = 32;
        static const float3 g_Kernel[KERNEL_SIZE] = {
            float3(-0.668154f, -0.084296f, 0.219458f), float3(-0.092521f, 0.141327f, 0.505343f),  float3(-0.041960f, 0.700333f, 0.365754f),
            float3(0.722389f, -0.015338f, 0.084357f),  float3(-0.815016f, 0.253065f, 0.465702f),  float3(0.018993f, -0.397084f, 0.136878f),
            float3(0.617953f, -0.234334f, 0.513754f),  float3(-0.281008f, -0.697906f, 0.240010f), float3(0.303332f, -0.443484f, 0.588136f),
            float3(-0.477513f, 0.559972f, 0.310942f),  float3(0.307240f, 0.076276f, 0.324207f),   float3(-0.404343f, -0.615461f, 0.098425f),
            float3(0.152483f, -0.326314f, 0.399277f),  float3(0.435708f, 0.630501f, 0.169620f),   float3(0.878907f, 0.179609f, 0.266964f),
            float3(-0.049752f, -0.232228f, 0.264012f), float3(0.537254f, -0.047783f, 0.693834f),  float3(0.001000f, 0.177300f, 0.096643f),
            float3(0.626400f, 0.524401f, 0.492467f),   float3(-0.708714f, -0.223893f, 0.182458f), float3(-0.106760f, 0.020965f, 0.451976f),
            float3(-0.285181f, -0.388014f, 0.241756f), float3(0.241154f, -0.174978f, 0.574671f),  float3(-0.405747f, 0.080275f, 0.055816f),
            float3(0.079375f, 0.289697f, 0.348373f),   float3(0.298047f, -0.309351f, 0.114787f),  float3(-0.616434f, -0.117369f, 0.475924f),
            float3(-0.035249f, 0.134591f, 0.840251f),  float3(0.175849f, 0.971033f, 0.211778f),   float3(0.024805f, 0.348056f, 0.240006f),
            float3(-0.267123f, 0.204885f, 0.688595f),  float3(-0.077639f, -0.753205f, 0.070938f)};

        // Samples from crysis's 4x4 rotation texture
        static const uint8_t ROT_SIZE                   = 16;
        static const float3 g_RotationVectors[ROT_SIZE] = {
            float3(0.42745, 0.35686, 0.97255), float3(0.26667, 0.28235, 0.67843), float3(0.26275, 0.38431, 0.35294),
            float3(0.35294, 0.60, 0.17647),    float3(0.10588, 0.62745, 0.63922), float3(0.24314, 0.74118, 0.67059),
            float3(0.19216, 0.61961, 0.62882), float3(0.3451, 0.54902, 0.4),      float3(0.4902, 0.60392, 0.29804),
            float3(0.74902, 0.61176, 0.56863), float3(0.62745, 0.54118, 0.89412), float3(0.56078, 0.46275, 0.51373),
            float3(0.96471, 0.06275, 0.27059), float3(0.95686, 0.20784, 0.52157), float3(0.75686, 0.10588, 0.81961),
            float3(0.44706, 0.08627, 0.36471)};
        static constexpr uint32_t g_SampleCount = 11;
        static constexpr float g_SampleCountInv = 1.0f / g_SampleCount;

        static const float g_SampleRadius   = 0.25f;
        static const float g_SampleBias     = 0.025f;
        static const uint8_t g_SSAOStrength = 2;

#endif

    }  // namespace Shaders

#ifdef __cplusplus
}
#endif
