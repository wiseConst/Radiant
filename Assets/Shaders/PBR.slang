// PBR.slang

#include "../../Source/ShaderDefines.h"

struct FragmentStageInput
{ 
    float4 Color;
    float2 UV;
    float3 WorldPos;
    float3 Normal;
    float3x3 TBN;
};

struct VertexStageOutput
{
    FragmentStageInput FSInput : FragmentStageInput;
    float4 sv_position : SV_Position;
};

struct PushConstantBlock
{
    float4x4 ModelMatrix;
    const CameraData *CameraData;
    const VertexPosition *VtxPositions;
    const VertexAttribute *VtxAttributes;
    const Shaders::GLTFMaterial *MaterialData;
};

[vk::push_constant] PushConstantBlock u_PC;

[shader("vertex")]
VertexStageOutput vertexMain(uint32_t vertexID: SV_VertexID)
{
    VertexStageOutput output;

    const float4 worldPos = mul(u_PC.ModelMatrix, float4(u_PC.VtxPositions[vertexID].Position, 1.0f));
    output.FSInput.Color = u_PC.VtxAttributes[vertexID].Color;
    output.FSInput.UV = u_PC.VtxAttributes[vertexID].UV;
    output.FSInput.WorldPos = worldPos.xyz;
    output.sv_position = mul(u_PC.CameraData->ViewProjectionMatrix, worldPos);

    const float3x3 normalMatrix = transpose(float3x3(u_PC.ModelMatrix));
    const float3 N = normalize(mul(normalMatrix, u_PC.VtxAttributes[vertexID].Normal));
    float3 T = normalize(mul(normalMatrix, u_PC.VtxAttributes[vertexID].Tangent.xyz));
    T = normalize(T - dot(T, N) * N);
    const float3 B = u_PC.VtxAttributes[vertexID].Tangent.w * cross(N, T);
    output.FSInput.Normal = N;
    output.FSInput.TBN = float3x3(T, B, N);

    return output;
}

#define PI 3.14159265

//struct PBRComputeData
//{
//    float NdotL;
//    float NdotH;
//    float NdotV;
//    float HdotV;
//};

// Microfacet distribution aligned to halfway vector
float EvaluateDistributionGGX(const float NdotH, const float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float denom = (NdotH * NdotH * (a2 - 1.0f) + 1.0f);
    
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotV, const float roughness)
{
    const float k = (roughness + 1.0f) * (roughness + 1.0f) / 8;
    return NdotV / (NdotV * (1.0f - k) + k);
}

// geom obstruction && geom shadowing
float EvaluateGeometrySmith(const float NdotV, const float NdotL, const float roughness)
{
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

// FresnelSchlick, evaluating ratio of base reflectivity looking perpendicularly towards surface
float3 EvaluateFresnel(const float HdotV, const float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - HdotV, 5);
}

float3 EvaluateNormalMap(const float3x3 TBN, const float2 uv, const Shaders::GLTFMaterial* materialData)
{
    const float3 N = normalize((Shaders::Texture_Heap[materialData->NormalTextureID].Sample(uv).xyz * 2.0f - 1.0f) * materialData->NormalScale);
    return mul(TBN, N);
}

// https://gamedev.stackexchange.com/questions/92015/optimized-linear-to-srgb-glsl
// Converts a color from linear light gamma to sRGB gamma
float4 LinearTosRGB(const float4 linearRGB)
{
    bool3 cutoff = linearRGB.rgb < float3(0.0031308);
    float3 higher = float3(1.055) * pow(linearRGB.rgb, float3(1.0 / 2.4)) - float3(0.055);
    float3 lower = linearRGB.rgb * float3(12.92);

    return float4(lerp(higher, lower, cutoff), linearRGB.a);
}

// Converts a color from sRGB gamma to linear light gamma
float4 sRGBToLinear(const float4 sRGB)
{
    const bool3 cutoff = sRGB.rgb < float3(0.04045);
    const float3 higher = pow((sRGB.rgb + float3(0.055)) / float3(1.055), float3(2.4));
    const float3 lower = sRGB.rgb / float3(12.92);

    return float4(lerp(higher, lower, cutoff), sRGB.a);
}

