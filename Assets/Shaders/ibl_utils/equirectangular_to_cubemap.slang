// equirectangular_to_cubemap.slang

#include <../../../Source/ShaderDefines.hpp>
#include <cubemap.slang>

// https://stackoverflow.com/questions/48494389/how-does-this-code-sample-from-a-spherical-map
float2 SampleSphericalMap(const float3 v)
{
    static constexpr float2 s_InvAtan = float2(0.1591f, 0.3183f); // inv 2PI, inv PI
    float2 uv = float2(atan2(v.z, v.x), asin(v.y)); // NOTE: This overload of atan() returns in range [-pi; pi], not the same as default math
    // uv *= float2(1, -1); // I don't need to flip UV since, I do flip on load.
    uv *= s_InvAtan; // transform into [-0.5, 0.5] range, normalizing from 2pi && pi.
    return uv + 0.5f; // final transform to uv space
}

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    const float2 uv = SampleSphericalMap(normalize(fsInput.LocalPos));
    const float3 outColor = Shaders::Texture_Heap[u_PC.SrcTextureID].Sample(uv).rgb;

    return float4(outColor, 1.0f);
}