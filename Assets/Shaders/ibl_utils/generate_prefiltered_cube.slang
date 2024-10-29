// generate_prefiltered_cube.slang

#include <../../../Source/ShaderDefines.hpp>
#include <cubemap.slang>

// Source: https://placeholderart.wordpress.com/2015/07/28/implementation-notes-runtime-environment-map-filtering-for-image-based-lighting/

static constexpr uint s_SampleCount = 32u;
static constexpr float s_InvSampleCount = 1.0f / float(s_SampleCount);

// Realtime usage notes(precompute as much as possible and if needed move into compute):
// 1. Precompute hammersley sequence, since it doesn't change across different roughness/mips.
// 2. Precompute mip levels
// So you end up with:
// float fTotalWeight = 0.0f;
// const uint iNumSamples = 32;
// for (uint i = 0; i < iNumSamples; i++)
// {
//     // We can once again take advantage of the isotropic assumption and realize that the dot product between 
//     // two vectors in local space is the same as the dot product of those two vectors in some other space. 
//     // So ndoth = vdoth = dot((0, 0, 1), half) or ndoth = vdoth = half.z, therefore this can be precompute per sample now too.
//     float3 vLight = mul(vSampleDirections[i], mTangentToWorld);

//     float fNdotL = saturate(dot(vNormal, vLight));
//     if (fNdotL > 0.0f)
//     {
//         vPrefilteredColor += EnvironmentCubemapTexture.SampleLevel(LinearSamplerWrap, vLight, fSampleMipLevels[i]).rgb * fNdotL;
//         fTotalWeight += fNdotL;
//     }
// }
//
// Most optimized version:
// Once again leverage that we have pre-computed the light vector in local space and say that ndotl = dot((0, 0, 1), light) = light.z.
// Additionally, we can check if the weight of a sample is going to be 0 before we even upload it, this lets us just skip samples entirely 
// that will fail the check. At this point the inner loop just rotates the the local light vector into world space and does a 
// madd to accumulate the sample.
// for (uint i = 0; i < iNumSamples; i++)
// {
//     float3 vLight = mul(vSampleDirections[i], mTangentToWorld);
//     vPrefilteredColor += EnvironmentCubemapTexture.SampleLevel(LinearSamplerWrap, vLight, fSampleMipLevels[i]).rgb * fSampleWeights[i];
// }
// return vPrefilteredColor * fInvTotalWeight;


[shader("fragment")]
float4 fragmentMain(VSOutput fsInput : VSOutput) : SV_Target
{
    // Part of a split-sum approximation. Viewing angle is zero. Following the isotropic assumption made
    const float3 N = normalize(fsInput.LocalPos);
    const float3 R = N;
    const float3 V = R;

    const float roughness = u_PC.Data0;
    const float cubemapResRcp = u_PC.Data1;

    float totalWeight = 0.0f;
    float3 prefilteredColor = float3(0.0f);
    for (uint i = 0u; i < s_SampleCount; ++i)
    {
        const float2 Xi = Shaders::Hammersley2D(i, s_InvSampleCount);
        const float3 H = Shaders::ImportanceSampleGGX(Xi, N, roughness);
        const float3 L = reflect(-V, H);

        const float NdotL = saturate(dot(N, L));
        if (NdotL > 0.0f)
        {
            const float NdotH = saturate(dot(N, H));
            const float VdotH = saturate(dot(V, H));

            // Probability Distribution Function
            const float pdf = Shaders::EvaluateDistributionGGX(NdotH, roughness) * NdotH / (4.0f * VdotH) + Shaders::s_KINDA_SMALL_NUMBER;
            // Solid angle of current sample
            const float omegaS = s_InvSampleCount * 1.0f / pdf;
            // Solid angle covered by 1 pixel with 6 faces that are EnvMapSize X EnvMapSize
            const float omegaP = 4.0 * Shaders::s_PI * cubemapResRcp * cubemapResRcp / 6.0f;
            // Biased mip level for better result
            const float mipBias = 1.0f;
            const float mipLevel = max(0.5f * log2(omegaS / omegaP) + mipBias, 0.0f);

            prefilteredColor += Shaders::Texture_Cube_Heap[u_PC.SrcTextureID].SampleLevel(L, mipLevel).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    prefilteredColor /= max(totalWeight, Shaders::s_KINDA_SMALL_NUMBER);
    return float4(prefilteredColor, 1.0f);
}