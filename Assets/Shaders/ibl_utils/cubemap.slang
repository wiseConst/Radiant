// cubemap.slang

struct EquirectangularToCubemapShaderData
{
    float4x4 CaptureViewMatrices[6];
    float4x4 ProjectionMatrix;
};

struct PushConstantBlock
{
    const EquirectangularToCubemapShaderData *ETCSData;
    uint SrcTextureID;
};
[[vk::push_constant]] PushConstantBlock u_PC;

struct VSOutput
{
    float3 LocalPos;
    float4 sv_position : SV_Position;
    uint sv_layer_index : SV_RenderTargetArrayIndex;
};

// Indices for the cube (total 36 indices)
static uint8_t g_CubeIndices[36] = {
    0,  1,  2,  0,  2,  3,  // Back face
    4,  5,  6,  4,  6,  7,  // Front face
    8,  9,  10, 8,  10, 11, // Left face
    12, 13, 14, 12, 14, 15, // Right face
    16, 17, 18, 16, 18, 19, // Top face
    20, 21, 22, 20, 22, 23  // Bottom face
};

static half3 g_UnitCubeVertices[24] = {
    half3(-0.5hf, -0.5hf, -0.5hf), // Back face
    half3(0.5hf, -0.5hf, -0.5hf),
    half3(0.5hf, 0.5hf, -0.5hf),
    half3(-0.5hf, 0.5hf, -0.5hf),

    half3(-0.5hf, -0.5hf, 0.5hf), // Front face
    half3(0.5hf, -0.5hf, 0.5hf),
    half3(0.5hf, 0.5hf, 0.5hf),
    half3(-0.5hf, 0.5hf, 0.5hf),

    half3(-0.5hf, -0.5hf, -0.5hf), // Left face
    half3(-0.5hf, -0.5hf, 0.5hf),
    half3(-0.5hf, 0.5hf, 0.5hf),
    half3(-0.5hf, 0.5hf, -0.5hf),

    half3(0.5hf, -0.5hf, -0.5hf), // Right face
    half3(0.5hf, -0.5hf, 0.5hf),
    half3(0.5hf, 0.5hf, 0.5hf),
    half3(0.5hf, 0.5hf, -0.5hf),

    half3(-0.5hf, 0.5hf, -0.5hf), // Top face
    half3(0.5hf, 0.5hf, -0.5hf),
    half3(0.5hf, 0.5hf, 0.5hf),
    half3(-0.5hf, 0.5hf, 0.5hf),

    half3(-0.5hf, -0.5hf, -0.5hf), // Bottom face
    half3(0.5hf, -0.5hf, -0.5hf),
    half3(0.5hf, -0.5hf, 0.5hf),
    half3(-0.5hf, -0.5hf,  0.5hf),
};

// Cube instance rendering 6 times, each time view is rotated 90 degrees * InstanceID.

[shader("vertex")]
VSOutput vertexMain(const uint vtxID : SV_VertexID, const uint instanceID : SV_InstanceID)
{
    const float3 localPos = g_UnitCubeVertices[vtxID];

    // Remove translation from view matrix
    float4x4 currentView = u_PC.ETCSData.CaptureViewMatrices[instanceID];
    // currentView[0][3] = 0.0f;
    // currentView[1][3] = 0.0f;
    // currentView[2][3] = 0.0f;
    currentView[3][0] = 0.0f;
    currentView[3][1] = 0.0f;
    currentView[3][2] = 0.0f;
    return VSOutput(localPos, mul(u_PC.ETCSData.ProjectionMatrix, mul(currentView, float4(localPos, 1.0f))), instanceID);

    //const float3 localPos = g_UnitCubeVertices[vtxID];

    //float4x4 currentView = u_PC.ETCSData.CaptureViewMatrices[instanceID];
    //float3x3 rotViewMatrixMat3 = transpose(float3x3(currentView[0].xyz, currentView[1].xyz, currentView[2].xyz));
    //float4x4 rotViewMatrixMat4 = transpose(float4x4(float4(rotViewMatrixMat3[0].xyz, 0.0f), float4(rotViewMatrixMat3[1].xyz, 0.0f), float4(rotViewMatrixMat3[2].xyz, 0.0f), float4(0.0f, 0.0f, 0.0f, 0.0f)));
    //return VSOutput(localPos, mul(u_PC.ETCSData.ProjectionMatrix, mul(rotViewMatrixMat4, float4(localPos, 1.0f))), instanceID);
}