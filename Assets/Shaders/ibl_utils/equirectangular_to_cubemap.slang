// equirectangular_to_cubemap.slang

#include <../../../Source/ShaderDefines.hpp>
#include <cubemap.slang>

// https://stackoverflow.com/questions/48494389/how-does-this-code-sample-from-a-spherical-map
static constexpr float2 s_InvAtan = float2(0.1591, 0.3183); // inv 2PI, inv PI
float2 SampleSphericalMap(const float3 v)
{
    float2 uv = float2(atan2(v.z, v.x), asin(v.y) * -1);
    uv *= s_InvAtan; // transform into [-0.5, 0.5] range
    return uv + 0.5; // final transform to uv space
}

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    const float2 uv = SampleSphericalMap(normalize(fsInput.LocalPos));
    float3 outColor = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(uv).rgb;

    return float4(outColor, 1.0f);
}