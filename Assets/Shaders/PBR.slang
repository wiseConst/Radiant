// PBR.slang

#include "../../Source/ShaderDefines.h"
#include "light_clusters_defines.hpp"

struct FragmentStageInput
{ 
    float4 Color;
    float2 UV;
    float3 FragPosWS;
    float3 Normal;
    float3 Tangent;
    float3 Bitangent;
};

struct VertexStageOutput
{
    FragmentStageInput FSInput : FragmentStageInput;
    float4 sv_position : SV_Position;
};

struct PushConstantBlock
{
    float3 scale;
    float3 translation;
#if !RENDER_FORCE_IGPU
    half4 orientation; // NOTE: quaternions are in range[-1, 1], so on CPU we firstly convert them in range[0, 1] and then quantize into fp16.
#else
    float4 orientation;
#endif
    const Shaders::CameraData *CameraData;
    const VertexPosition *VtxPositions;
    const VertexAttribute *VtxAttributes;
    const Shaders::GLTFMaterial *MaterialData;
    const Shaders::LightData *LightData;
    const Shaders::LightClusterList *LightClusterList;
    uint32_t SSAOTextureID;
    uint32_t SSSTextureID;
    float2 ScaleBias; // For clustered shading, x - scale, y - bias
};
[vk::push_constant] PushConstantBlock u_PC;

[shader("vertex")]
VertexStageOutput vertexMain(uint32_t vertexID: SV_VertexID)
{
    VertexStageOutput output;

#if !RENDER_FORCE_IGPU
    const float3 worldPos = Shaders::RotateByQuat(u_PC.VtxPositions[vertexID].Position * u_PC.scale, u_PC.orientation * 2.0f - 1.0f) + u_PC.translation;
 #else
    const float3 worldPos = Shaders::RotateByQuat(u_PC.VtxPositions[vertexID].Position * u_PC.scale, u_PC.orientation) + u_PC.translation;
 #endif
    output.FSInput.Color = max(Shaders::UnpackUnorm4x8(u_PC.VtxAttributes[vertexID].Color), float4(1.0f));
    output.FSInput.UV = u_PC.VtxAttributes[vertexID].UV;
    output.FSInput.FragPosWS = worldPos;
    output.sv_position = mul(u_PC.CameraData->ViewProjectionMatrix, float4(worldPos, 1.0f));

#if !RENDER_FORCE_IGPU
    const float3x3 normalMatrix = Shaders::QuatToRotMat3(u_PC.orientation * 2.0f - 1.0f);
#else
    const float3x3 normalMatrix = Shaders::QuatToRotMat3(u_PC.orientation);
#endif

    output.FSInput.Normal = normalize(mul(normalMatrix, Shaders::DecodeOct(u_PC.VtxAttributes[vertexID].Normal)));
    
    float3 T = normalize(mul(normalMatrix, Shaders::DecodeOct(u_PC.VtxAttributes[vertexID].Tangent)));
    T = normalize(T - dot(T, output.FSInput.Normal) * output.FSInput.Normal);
    output.FSInput.Tangent = T;

    output.FSInput.Bitangent = float(u_PC.VtxAttributes[vertexID].TSign) * cross(output.FSInput.Normal, T);

    return output;
}

// Microfacet distribution aligned to halfway vector
float EvaluateDistributionGGX(const float NdotH, const float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float denom = (NdotH * NdotH * (a2 - 1.0f) + 1.0f);
    
    return a2 / (Shaders::s_PI * denom * denom);
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
    static constexpr float F90 = 1.0f;
    return F0 + (F90 - F0) * pow(1.0f - HdotV, 5.0f);
}

float3 EvaluateNormalMap(const float3 T, const float3 B, const float3 N, const float2 uv, const Shaders::GLTFMaterial *materialData)
{
    if (materialData->NormalTextureID != 0)
    {
        const float3 sampledN = (Shaders::Texture_Heap[materialData->NormalTextureID].Sample(uv).xyz * 2.0f - 1.0f) * materialData->NormalScale;
        const float3x3 TBN = transpose(float3x3(T, B, N)); // NOTE: slang treats matrices as rowXcol, but we target colXrow!
        return normalize(mul(TBN, sampledN));
    }
    return normalize(N);
}

