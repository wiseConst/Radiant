// triangle_mesh_shader.slang

#include <../../../Source/ShaderDefines.hpp>

struct PushConstantBlock
{
    const Shaders::CameraData *CameraData;
};
[vk::push_constant] PushConstantBlock u_PC;

struct DummyPayLoad
{
    uint Dummy;
};

// We don't use pay loads in this sample, but the fn call requires one
groupshared DummyPayLoad gs_DummyPayload;

[numthreads(1, 1, 1)]
[shader("amplification")]
void amplificationMain()
{
    DispatchMesh(1, 1, 1, gs_DummyPayload);
}

struct VertexOutput
{
    float4 sv_position : SV_Position;
    float4 color;
};

// vulkan ndc
static const float4 positions[3] = {
    float4(0.0, -1.0, 0.0, 1.0),
    float4(-1.0, 1.0, 0.0, 1.0),
    float4(1.0,  1.0, 0.0, 1.0)
};

static const float4 colors[3] = {
    float4(0.0, 1.0, 0.0, 1.0),
    float4(0.0, 0.0, 1.0, 1.0),
    float4(1.0, 0.0, 0.0, 1.0)
};

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
[shader("mesh")]
void meshMain(out indices uint3 triangles[1], out vertices VertexOutput vertices[3]
//, in payload DummyPayLoad payload
, const uint3 DTid: SV_DispatchThreadID
, const uint3 GTid: SV_GroupThreadID
)
{
    SetMeshOutputCounts(3, 1);

    triangles[0] = uint3(0, 1, 2);
    for (uint i = 0; i < 3; i++) {
        vertices[i].sv_position = mul(u_PC.CameraData.ViewProjectionMatrix, positions[i] + float4(0, 0, -2, 0));
        vertices[i].color = colors[i];
    }
}

[shader("fragment")]
float4 fragmentMain(VertexOutput fsInput)
{
    return float4(fsInput.color);
}