// Attenuation for different light types:
// Point: attenuation = max( min( 1.0 - ( current_distance / range )^4, 1 ), 0 ) / current_distance^2
// Spot: These two values can be calculated on the CPU and passed into the shader
//      float lightAngleScale = 1.0f / max(0.001f, cos(innerConeAngle) - cos(outerConeAngle));
//      float lightAngleOffset = -cos(outerConeAngle) * lightAngleScale;
// Then, in the shader:
//      float cd = dot(spotlightDir, normalizedLightVector);
//      float angularAttenuation = saturate(cd * lightAngleScale + lightAngleOffset);
//      angularAttenuation *= angularAttenuation;

// NOTE: By default all textures in glTF are in sRGB color space, so we convert all textures firstly in linear RGB color space and then apply in the end gamma correction.
[shader("fragment")]
float4 fragmentMain(FragmentStageInput fsInput: FragmentStageInput) : SV_Target
{
    float3 irradiance = float3(0.f);
    const float intensity = 1.1f;
    const float3 radiance = float3(0.7f, 0.4f, 0.1f) * intensity;
    const float3 lightDir = float3(0.0f, 0.8f, 0.5f); // Vector from origin to sun

    const Shaders::GLTFMaterial *materialData = u_PC.MaterialData;
    const float4 albedo = /*fsInput.Color **/ materialData->PbrData.BaseColorFactor * sRGBToLinear(Shaders::Texture_Heap[materialData->PbrData.AlbedoTextureID].Sample(fsInput.UV));
    if (albedo.a < materialData->AlphaCutoff) discard;

    const float2 metallicRoughness = Shaders::Texture_Heap[materialData->PbrData.MetallicRoughnessTextureID].Sample(fsInput.UV).bg * float2(materialData->PbrData.MetallicFactor, materialData->PbrData.RoughnessFactor);
    const float3 F0 = lerp(float3(0.04f), albedo.rgb, metallicRoughness.x); // Initial reflectivity based on metallness
    const float3 emissive = materialData->EmissiveFactor * sRGBToLinear(Shaders::Texture_Heap[materialData->EmissiveTextureID].Sample(fsInput.UV)).rgb;
    const float3 ambient = float3(0.03f) * albedo.rgb * Shaders::Texture_Heap[materialData->OcclusionTextureID].Sample(fsInput.UV).r * materialData->OcclusionStrength;

    const float3 N = materialData->NormalTextureID != 0 ? EvaluateNormalMap(fsInput.TBN, fsInput.UV, materialData) : fsInput.Normal;
    const float3 L = normalize(lightDir);
    const float3 V = normalize(u_PC.CameraData->Position - fsInput.WorldPos);
    const float3 H = normalize(V + L);

    const float NdotH = max(dot(N, H), 0.0f);
    const float NdotV = max(dot(N, V), 0.0f);
    const float NdotL = max(dot(N, L), 0.0f);
    const float HdotV = max(dot(H, V), 0.0f);

    const float3 F = EvaluateFresnel(HdotV, F0); // kS

    const float3 DFG = EvaluateDistributionGGX(NdotH, metallicRoughness.y) * F * EvaluateGeometrySmith(NdotV, NdotL, metallicRoughness.y);
    const float denom = max(4 * NdotV * NdotL, Shaders::s_KINDA_SMALL_NUMBER);
    const float3 kD = (float3(1.0f) - F) * (1.0f - metallicRoughness.x);
    const float3 BRDF = (kD * albedo.rgb / PI + DFG / denom);

    irradiance += emissive + ambient + BRDF * radiance * NdotL;

    // Reinhard tone map(HDR->LDR) + gamma correction
    irradiance.rgb = irradiance.rgb / (irradiance.rgb + float3(1.0f));
    irradiance.rgb = pow(irradiance.rgb, float3(1.0f / 2.2f));
    return float4(irradiance, albedo.a);
}