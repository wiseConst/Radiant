// points2d.slang

#include "../../Source/ShaderDefines.h"
#include <radiance_cascades_defines.hpp>

struct VSOutput
{
    float3 color;
    float2 localPos;
    float4 sv_position : SV_Position;
};

struct PushConstantBlock
{
    const Shaders::CameraData *CameraData;
    const Point2D *Points;
    uint2 FullResolution;
};
[vk::push_constant] PushConstantBlock u_PC;

static const float2 g_TriToCircleVertices[3] = { float2(0.0f, 2.0f), float2(1.7321f, -1.0f), float2(-1.7321f, -1.0f) };

[shader("vertex")]
VSOutput vertexMain(const uint32_t vertexID: SV_VertexID, const uint32_t instanceID : SV_InstanceID)
{
    Point2D p = u_PC.Points[instanceID];

    VSOutput output;
    output.color = p.Color;
    output.localPos = g_TriToCircleVertices[vertexID];
    const float2 instPos = output.localPos * p.Radius + p.Position;
    const float3 worldPos = float3(instPos / u_PC.FullResolution, 0.5f) * 2.f - 1.0f;

    //  output.sv_position = (worldPos, 1.0f);//mul(u_PC.CameraData->ViewProjectionMatrix, float4(worldPos, 1.0f));
    output.sv_position = mul(u_PC.CameraData->ViewProjectionMatrix, float4(instPos, 0.5f, 1.0f));
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    if (dot(fsInput.localPos, fsInput.localPos) > 1.0f) discard;

    return float4(fsInput.color, 1.0f);
}

/*
struct VSOutput
{
    float3 color;
    float radius;
    float2 localPosition;
    float4 sv_position : SV_Position;
    float sv_pointSize : SV_PointSize;
};

struct PushConstantBlock
{
    const Shaders::CameraData *CameraData;
    const Point2D *Points;
    uint2 FullResolution;
};
[vk::push_constant] PushConstantBlock u_PC;

[shader("vertex")]
VSOutput vertexMain(const uint32_t vertexID : SV_VertexID)
{
    Point2D p = u_PC.Points[vertexID];
    
    const float3 worldPos = float3(p.Position, 0.0f);

    float3 convertedPos = float3(p.Position / u_PC.FullResolution, 0.5f) * 2.f - 1.0f;

    VSOutput output;
    output.localPosition = p.Position / u_PC.FullResolution;
    output.radius = p.Radius;
    output.color = p.Color;
    output.sv_pointSize = p.Size;
    output.sv_position = float4(convertedPos, 1.0f);//mul(u_PC.CameraData->ViewProjectionMatrix, float4(worldPos, 1.0f));

    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput fsInput: VSOutput) : SV_Target
{
    float4 outColor = float4(fsInput.color, 1.0f);
    const float dist = distance(fsInput.localPosition, 0.5f);

    if (dist <= fsInput.radius) {
        return outColor;
    } else {
        discard; 
    }
   
}
*/