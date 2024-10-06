// generate_irradiance_cube.slang

#include <../../../Source/ShaderDefines.hpp>
#include <cubemap.slang>

static constexpr float g_DeltaPhi = 2.0f * Shaders::s_PI / 180.0f;
static constexpr float g_DeltaTheta = 0.5f * Shaders::s_PI / 64.0f;

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput : VSOutput) : SV_Target
{
    const float3 N = normalize(fsInput.LocalPos);
    float3 irradiance = 0.f;

    float3 up = float3(0.0f, 1.0f, 0.0f);
    const float3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    float samplesNum = 0.0f;
    for (float phi = 0.0f; phi < 2.0f * Shaders::s_PI; phi += g_DeltaPhi)
    {
        for (float theta = 0.0f; theta < 0.5f * Shaders::s_PI; theta += g_DeltaTheta)
        {
            // spherical to cartesian (in tangent space)
            const float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            // tangent space to world
            const float3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            irradiance += Shaders::Texture_Cube_Heap[u_PC.SrcTextureID].Sample(sampleVec).rgb * cos(theta) * sin(theta);
            samplesNum += 1.0f;
        }
    }
    irradiance *= Shaders::s_PI  / samplesNum;

    return float4(irradiance, 1.0f);
}
