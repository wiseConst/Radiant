// generate_irradiance_cube.slang

#include <../../../Source/ShaderDefines.hpp>
#include <cubemap.slang>

// NOTE: I think shader can be optimized through compute shaders
// if we create 2 shared float2 arrays, so we precompute those heavy funcs
// for the whole warp and then reuse them.
// groupshared float2 gs_Phi[(sampleDelta + 2.0f * Shaders::s_PI - 1) / sampleDelta]
// groupshared float2 gs_Theta[(sampleDelta + 0.5f * Shaders::s_PI - 1) / sampleDelta]
// For sampleDelta = 0.025f it results in total 2520 bytes: 
// (phi is 252 element length = 252 * sizeof(float2) = 252 * 8 bytes = 2016 bytes)
// (theta is 63 element length = 63 * sizeof(float2) = 63 * 8 bytes = 504 bytes)
// nvm, almost everything can be precomputed on CPU.

static const float sampleDeltaPhi = 0.034f;
static const float sampleDeltaTheta = 0.024f;
static const uint s_SampleCount = uint((2.0f * Shaders::s_PI / sampleDeltaPhi) * (0.5f * Shaders::s_PI / sampleDeltaTheta));
static const float s_InvSampleCount = 1.0f / s_SampleCount;

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput : VSOutput) : SV_Target
{
    /* 
    const float3 N = normalize(fsInput.LocalPos);
    float3 irradiance = 0.f;

    float3 B = float3(0.0f, 1.0f, 0.0f);
    const float3 T = normalize(cross(B, N));
    B = cross(N, T);

    float samplesNum = 0.f;
    for (float phi = 0.0f; phi < 2.0f * Shaders::s_PI; phi += sampleDeltaPhi) // around X
    {
        const float cosPhi = cos(phi);
        const float sinPhi = sin(phi);
        for (float theta = 0.0f; theta < 0.5f * Shaders::s_PI; theta += sampleDeltaTheta) // around Y
        {
            const float sinTheta = sin(theta);
            const float cosTheta = cos(theta);
            // Spherical to cartesian (in tangent space).
            const float3 tangentSample = float3(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
            // Tangent space to world.
            const float3 sampleVec = normalize(tangentSample.x * T + tangentSample.y * B + tangentSample.z * N);

            irradiance += Shaders::Texture_Cube_Heap[u_PC.SrcTextureID].Sample(sampleVec).rgb * cosTheta * sinTheta;
            samplesNum += 1.0f;
        }
    }
    irradiance *= 1.0f / samplesNum; // there was PI in numerator but it gets removed in main pass
    */

    // https://www.reddit.com/r/opengl/comments/l0h5fy/rendering_the_irradiance_map_for_physically_based/
    // Reusing importance sampling from prefiltering, to mitigate bright dots.
    const float3 N = normalize(fsInput.LocalPos);
    float3 irradiance = 0.f;

    float3 B = float3(0.0f, 1.0f, 0.0f);
    const float3 T = normalize(cross(B, N));
    B = cross(N, T);

    const float cubemapResRcp = u_PC.Data1;

    float totalWeight = 0.0f;
    for (uint i = 0u; i < s_SampleCount; ++i) {
        const float2 Xi = Shaders::Hammersley2D(i, s_InvSampleCount);
        const float3 H = Shaders::ImportanceSampleGGX(Xi, N, 1.0f);

        // NdotH is equal to cos(theta)
        const float NdotH = saturate(dot(N, H));

        // With roughness == 1 in the distribution function we get 1/pi
        const float D = Shaders::s_RcpPI;
        const float pdf = (D * NdotH / (4.0)) + Shaders::s_KINDA_SMALL_NUMBER;

        // Solid angle of current sample
        const float omegaS = s_InvSampleCount * 1.0f / pdf;
        // Solid angle covered by 1 pixel with 6 faces that are EnvMapSize X EnvMapSize
        const float omegaP = 4.0 * Shaders::s_PI * cubemapResRcp * cubemapResRcp / 6.0f;
        // Biased mip level for better result
        const float mipBias = 1.0f;
        const float mipLevel = max(0.5f * log2(omegaS / omegaP) + mipBias, 0.0f);

        irradiance += Shaders::Texture_Cube_Heap[u_PC.SrcTextureID].SampleLevel(H, mipLevel).rgb * NdotH;
        totalWeight += NdotH;
    }
    irradiance *= 1.0f / max(totalWeight, Shaders::s_KINDA_SMALL_NUMBER);

    return float4(irradiance, 1.0f);
}
