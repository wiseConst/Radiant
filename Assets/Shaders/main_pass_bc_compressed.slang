// main_pass_bc_compressed.slang

#include "../../Source/ShaderDefines.hpp"
#include "clustered_shading/light_clusters_defines.hpp"
#include "csm/csm_defines.hpp"

struct FragmentStageInput
{ 
    float4 Color;
    float2 UV;
    float3 FragPosVS; // Used for CSM && light cluster detection.
    float3 FragPosWS;
    float3 Normal;
    float3 Tangent;
    float3 Bitangent;
};

struct VSOutput
{
    FragmentStageInput FSInput : FragmentStageInput;
    float4 sv_position : SV_Position;
};

struct MainPassShaderData
{
    uint IrradianceMapTextureCubeID;
    uint PrefilteredMapTextureCubeID; 
    uint PrefilteredMapLodCount;
    uint BRDFIntegrationTextureID;
    uint SSAOTextureID;
    uint SSSTextureID;
    float2 ScaleBias; // For clustered shading, x - scale, y - bias
    const Shaders::CascadedShadowMapsData *CSMData;
    uint ShadowMapAtlasTextureID;
};

struct PushConstantBlock
{
    float3 scale;
    float3 translation;
    float4 orientation;
    const Shaders::CameraData *CameraData;
    const VertexPosition *VtxPositions;
    const VertexAttribute *VtxAttributes;
    const Shaders::GLTFMaterial *MaterialData;
    const Shaders::LightData *LightData;
    const Shaders::LightClusterList *LightClusterList;
    const MainPassShaderData *MPSData;
};
[vk::push_constant] PushConstantBlock u_PC;

[shader("vertex")]
VSOutput vertexMain(uint32_t vertexID: SV_VertexID)
{
    const float3 worldPos = Shaders::RotateByQuat(u_PC.VtxPositions[vertexID].Position * u_PC.scale, u_PC.orientation) + u_PC.translation;

    VSOutput output;
    output.FSInput.Color = max(Shaders::UnpackUnorm4x8(u_PC.VtxAttributes[vertexID].Color), float4(1.0f));
    output.FSInput.UV = u_PC.VtxAttributes[vertexID].UV;
    output.FSInput.FragPosWS = worldPos;
    output.sv_position = mul(u_PC.CameraData->ViewProjectionMatrix, float4(worldPos, 1.0f));
    output.FSInput.FragPosVS = mul(u_PC.CameraData->ViewMatrix, float4(worldPos, 1.0f)).xyz;

    const float3x3 normalMatrix = transpose(Shaders::QuatToRotMat3(u_PC.orientation)); // NOTE: idk if I need transpose(), cuz I use CR, but slang constructs matrices in RC layout
    output.FSInput.Normal = normalize(mul(normalMatrix, Shaders::DecodeOct(u_PC.VtxAttributes[vertexID].Normal)));
    
    float3 T = normalize(mul(normalMatrix, Shaders::DecodeOct(u_PC.VtxAttributes[vertexID].Tangent)));
    T = normalize(T - dot(T, output.FSInput.Normal) * output.FSInput.Normal);
    output.FSInput.Tangent = T;
    output.FSInput.Bitangent = u_PC.VtxAttributes[vertexID].TSign * cross(output.FSInput.Normal, T);

    return output;
}

float4 UnpackNormalMap(float4 TextureSample)
{
    float2 NormalXY = TextureSample.xy;

    NormalXY = NormalXY * float2(2.0f, 2.0f) - float2(1.0f, 1.0f);
    const float NormalZ = sqrt(saturate(1.0f - dot(NormalXY, NormalXY))); // Derived from unit vector length equation: x^2 + y^2 + z^2 = 1.
    return float4(NormalXY.xy, NormalZ, 1.0f);
}

float3 EvaluateNormalMap(const float3 T, const float3 B, const float3 N, const float2 uv, const Shaders::GLTFMaterial *materialData)
{
    if (materialData->NormalTextureID != 0)
    {
        const float3 unpackedN = UnpackNormalMap(Shaders::Texture_Heap[materialData->NormalTextureID].Sample(uv)).xyz;
        const float3 reconstructedN = unpackedN * materialData->NormalScale;
        const float3x3 TBN = transpose(float3x3( T, B, N)); // NOTE: slang treats matrices(when we construct them in shader) as rowXcol, but we target colXrow!
        return normalize(mul(TBN, reconstructedN));
    }
    return normalize(N);
}

