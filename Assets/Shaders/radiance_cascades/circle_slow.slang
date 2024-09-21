// circle.slang

struct VSOutput
{
    float3 localPos;
    float4 sv_position : SV_Position;
};

struct PushConstantBlock
{
    const Shaders::CameraData* CameraData;
};
[vk::push_constant] PushConstantBlock u_PC;

// https://www.youtube.com/watch?v=VEnglRKNHjU
static const float2 g_TriToCircleVertices[3] = { float2(0.0f, 2.0f), float2(1.7321f, -1.0f), float2(-1.7321f, -1.0f) };

[shader("vertex")]
VSOutput vertexMain(const uint32_t vertexID : SV_VertexID)
{
    VSOutput output;
    output.localPos = float3(g_TriToCircleVertices[vertexID], 0.0f);
    const float3 worldPos = localPos * instance.radius + instance.position;

    output.sv_position = mul(u_PC.CameraData->ViewProjectionMatrix, float4(worldPos, 1.0f));
    return output;
}



[shader("fragment")]
float4 fragmentMain(VSOutput fsInput : VSOutput) : SV_Target
{
    if(dot(fsInput.localPos, fsInput.localPos) > 1.0f) discard;

}