// NOTE: Input: normalized vectors V, L, N.
float3 EvaluateRadiance(const float3 F0, const float3 lightColor, const float3 albedo, const float2 metallicRoughness, const float3 V, const float3 L, const float3 N)
{
    const float3 H = normalize(V + L);

    const float NdotH = max(dot(N, H), 0.0f);
    const float NdotV = max(dot(N, V), 0.0f);
    const float NdotL = max(dot(N, L), 0.0f);
    const float HdotV = max(dot(H, V), 0.0f);

    const float3 F = EvaluateFresnel(HdotV, F0); // kS
    const float3 DFG = EvaluateDistributionGGX(NdotH, metallicRoughness.y) * F * EvaluateGeometrySmith(NdotV, NdotL, metallicRoughness.y);
    const float denom = max(4 * NdotV * NdotL, Shaders::s_KINDA_SMALL_NUMBER);
    const float3 kD = (1.0f - F) * (1.0f - metallicRoughness.x);

    return (kD * albedo / Shaders::s_PI + DFG / denom) * lightColor * NdotL;
}

#define VISUALIZE_LIGHT_CLUSTERS_SUBDIVISION 0
#define VISUALIZE_LIGHT_CLUSTERS_HEAT_MAP 0

#ifdef VISUALIZE_LIGHT_CLUSTERS_HEAT_MAP
float4 Heatmap(uint lightCount)
{
    static const uint heatmapSize = 6;
    const float3 heatmap[heatmapSize] = {
        float3(0, 0, 0),
		float3(0, 0, 1),
		float3(0, 1, 1),
		float3(0, 1, 0),
		float3(1, 1, 0),
		float3(1, 0, 0),
    };
    const uint maxHeat = 50;
    const float l = saturate(float(lightCount) / maxHeat) * (heatmapSize - 1);
    const float3 a = heatmap[int(floor(l))];
    const float3 b = heatmap[int(ceil(l))];
    return float4(lerp(a, b, l - floor(l)), 1.f);
}
#endif

uint GetLightClusterIndex(const float2 fullResolution, const uint3 lightClusterSubdivisions, const uint slice, const float2 fragCoord)
{
    // Locating which cluster this fragment is part of.
    const float2 froxelSize = fullResolution / (float2)lightClusterSubdivisions.xy;
    const uint3 froxelID = uint3(uint2(fragCoord.xy / froxelSize), slice);
    return froxelID.x + froxelID.y * lightClusterSubdivisions.x + froxelID.z * lightClusterSubdivisions.x * lightClusterSubdivisions.y;
}

