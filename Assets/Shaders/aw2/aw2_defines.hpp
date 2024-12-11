#pragma once

#include "../../../Source/ShaderDefines.hpp"

#ifdef __cplusplus
namespace Radiant
{

    using uint32_t = unsigned int;
    using int32_t  = signed int;

    using uint16_t = unsigned short int;
    using int16_t  = signed short int;

    using uint8_t = unsigned char;
    using int8_t  = signed char;

    //    using mat4x4 = glm::mat4;

#endif

    namespace Shaders
    {
// Keep it simple stupid, please.

// remove it?
#if 0
        struct ObjectInstanceData
        {
            float3 translation;
            float3 scale;
            // x - real part, yzw - imaginary part.
            // TODO: on c++ side convert from range[-1. 1] to [0,1] (*0.5 + 0.5) and then halfPackUnorm
            // unpacking *2-1
#ifdef __cplusplus
            u16vec4 orientation;
#else
            half4 orientation;
#endif
        };
#endif

        // Geometry passes:
        // skinning -> depth pre pass -> shadow maps -> visbuf

        namespace AW2
        {

            struct BindlessTexture
            {
                uint32_t textureID;
                uint16_t samplerID;  // Currently supported up to s_MAX_BINDLESS_SAMPLERS(which is 1024 as for 07.12.2024)
            };

            // NOTE: Struct is based on glTF-2.0;
            // TODO: Quantize it.
            struct GPUMaterial
            {
                float4 baseColor;  // = 1.0f
                float metallic;    // = 1.0f
                float roughness;   // = 1.0f
                BindlessTexture albedoTexture;
                BindlessTexture metallicRoughnessTexture;  // 'B' - metallic, G - 'roughness'
                BindlessTexture normalTexture;
                BindlessTexture occlusionTexture;

                /* KHR_materials_emissive_strength */
                BindlessTexture emissiveTexture;
                float3 emissive;         // = 0.0f
                float emissiveStrength;  // = 1.0f
                /* KHR_materials_emissive_strength */

                /* KHR_materials_ior */
                float ior;  // 1.5f
                /* KHR_materials_ior */

                /* KHR_materials_transmission */
                BindlessTexture transmissionTexture;
                float transmission;  // = 0.0f
                /* KHR_materials_transmission */

                /* KHR_materials_specular */
                BindlessTexture specularTexture;
                float specular;        // = 1.0f
                float3 specularColor;  // = 1.0f
                BindlessTexture specularColorTexture;
                /* KHR_materials_specular */

                /* KHR_materials_sheen */
                BindlessTexture sheenColorTexture;
                float sheenRoughness;  // = 0.0f
                float3 sheenColor;     // = 0.0f
                BindlessTexture sheenRoughnessTexture;
                /* KHR_materials_sheen */

                /* KHR_materials_iridescence */
                float iridescence;  // = 0.0f
                BindlessTexture iridescenceTexture;
                float iridescenceIor;               // = 1.3f;
                float iridescenceThicknessMinimum;  // = 100.0f;
                float iridescenceThicknessMaximum;  // = 400.0f;
                BindlessTexture iridescenceThicknessTexture;
                /* KHR_materials_iridescence */

                float alphaCutoff;                 // = 0.5f
                /* bool */ uint32_t bDoubleSided;  // = false
            };

            // https://burtleburtle.net/bob/hash/integer.html
            static uint32_t hash(uint32_t a)
            {
                a += ~(a << 15);
                a ^= (a >> 10);
                a += (a << 3);
                a ^= (a >> 6);
                a += ~(a << 11);
                a ^= (a >> 16);
                return a;
            }

            /*      struct CameraData
                  {
                      mat4x4 viewProj;
                  };*/

        }  // namespace AW2

        // Meshes(group of meshlets) -> meshlets(small group of vertices/primitives).
#define MESHLET_MAX_VTX_COUNT 64u
#define MESHLET_MAX_TRI_COUNT 64u
#define MESHLET_CONE_WEIGHT 0.0f

#define MESHLET_WG_SIZE MESHLET_MAX_VTX_COUNT  // Thread per vertex

        // uint8_t is enough cuz even on rtx4090, maxMeshOutputVertices = 256, maxMeshOutputPrimitives = 256.
        struct MeshletMainData
        {
            uint32_t vertexOffset;
            uint32_t triangleOffset;
            uint8_t vertexCount;
            uint8_t triangleCount;
        };

        struct MeshletCullData
        {
            /* bounding sphere, useful for frustum and occlusion culling */
            Sphere sphere;

            /* normal cone, useful for backface culling */
            float coneApex[3];

            /* normal cone axis and cutoff, stored in 8-bit SNORM format; decode using x/127.0 */
            int8_t coneAxisS8[3];
            int8_t coneCutoffS8;
        };

#if 0
        // We have 1 global instance buffer across the renderer,
        // I don't like storing geometryID(wasting 4/8 bytes) this instance refers to, but otherwise its
        // tricky to handle.
        // TODO: compact <float4 orientation> into half4, since quat is unit length.
        struct MeshInstanceData
        {
            float3 translation;
            float4 orientation;  // quat: x - angle, yzw - vector
            float3 scale;
            uint32_t geometryID;  // meshID
        };

        struct SceneData
        {
            uint32_t geometryCount;
            uint32_t instanceCount;
            uint32_t materialCount;
            const MeshGeometryData* geometryData;
            const MeshInstanceData* instanceData;
            const AW2::GPUMaterial* materialData;
        };

        struct DrawSetBucket
        {
            uint32_t instanceCount;
            const MeshInstanceData* instanceData;
        };

        // Per view.
        struct DrawSetCollection
        {
            DrawSetBucket opaqueDrawSet;
            DrawSetBucket alphaTestDrawSet;
        };
#endif

        struct MeshData
        {
            Sphere sphere;
            uint32_t meshletCount;
            const float3* positions;
            const u32* meshletVertices;
            const u8* meshletTriangles;
            const MeshletMainData* meshletMainData;
            const MeshletCullData* meshletCullData;
        };

        struct DrawMeshTasksIndirectCommand
        {
            uint32_t groupCountX;
            uint32_t groupCountY;
            uint32_t groupCountZ;
        };

        struct DispatchIndirectCommand
        {
            uint32_t x;
            uint32_t y;
            uint32_t z;
        };

        struct DrawMeshTasksIndirectCountBuffer
        {
            uint32_t count;
            DrawMeshTasksIndirectCommand* commands;
        };

        // Indirect rendering done via 1 buffer for both drawCount and drawParams.
        // const uint32_t maxDrawCount = scene.mesh.count() or std::numeric_limits<uint32_t>::max().
        // actual drawCount is min(maxDrawCount, count from draw buffer).
        // const uint32_t drawParamStrideSize = sizeof(DrawMeshTasksIndirectCommand);
        // vkCmdDrawMeshTasksIndirectCountEXT(cmd, drawBuffer, sizeof(uint32_t), drawBuffer,/* draw count buffer offset */ 0, maxDrawCount,
        // drawParamStrideSize);

    }  // namespace Shaders

#ifdef __cplusplus
}
#endif
