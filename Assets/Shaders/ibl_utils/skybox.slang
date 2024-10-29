// skybox.slang

#include <../../../Source/ShaderDefines.hpp>

struct PushConstantBlock
{
    const Shaders::CameraData *CameraData;
    uint CubemapTextureID;
};
[[vk::push_constant]] PushConstantBlock u_PC;

struct VSOutput
{
    float3 LocalPos;
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VSOutput vertexMain(const uint vtxID: SV_VertexID)
{
    const float3 localPos = Shaders::g_UnitCubeVertices[vtxID];
    float4x4 rotView = u_PC.CameraData.ViewMatrix; // Slang"ish" Row-Column access, getting rid of translation.
    rotView[0][3] = 0.0f;
    rotView[1][3] = 0.0f;
    rotView[2][3] = 0.0f;

    float4 clipPos = mul(u_PC.CameraData.ProjectionMatrix, mul(rotView, float4(localPos, 1.0f)));
    clipPos.z = 0.0f; // reversed Z, cleared as BLACK color. The whiter the color, the closer the object.
    
    return VSOutput(localPos, clipPos);
}

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    const float3 outColor = Shaders::Texture_Cube_Heap[u_PC.CubemapTextureID].Sample(fsInput.LocalPos).rgb;
    return float4(outColor, 1.0f);
}