[shader("fragment")]
float4 fragmentMain(FragmentStageInput fsInput: FragmentStageInput, float4 fragCoord : SV_Position) : SV_Target
{
    const float2 globalUV = fragCoord.xy * u_PC.CameraData->InvFullResolution;
    // return float4(globalUV.x, globalUV.y, 0.0, 1.0f);        // Vulkan coordinate system(Y down)
    // return float4(globalUV.x, 1.0f - globalUV.y, 0.0, 1.0f); // OGL coordinate system(Y up)

    const float3 fragPosVS = mul(u_PC.CameraData->ViewMatrix, float4(fsInput.FragPosWS, 1.0f)).xyz;
    const uint32_t slice = uint32_t(max(log2(-fragPosVS.z) * u_PC.ScaleBias.x + u_PC.ScaleBias.y, 0.0f));
#if VISUALIZE_LIGHT_CLUSTERS_SUBDIVISION
    return Shaders::s_RAINBOW_COLORS[slice % Shaders::s_RAINBOW_COLOR_COUNT] * float4(float3(0.8f), 1.0f);
#endif

    //return float4(float3(Shaders::Texture_Heap[u_PC.SSAOTextureID].Sample(globalUV).r), 1.0f);
   // return float4( float3(Shaders::Texture_Heap[u_PC.SSSTextureID].Sample(globalUV).r), 1.0f);

    const Shaders::GLTFMaterial *materialData = u_PC.MaterialData;
    const float4 albedo = fsInput.Color * Shaders::UnpackUnorm4x8(materialData->PbrData.BaseColorFactor) * Shaders::sRGB2Linear(Shaders::Texture_Heap[materialData->PbrData.AlbedoTextureID].Sample(fsInput.UV));

    const float3 emissive = materialData->EmissiveFactor * Shaders::sRGB2Linear(Shaders::Texture_Heap[materialData->EmissiveTextureID].Sample(fsInput.UV)).rgb;
    const float3 ao = Shaders::Texture_Heap[u_PC.SSAOTextureID].Sample(globalUV).r * Shaders::Texture_Heap[materialData->OcclusionTextureID].Sample(fsInput.UV).r * Shaders::UnpackUnorm2x8(materialData->OcclusionStrength);
    const float3 ambient = float3(0.03f) * albedo.rgb * ao;
    float3 irradiance = ambient + emissive;

    const float3 V = normalize(u_PC.CameraData->Position - fsInput.FragPosWS);
    const float3 N = EvaluateNormalMap(fsInput.Tangent, fsInput.Bitangent, fsInput.Normal, fsInput.UV, materialData);
    const float2 metallicRoughness = Shaders::Texture_Heap[materialData->PbrData.MetallicRoughnessTextureID].Sample(fsInput.UV).bg * float2(Shaders::UnpackUnorm2x8(materialData->PbrData.MetallicFactor), Shaders::UnpackUnorm2x8(materialData->PbrData.RoughnessFactor));
    const float3 F0 = lerp(float3(0.04f), albedo.rgb, metallicRoughness.x); // Initial reflectivity based on metallness

    irradiance += /*Shaders::Texture_Heap[u_PC.SSSTextureID].Sample(globalUV).r **/ EvaluateRadiance(F0, Shaders::UnpackUnorm4x8(u_PC.LightData->Sun.Color).xyz * u_PC.LightData->Sun.Intensity, albedo.rgb, metallicRoughness, V, normalize(u_PC.LightData.Sun.Direction), N);

    const uint froxelIndex = GetLightClusterIndex(u_PC.CameraData.FullResolution, Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS, slice, fragCoord.xy);

    for (uint k = 0; k < u_PC.LightClusterList[froxelIndex].PointLightCount; ++k)
    {
        PointLight pl = u_PC.LightData->PointLights[u_PC.LightClusterList[froxelIndex].PointLightIndices[k]];
        const float lightToFragDistance = length(pl.sphere.Origin - fsInput.FragPosWS);
        const float att = max(min(1.0f - pow(lightToFragDistance / pl.sphere.Radius, 4), 1.0f), 0.0f) / pow(lightToFragDistance, 2);
        const float3 L = normalize(pl.sphere.Origin - fsInput.FragPosWS);
        irradiance += att * EvaluateRadiance(F0, Shaders::UnpackUnorm4x8(pl.Color).xyz * pl.Intensity, albedo.rgb, metallicRoughness, V, L, N);
    }

#if VISUALIZE_LIGHT_CLUSTERS_HEAT_MAP
    uint heatmapPointLightCount = 0;

#define COMPUTE_POINT_LIGHTS_ALONG_ALL_DEPTH_SLICES 0
#if COMPUTE_POINT_LIGHTS_ALONG_ALL_DEPTH_SLICES
    for (uint sliceIndex = 0; sliceIndex < Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS.z; ++sliceIndex)
        heatmapPointLightCount += u_PC.LightClusterList[GetLightClusterIndex(u_PC.CameraData.FullResolution, Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS, sliceIndex, fragCoord.xy)].PointLightCount;
#else
    heatmapPointLightCount += u_PC.LightClusterList[froxelIndex].PointLightCount;
#endif

    return lerp(Heatmap(heatmapPointLightCount), float4(irradiance, albedo.a), 0.3f);
#endif

    if (albedo.a < materialData->AlphaCutoff) discard;
    return float4(irradiance, albedo.a);
}