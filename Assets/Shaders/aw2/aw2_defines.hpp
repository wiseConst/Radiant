#pragma once

#ifdef __cplusplus
namespace Radiant
{

#endif

    namespace Shaders
    {
        // Keep it simple stupid, please.

#define HZB_WG_SIZE 32u
#define HZB_MIP_COUNT 16u  // up to 65k viewport size

#define MAX_LOD_LEVEL 10u

        struct MeshGeometryData
        {
        };

#define MESHLET_WG_SIZE 64u

        // Meshes(group of meshlets) -> meshlets(small group of vertices/primitives).
        static constexpr uint32_t s_MaxMeshletVertexCount   = MESHLET_WG_SIZE;  // Thread per vertex
        static constexpr uint32_t s_MaxMeshletTriangleCount = 126;
        static constexpr float s_MeshletCullConeWeight      = 0.5f;

        // why the fk vtx/tri count is uint32_t if I wanna store at maximum 256 things, so uint8_t is enough buddy
        struct MeshletMainData
        {
            uint32_t VertexOffset;
            uint32_t TriangleOffset;
            uint32_t VertexCount;
            uint32_t TriangleCount;
        };

        struct MeshletCullData
        {
            /* bounding sphere, useful for frustum and occlusion culling */
            Sphere sphere;

            /* normal cone, useful for backface culling */
            float cone_apex[3];
            float cone_axis[3];
            float cone_cutoff; /* = cos(angle/2) */

            /* normal cone axis and cutoff, stored in 8-bit SNORM format; decode using x/127.0 */
            int8_t cone_axis_s8[3];
            int8_t cone_cutoff_s8;
        };

        // We have 1 global instance buffer across the renderer,
        // I don't like storing geometryID(wasting 4/8 bytes) this instance refers to, but otherwise its
        // tricky to handle.
        struct MeshInstanceData
        {
            float3 Translation;
            float4 Orientation;  // x - angle, yzw - vector
            float3 Scale;
            uint32_t GeometryID;
        };

        struct SceneData
        {
            const MeshGeometryData* geometryData;
            const MeshInstanceData* instanceData;
        };

    }  // namespace Shaders

#ifdef __cplusplus
}
#endif