// NOTE: Input: normalized vectors V, L, N.
float3 EvaluateRadiance(const float3 F0, const float3 lightColor, const float3 albedoOverPi, const float2 metallicRoughness, const float3 V, const float3 L, const float3 N)
{
    const float3 H = normalize(V + L);

    const float NdotH = max(dot(N, H), Shaders::s_KINDA_SMALL_NUMBER);
    const float NdotL = max(dot(N, L), Shaders::s_KINDA_SMALL_NUMBER);
    const float NdotV = abs(dot(N, V)) + Shaders::s_KINDA_SMALL_NUMBER;

    const float3 F = Shaders::EvaluateFresnelSchlick(NdotV, F0); // kS
    const float3 DFG = Shaders::EvaluateDistributionGGX(NdotH, metallicRoughness.y) * Shaders::EvaluateGeometrySmith(NdotV, NdotL, metallicRoughness.y) * F;
    const float denom = 4 * NdotV * NdotL;
    const float3 kD = (1.0f - F) * (1.0f - metallicRoughness.x);

    return NdotL * lightColor * (albedoOverPi * kD + DFG / denom);
}

#define VISUALIZE_CSM_SPLITS 0
#define PCF_SHADOWS_ENABLE 0
#define PCF_RADIUS 1

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

void ProcessSinglePointLight(const uint pointLightIndex, inout float3 irradiance, const float3 fragPosWS, const float3 F0, const float3 albedoOverPi, const float2 metallicRoughness, const float3 V, const float3 N)
{
    PointLight pl = u_PC.LightData->PointLights[pointLightIndex];
    const float lightToFragDistance = length(pl.sphere.Origin - fragPosWS);
    const float att = max(min(1.0f - pow(lightToFragDistance / pl.sphere.Radius, 4), 1.0f), 0.0f) / pow(lightToFragDistance, 2);
    const float3 L = normalize(pl.sphere.Origin - fragPosWS);
    irradiance += att * EvaluateRadiance(F0, Shaders::UnpackUnorm4x8(pl.Color).xyz * pl.Intensity, albedoOverPi, metallicRoughness, V, L, N);
}

void ProcessPointLights(inout float3 irradiance, const uint froxelIndex, const float3 fragPosWS, const float3 F0, const float3 albedoOverPi, const float2 metallicRoughness, const float3 V, const float3 N)
{
    // Read range of words of visibility bits.
    for (uint bitmaskIndex = 0; bitmaskIndex < LIGHT_CLUSTERS_POINT_LIGHT_BITMASK_ARRAY_SIZE; ++bitmaskIndex)
    {
        uint currentBitmask = u_PC.LightClusterList[froxelIndex].PointLightBitmasks[bitmaskIndex];
        while (currentBitmask != 0)
        {
            const uint lightBitIndex = firstbitlow(currentBitmask);
            const uint lightIndex = bitmaskIndex * LIGHT_CLUSTERS_WORD_SIZE + lightBitIndex;
            ProcessSinglePointLight(lightIndex, irradiance, fragPosWS, F0, albedoOverPi, metallicRoughness, V, N);
            currentBitmask &= ~(1u << lightBitIndex); // Remove processed light.
        }
    }
}

