// cubemap.slang

#include <../../../Source/ShaderDefines.hpp>

struct EquirectangularToCubemapShaderData
{
    float4x4 CaptureViewMatrices[6];
    float4x4 ProjectionMatrix;
};

struct PushConstantBlock
{
    const EquirectangularToCubemapShaderData *ETCSData;
    uint SrcTextureID;
    float Data0; // used for roughness when prefiltering.
    float Data1; // used as cubemap res rcp when prefiltering.
};
[[vk::push_constant]] PushConstantBlock u_PC;

struct VSOutput
{
    float3 LocalPos;
    float4 sv_position : SV_Position;
    uint sv_layer_index : SV_RenderTargetArrayIndex;
};

// Cube instance rendering 6 times, each time view is rotated 90 degrees * InstanceID.

[shader("vertex")]
VSOutput vertexMain(const uint vtxID : SV_VertexID, const uint instanceID : SV_InstanceID)
{
    const float3 localPos = Shaders::g_UnitCubeVertices[vtxID];

    float4x4 currentView = u_PC.ETCSData.CaptureViewMatrices[instanceID]; // Slang"ish" Row-Column access, getting rid of translation.
    currentView[3][0] = 0.0f;
    currentView[3][1] = 0.0f;
    currentView[3][2] = 0.0f;

    const float4 clipPos = mul(u_PC.ETCSData.ProjectionMatrix, mul(currentView, float4(localPos, 1.0f)));
    return VSOutput(localPos, clipPos, instanceID);
}