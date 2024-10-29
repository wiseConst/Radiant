// generate_brdf_lut.slang

#include <../../../Source/ShaderDefines.hpp>
#include <../FullScreenQuad.slang>

static constexpr uint s_SampleCount = 1024u;
static constexpr float s_InvSampleCount = 1.0f / (float)s_SampleCount;

float2 IntegrateBRDF(const float inputNdotV, const float roughness)
{
    // Normal always points along z-axis for the 2D lookup
    const float3 N = float3(0.0, 0.0, 1.0);
    const float3 V = float3(sqrt(1.0f - inputNdotV * inputNdotV), 0.0f, inputNdotV);

    float2 lut = float2(0.0f);
    for (uint i = 0u; i < s_SampleCount; ++i)
    {
        const float2 Xi = Shaders::Hammersley2D(i, s_InvSampleCount);
        const float3 H = Shaders::ImportanceSampleGGX(Xi, N, roughness);
        
        const float3 L = normalize(reflect(-V, H));
        const float NdotL = saturate(dot(N, L));
        if (NdotL > 0.0)
        {
            const float NdotV = saturate(dot(N, V));
            const float VdotH = saturate(dot(V, H));
            const float NdotH = saturate(dot(H, N));

            const float G = Shaders::EvaluateGeometrySmith_IBL(NdotV, NdotL, roughness);
            const float G_Vis = (G * VdotH) / (NdotH * NdotV);
            const float Fc = pow(1.0 - VdotH, 5.0);

            lut += float2((1.0 - Fc), Fc) * G_Vis;
        }
    }
    return lut * s_InvSampleCount;
}

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    return float4(IntegrateBRDF(fsInput.uv.x, fsInput.uv.y), 0.0f, 1.0f);
}