// shading_pbr_bc_compressed.slang

#include "../../../Source/ShaderDefines.hpp"
#include "csm_defines.hpp"

struct FragmentStageInput
{ 
    float4 Color;
    float2 UV;
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
    const Shaders::CascadedShadowMapsData *CSMData;
    uint ShadowMapTextureArrayID;
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
#define CSM_FILTER_ACROSS_CASCADES 1

void EvaluateDirectionalShadows(out uint cascadeIndex, out float shadow, const float3 L, const float3 fragPosWS, const float3 N, const float maxShadowDistance, const float shadowTransitionDistance)
{
    const float NdotL = saturate(dot(N, L));
    const float2 shadowOffsets = Shaders::GetShadowOffsets(NdotL);
    const float3 fragPosWSBiased = fragPosWS + 0.001 * N * shadowOffsets.x + 0.003 * shadowOffsets.y * L;

    shadow = 0.0f;
    cascadeIndex = SHADOW_MAP_CASCADE_COUNT - 1;
    const float3 fragPosVS = mul(u_PC.CameraData->ViewMatrix, float4(fragPosWSBiased, 1.0f)).xyz;
    const float positiveDepthVS = abs(fragPosVS.z);

    const float4 fComparison = float4(float4(positiveDepthVS) > float4(u_PC.MPSData.CSMData.CascadeSplits[0], 
        u_PC.MPSData.CSMData.CascadeSplits[1], u_PC.MPSData.CSMData.CascadeSplits[2], u_PC.MPSData.CSMData.CascadeSplits[3]));
    float fIndex = dot(
    float4(float(SHADOW_MAP_CASCADE_COUNT > 0),
           float(SHADOW_MAP_CASCADE_COUNT > 1),
           float(SHADOW_MAP_CASCADE_COUNT > 2),
           float(SHADOW_MAP_CASCADE_COUNT > 3))
        , fComparison);

    fIndex = min(fIndex, SHADOW_MAP_CASCADE_COUNT - 1);
    cascadeIndex = (uint)fIndex;

    shadow += Shaders::SampleShadowMapArray(Shaders::Texture_Array_Heap[u_PC.MPSData.ShadowMapTextureArrayID], cascadeIndex, u_PC.MPSData.CSMData, fragPosWSBiased, NdotL, u_PC.LightData.Sun.Size);

#if CSM_FILTER_ACROSS_CASCADES
    // Sample the next cascade, and blend between the two results to
    // smooth the transition
    const float BlendThreshold = 0.1f;

    const float nextSplit = u_PC.MPSData.CSMData.CascadeSplits[cascadeIndex];
    const float splitSize = cascadeIndex == 0 ? nextSplit : nextSplit - u_PC.MPSData.CSMData.CascadeSplits[cascadeIndex - 1];
    const float fadeFactor = (nextSplit - positiveDepthVS) / splitSize;

    [branch]
    if (fadeFactor <= BlendThreshold && cascadeIndex != SHADOW_MAP_CASCADE_COUNT - 1)
    {
        const uint nextCascadeIndex = cascadeIndex + 1;
        float nextSplitVisibility = Shaders::SampleShadowMapArray(Shaders::Texture_Array_Heap[u_PC.MPSData.ShadowMapTextureArrayID], nextCascadeIndex, u_PC.MPSData.CSMData, fragPosWSBiased, NdotL, u_PC.LightData.Sun.Size);

        const float lerpAmt = smoothstep(0.0f, BlendThreshold, fadeFactor);
        shadow = lerp(nextSplitVisibility, shadow, lerpAmt);
    }
#endif

    const float distance = length(fragPosVS);
    const float shadowFade = saturate(1.0f - ((distance - (maxShadowDistance - shadowTransitionDistance)) / shadowTransitionDistance));

    shadow *= shadowFade;
}

[shader("fragment")]
float4 fragmentMain(FragmentStageInput fsInput: FragmentStageInput, float4 fragCoord : SV_Position) : SV_Target
{
    const Shaders::GLTFMaterial *materialData = u_PC.MaterialData;
    float4 albedo = fsInput.Color * Shaders::UnpackUnorm4x8(materialData->PbrData.BaseColorFactor);
    if(materialData->PbrData.AlbedoTextureID != 0) albedo *= Shaders::sRGB2Linear(Shaders::Texture_Heap[materialData->PbrData.AlbedoTextureID].Sample(fsInput.UV));
    
    float3 emissive = materialData->EmissiveFactor;
    if (materialData->EmissiveTextureID != 0) emissive *= Shaders::sRGB2Linear(Shaders::Texture_Heap[materialData->EmissiveTextureID].Sample(fsInput.UV)).rgb;
    float3 irradiance = emissive;
    
    const float3 V = normalize(u_PC.CameraData->Position - fsInput.FragPosWS);
    const float3 N = EvaluateNormalMap(fsInput.Tangent, fsInput.Bitangent, fsInput.Normal, fsInput.UV, materialData);
    float2 metallicRoughness = float2(Shaders::UnpackUnorm2x8(materialData->PbrData.MetallicFactor), Shaders::UnpackUnorm2x8(materialData->PbrData.RoughnessFactor));
    if (materialData->PbrData.MetallicRoughnessTextureID != 0) metallicRoughness *= Shaders::Texture_Heap[materialData->PbrData.MetallicRoughnessTextureID].Sample(fsInput.UV).bg;

    const float3 F0 = lerp(Shaders::s_BASE_REFLECTIVITY, albedo.rgb, metallicRoughness.x); // Initial reflectivity based on metallness

    const float3 dirL = normalize(u_PC.LightData.Sun.Direction);
    float shadow = 0.0f;
    uint cascadeIndex = 0;
    if (u_PC.LightData.Sun.bCastShadows) EvaluateDirectionalShadows(cascadeIndex, shadow, dirL, fsInput.FragPosWS, N, u_PC.LightData.Sun.MaxShadowDistance, u_PC.LightData.Sun.ShadowFade);

    const float3 albedoOverPi = albedo.rgb * Shaders::s_RcpPI;
    irradiance += (1.0f - shadow) * EvaluateRadiance(F0, Shaders::UnpackUnorm4x8(u_PC.LightData->Sun.Color).xyz * u_PC.LightData->Sun.Intensity, albedoOverPi, metallicRoughness, V, dirL, N);

    // Indirect part
    float3 ao = float3(1.0f);
    if (materialData->OcclusionTextureID != 0)
        ao *= Shaders::Texture_Heap[materialData->OcclusionTextureID].Sample(fsInput.UV).r * Shaders::UnpackUnorm2x8(materialData->OcclusionStrength);

    const float3 ambient = 0.03f * albedo.rgb * ao; 
    irradiance += ambient;

#if VISUALIZE_CSM_SPLITS
    irradiance *= Shaders::s_RAINBOW_COLORS[cascadeIndex % Shaders::s_RAINBOW_COLOR_COUNT].rgb;
#endif

    if (albedo.a < materialData->AlphaCutoff) discard;
    return float4(irradiance, albedo.a);
}