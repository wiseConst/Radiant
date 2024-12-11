// shade_main.slang

#include <../../../Source/ShaderDefines.hpp>
#include <aw2_defines.hpp>

struct PushConstantBlock
{
   // const Shaders::AW2::CameraData *cameraData;
    const Shaders::AW2::MeshData *meshData;
};
[[vk::push_constant]] PushConstantBlock u_PC;

#if 0
[numthreads(MESHLET_WG_SIZE, 1, 1)]
[shader("amplification")]
void amplificationMain()
{

}
#endif

struct VertexOutput
{
    float4 sv_position : SV_Position;
    float3 normal;
};

[outputtopology("triangle")]
[numthreads(MESHLET_WG_SIZE, 1, 1)]
[shader("mesh")]
void meshMain(
const uint groupIndex : SV_GroupIndex, 
const uint3 Gid : SV_GroupID,
out indices uint3 tris[MESHLET_MAX_TRI_COUNT], 
out vertices VertexOutput[MESHLET_MAX_VTX_COUNT] verts)
{
    const Shaders::AW2::MeshletMainData meshletData = u_PC.meshData.meshletMainData[Gid];

    SetMeshOutputCounts(meshletData.vertexCount, meshletData.triangleCount);

    for (uint i = groupIndex; i < meshletData.vertexCount; i += MESHLET_WG_SIZE)
    {
        const uint idx = meshlet_vertices[meshletData.vertexOffset + i];
        verts[i].sv_position = 
    }

    for (uint i = groupIndex; i < meshletData.triangleCount; i += MESHLET_WG_SIZE)
    {
        const uint offset = meshletData.triangleOffset + i * 3;
        tris[i] = uint3(meshlet_triangles[offset], meshlet_triangles[offset + 1], meshlet_triangles[offset + 2]);
    }
}

[shader("fragment")]
float4 fragmentMain(VertexOutput fsInput) : SV_Target
{
    const float3 N = normalize(fsInput.normal);
    return float4(N * 0.5f + 0.5f, 1.0f);
}