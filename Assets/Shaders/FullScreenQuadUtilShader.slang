// FullScreenQuadUtilShader.slang

struct VSOutput
{
    float2 uv;
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VSOutput vertexMain(const uint32_t vertexID : SV_VertexID)
{
    VSOutput output;
    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.sv_position = float4(output.uv * 2.0f - 1.0f, 0.0f, 1.0f);

    return output;
}