[shader("fragment")]
float4 fragmentMain(FragmentStageInput fsInput: FragmentStageInput, float4 fragCoord : SV_Position) : SV_Target
{
    const float2 globalUV = fragCoord.xy * u_PC.CameraData->InvFullResolution;

    const uint32_t slice = Shaders::GetLightClusterDepthSlice(fsInput.FragPosVS.z, u_PC.MPSData.ScaleBias.x, u_PC.MPSData.ScaleBias.y);
#if VISUALIZE_LIGHT_CLUSTERS_SUBDIVISION
    return Shaders::s_RAINBOW_COLORS[slice % Shaders::s_RAINBOW_COLOR_COUNT] * float4(float3(0.8f), 1.0f);
#endif

   // return float4(float3(Shaders::Texture_Heap[u_PC.MPSData.SSAOTextureID].Sample(globalUV).r), 1.0f);
   //  return float4( float3(Shaders::Texture_Heap[u_PC.MPSData.SSSTextureIDD].Sample(globalUV).r), 1.0f);

    const Shaders::GLTFMaterial *materialData = u_PC.MaterialData;
    float4 albedo = fsInput.Color * Shaders::UnpackUnorm4x8(materialData->PbrData.BaseColorFactor);
    if(materialData->PbrData.AlbedoTextureID != 0)
    {
        albedo *= Shaders::sRGB2Linear(Shaders::Texture_Heap[materialData->PbrData.AlbedoTextureID].Sample(fsInput.UV));
    }

    float3 emissive = materialData->EmissiveFactor;
    if (materialData->EmissiveTextureID != 0)
    {
        emissive *= Shaders::sRGB2Linear(Shaders::Texture_Heap[materialData->EmissiveTextureID].Sample(fsInput.UV)).rgb;
    }
    float3 irradiance = emissive;

    const float3 V = normalize(u_PC.CameraData->Position - fsInput.FragPosWS);
    const float3 N = EvaluateNormalMap(fsInput.Tangent, fsInput.Bitangent, fsInput.Normal, fsInput.UV, materialData);
    float2 metallicRoughness = float2(Shaders::UnpackUnorm2x8(materialData->PbrData.MetallicFactor), Shaders::UnpackUnorm2x8(materialData->PbrData.RoughnessFactor));
    if (materialData->PbrData.MetallicRoughnessTextureID != 0)
    {
        metallicRoughness *= Shaders::Texture_Heap[materialData->PbrData.MetallicRoughnessTextureID].Sample(fsInput.UV).bg;
    }

    const float3 F0 = lerp(Shaders::s_BASE_REFLECTIVITY, albedo.rgb, metallicRoughness.x); // Initial reflectivity based on metallness

    const float3 dirL = normalize(u_PC.LightData.Sun.Direction);
    float shadow = 0.0f;
    uint cascadeIndex = 0;
    if (u_PC.LightData.Sun.bCastShadows) {

        const float4 fComparison = float4(float4(fsInput.FragPosVS.z) < float4(u_PC.MPSData.CSMData.CascadeSplits[0], 
            u_PC.MPSData.CSMData.CascadeSplits[1], u_PC.MPSData.CSMData.CascadeSplits[2], u_PC.MPSData.CSMData.CascadeSplits[3]));
            float fIndex = dot(
        float4(float(SHADOW_MAP_CASCADE_COUNT > 0),
               float(SHADOW_MAP_CASCADE_COUNT > 1),
               float(SHADOW_MAP_CASCADE_COUNT > 2),
               float(SHADOW_MAP_CASCADE_COUNT > 3))
            , fComparison);

        fIndex = min(fIndex, SHADOW_MAP_CASCADE_COUNT - 1);
        cascadeIndex = (uint)fIndex;

        const float4 fragPosLS = mul(u_PC.MPSData.CSMData.ViewProjectionMatrix[cascadeIndex], float4(fsInput.FragPosWS, 1.0f));
        float3 projCoords = fragPosLS.xyz / fragPosLS.w;
        projCoords.xy = projCoords.xy * 0.5 + 0.5;

        // transform texture coordiantes to sample appropriate part of shadow texture atlas
        Shaders::AdjustTextureCoordinatesToShadowTextureAtlas(projCoords.xy, cascadeIndex);

        if (Shaders::is_saturated(projCoords))
        {
            const float bias = max(0.005f, 0.1f * (1.0f - dot(N, dirL))) / abs(u_PC.MPSData.CSMData.CascadeSplits[cascadeIndex]);
            const float currentDepth = projCoords.z;  // get depth of current fragment from light's perspective

#if PCF_SHADOWS_ENABLE
            static constexpr float2 texelSize = 1.0f / SHADOW_MAP_CASCADE_SIZE;
            for (int x = -PCF_RADIUS; x <= PCF_RADIUS; ++x)
            {
                for (int y = -PCF_RADIUS; y <= PCF_RADIUS; ++y)
                {
                    const float sampledDepth = Shaders::Texture_Heap[u_PC.MPSData.ShadowMapAtlasTextureID].Sample(projCoords.xy + float2(x, y) * texelSize) .r;
                    const float fOccluded = (currentDepth + bias) < sampledDepth ? 1.0 : 0.0;
                    shadow += fOccluded;
                }
            }
            shadow /= (float)((PCF_RADIUS * 2 + 1) * (PCF_RADIUS * 2 + 1));
#else
            const float sampledDepth = Shaders::Texture_Heap[u_PC.MPSData.ShadowMapAtlasTextureID].Sample(projCoords.xy).r;
            shadow += (currentDepth + bias) < sampledDepth ? 1.0 : 0.0;
#endif
        }
    }

    const float3 albedoOverPi = albedo.rgb * Shaders::s_RcpPI;
    irradiance += /*Shaders::Texture_Heap[u_PC.SSSTextureID].Sample(globalUV).r **/ (1.0f - shadow) * EvaluateRadiance(F0, Shaders::UnpackUnorm4x8(u_PC.LightData->Sun.Color).xyz * u_PC.LightData->Sun.Intensity, albedoOverPi, metallicRoughness, V, dirL, N);

    const uint froxelIndex = Shaders::GetLightClusterIndex(fragCoord.xy, u_PC.CameraData.FullResolution, slice);
    ProcessPointLights(irradiance, froxelIndex, fsInput.FragPosWS, F0, albedoOverPi, metallicRoughness, V, N);

    // Ambient / indirect part
    float3 ambient = float3(0.0f);
    float3 ao = float3(1.0f);
    if (u_PC.MPSData.SSAOTextureID != 0)
    {
        ao *= Shaders::Texture_Heap[u_PC.MPSData.SSAOTextureID].Sample(globalUV).r;
    }
    if (materialData->OcclusionTextureID != 0)
    {
        ao *= Shaders::Texture_Heap[materialData->OcclusionTextureID].Sample(fsInput.UV).r * Shaders::UnpackUnorm2x8(materialData->OcclusionStrength);
    }
    if (u_PC.MPSData.IrradianceMapTextureCubeID != 0 && u_PC.MPSData.PrefilteredMapTextureCubeID != 0 && u_PC.MPSData.BRDFIntegrationTextureID != 0) {
        const float NdotV = max(dot(N, V), Shaders::s_KINDA_SMALL_NUMBER);

        const float3 cubemapIrradiance = Shaders::Texture_Cube_Heap[u_PC.MPSData.IrradianceMapTextureCubeID].Sample(N).rgb;
        const float3 F = Shaders::EvaluateFresnelSchlickRoughness(NdotV, F0, metallicRoughness.y);
        const float3 kD = (1.0f - F) * (1.0f - metallicRoughness.x);
        const float3 indirectDiffuse = kD * cubemapIrradiance * albedoOverPi;

        const float3 cubemapPrefilteredColor = Shaders::Texture_Cube_Heap[u_PC.MPSData.PrefilteredMapTextureCubeID] .SampleLevel(reflect(-V, N), metallicRoughness.y * u_PC.MPSData.PrefilteredMapLodCount) .rgb;
        const float2 brdf = Shaders::Texture_Heap[u_PC.MPSData.BRDFIntegrationTextureID].Sample(float2(NdotV, metallicRoughness.y)).rg;
        const float3 indirectSpecular = cubemapPrefilteredColor * (F * brdf.r + brdf.g);

        ambient = (indirectDiffuse + indirectSpecular) * ao;
    }
    else
    {
        ambient = 0.03f * albedoOverPi * ao;
    }
    irradiance += ambient;

#if VISUALIZE_LIGHT_CLUSTERS_HEAT_MAP
    uint heatmapPointLightCount = 0;
    // Read range of words of visibility bits
    for (uint bitmaskIndex = 0; bitmaskIndex < LIGHT_CLUSTERS_POINT_LIGHT_BITMASK_ARRAY_SIZE; ++bitmaskIndex)
    {
        uint currentBitmask = u_PC.LightClusterList[froxelIndex].PointLightBitmasks[bitmaskIndex];
        while (currentBitmask != 0)
        {
            const uint lightBitIndex = firstbitlow(currentBitmask);
            currentBitmask &= ~(1u << lightBitIndex); // Remove processed light.
            ++heatmapPointLightCount;
        }
    }
    return lerp(Heatmap(heatmapPointLightCount), float4(irradiance, albedo.a), 0.3f);
#endif

#if VISUALIZE_CSM_SPLITS
    irradiance *= Shaders::s_RAINBOW_COLORS[cascadeIndex % Shaders::s_RAINBOW_COLOR_COUNT].rgb;
#endif

    if (albedo.a < materialData->AlphaCutoff) discard;
    return float4(irradiance, albedo.a);
}