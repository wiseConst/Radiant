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

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput : VSOutput) : SV_Target
{
    const float3 N = normalize(fsInput.LocalPos);
    float3 irradiance = 0.f;

    float3 B = float3(0.0f, 1.0f, 0.0f);
    const float3 T = normalize(cross(B, N));
    B = cross(N, T);

    static const float sampleDelta = 0.025f;
    float samplesNum = 0.f;
    for (float phi = 0.0f; phi < 2.0f * Shaders::s_PI; phi += sampleDelta) // around X
    {
        const float cosPhi = cos(phi);
        const float sinPhi = sin(phi);
        for (float theta = 0.0f; theta < 0.5f * Shaders::s_PI; theta += sampleDelta) // around Y
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

    irradiance *= Shaders::s_PI / samplesNum;
    return float4(irradiance, 1.0f);
}
