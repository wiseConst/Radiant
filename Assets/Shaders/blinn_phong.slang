// blinn_phong.slang

#include "../../Source/ShaderDefines.hpp"

struct FragmentStageInput
{ 
    float4 Color;
    float3 Normal;
    float2 UV;
    float3 WorldPos;
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

    const float4 worldPos = mul(u_PC.ModelMatrix, float4(u_PC.VtxPositions[vertexID].Position, 1.0));

    output.FSInput.Color = u_PC.VtxAttributes[vertexID].Color;
    output.FSInput.Normal = u_PC.VtxAttributes[vertexID].Normal;
    output.FSInput.UV = u_PC.VtxAttributes[vertexID].UV;
    output.FSInput.WorldPos = worldPos.xyz;

    output.sv_position = mul(u_PC.CameraData->ViewProjectionMatrix, worldPos);
    return output;
}

[shader("fragment")]
float4 fragmentMain(FragmentStageInput fsInput: FragmentStageInput) : SV_Target
{
    const float3 sunColor = float3(0.8f, 0.5f, 0.1f);
    const float3 N = normalize(fsInput.Normal);
    const float3 L = normalize(float3(0.5f, 5.0f, 6.0f));
    const float NdotL = max(dot(N, L), 0.f);

    const float3 V = normalize(u_PC.CameraData->Position - fsInput.WorldPos);
    const float3 H = normalize(V + L);

    Shaders::GLTFMaterial *materialData = u_PC.MaterialData;
    const float4 albedo = Shaders::Texture_Heap[materialData->PbrData.AlbedoTextureID].Sample(fsInput.UV);
    const float ao = Shaders::Texture_Heap[materialData->OcclusionTextureID].Sample(fsInput.UV).r * materialData->OcclusionStrength;

    float3 ambient = albedo.xyz * ao * 0.08f;
    float3 diffuse = albedo.xyz * NdotL * sunColor;

    const float specFactor = pow(max(dot(V, H), 0.0), 128);
    const float3 specular = sunColor * specFactor * (1.f - NdotL);

    return float4(ambient + diffuse + specular, albedo.a);
}