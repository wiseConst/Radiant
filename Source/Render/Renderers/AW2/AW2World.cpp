#include "AW2World.hpp"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

#include <meshoptimizer.h>

namespace Radiant
{

    namespace MeshoptimizerUtils
    {
        NODISCARD auto GenerateBoundingSphere(const glm::vec3* vertices, const u64 vtxCount, const AABB aabb) noexcept -> Sphere
        {
            RDNT_ASSERT(vertices && vtxCount > 0, "Vertices are invalid!");

            // First pass - find averaged vertex pos.
            glm::vec3 averagedVertexPos(0.0f);
            for (u64 vtxIdx{}; vtxIdx < vtxCount; ++vtxIdx)
                averagedVertexPos += vertices[vtxIdx];

            averagedVertexPos /= vtxCount;
            const auto aabbCenter = (aabb.Max + aabb.Min) * 0.5f;

            // Second pass - find farthest vertices for both averaged vertex position and AABB centroid.
            glm::vec3 farthestVtx[2] = {vertices[0], vertices[0]};
            for (u64 vtxIdx{}; vtxIdx < vtxCount; ++vtxIdx)
            {
                if (glm::distance2(averagedVertexPos, vertices[vtxIdx]) > glm::distance2(averagedVertexPos, farthestVtx[0]))
                    farthestVtx[0] = vertices[vtxIdx];
                if (glm::distance2(aabbCenter, vertices[vtxIdx]) > glm::distance2(aabbCenter, farthestVtx[1]))
                    farthestVtx[1] = vertices[vtxIdx];
            }

            const f32 averagedVtxToFarthestDistance  = glm::distance(farthestVtx[0], averagedVertexPos);
            const f32 aabbCentroidToFarthestDistance = glm::distance(farthestVtx[1], aabbCenter);

            return Sphere{.Origin = averagedVtxToFarthestDistance < aabbCentroidToFarthestDistance ? averagedVertexPos : aabbCenter,
                          .Radius = glm::min(averagedVtxToFarthestDistance, aabbCentroidToFarthestDistance)};
        }
    }  // namespace MeshoptimizerUtils

    namespace FastGltfUtils
    {

        NODISCARD auto ExtractMipMapMode(const fastgltf::Filter filter) noexcept -> vk::SamplerMipmapMode
        {
            switch (filter)
            {
                case fastgltf::Filter::LinearMipMapLinear:
                case fastgltf::Filter::NearestMipMapLinear: return vk::SamplerMipmapMode::eLinear;

                case fastgltf::Filter::LinearMipMapNearest:
                case fastgltf::Filter::NearestMipMapNearest:
                default: return vk::SamplerMipmapMode::eNearest;
            }
        }

        NODISCARD auto ExtractFilter(const fastgltf::Filter filter) noexcept -> vk::Filter
        {
            switch (filter)
            {
                case fastgltf::Filter::Linear:
                case fastgltf::Filter::LinearMipMapLinear:
                case fastgltf::Filter::LinearMipMapNearest: return vk::Filter::eLinear;

                case fastgltf::Filter::Nearest:
                case fastgltf::Filter::NearestMipMapLinear:
                case fastgltf::Filter::NearestMipMapNearest:
                default: return vk::Filter::eNearest;
            }
        }

        NODISCARD auto ExtractWrap(const fastgltf::Wrap wrap) noexcept -> vk::SamplerAddressMode
        {
            switch (wrap)
            {
                case fastgltf::Wrap::ClampToEdge: return vk::SamplerAddressMode::eClampToEdge;
                case fastgltf::Wrap::MirroredRepeat: return vk::SamplerAddressMode::eMirroredRepeat;
                case fastgltf::Wrap::Repeat:
                default: return vk::SamplerAddressMode::eRepeat;
            }
        }

        NODISCARD auto GetAccessorMinMax(const decltype(fastgltf::Accessor::min)& values) noexcept -> glm::vec3
        {
            return std::visit(
                fastgltf::visitor{
                    [](const auto& arg) { return glm::vec3(); },
                    [&](const FASTGLTF_STD_PMR_NS::vector<double>& values)
                    {
                        RDNT_ASSERT(values.size() == 3, "Min/max  component count isn't 3.");
                        return glm::fvec3(values[0], values[1], values[2]);
                    },
                    [&](const FASTGLTF_STD_PMR_NS::vector<std::int64_t>& values)
                    {
                        RDNT_ASSERT(values.size() == 3, "Min/max  component count isn't 3.");
                        return glm::fvec3(values[0], values[1], values[2]);
                    },
                },
                values);
        }

        NODISCARD auto ExtractAlphaMode(const fastgltf::AlphaMode alphaMode) noexcept -> EAlphaMode
        {
            switch (alphaMode)
            {
                case fastgltf::AlphaMode::Mask: return EAlphaMode::ALPHA_MODE_MASK;
                case fastgltf::AlphaMode::Blend: return EAlphaMode::ALPHA_MODE_BLEND;
                case fastgltf::AlphaMode::Opaque:
                default: return EAlphaMode::ALPHA_MODE_OPAQUE;
            }
        }

    }  // namespace FastGltfUtils

    void AW2World::LoadScene(const Unique<GfxContext>& gfxContext, const std::filesystem::path& scenePath) noexcept
    {
        auto maybeAsset = [&]()
        {
            constexpr auto gltfExtensions = fastgltf::Extensions::KHR_materials_ior | fastgltf::Extensions::KHR_materials_clearcoat |
                                            fastgltf::Extensions::KHR_materials_sheen | fastgltf::Extensions::KHR_materials_specular |
                                            fastgltf::Extensions::KHR_materials_iridescence |
                                            fastgltf::Extensions::KHR_materials_transmission |
                                            fastgltf::Extensions::KHR_materials_emissive_strength;
            auto parser = fastgltf::Parser(gltfExtensions);

            auto gltfDataBuffer = fastgltf::GltfDataBuffer::FromPath(scenePath);
            RDNT_ASSERT(gltfDataBuffer.error() == fastgltf::Error::None, "Failed to load gltfDataBuffer for: {}", scenePath.string());

            constexpr auto gftfLoadOptions = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::GenerateMeshIndices |
                                             fastgltf::Options::LoadExternalImages | fastgltf::Options::DontRequireValidAssetMember |
                                             fastgltf::Options::DecomposeNodeMatrices;
            return parser.loadGltf(gltfDataBuffer.get(), scenePath.parent_path(), gftfLoadOptions);
        }();

        RDNT_ASSERT(maybeAsset.error() == fastgltf::Error::None, "Failed to load: {}", scenePath.string());

        std::vector<Shaders::AW2::GPUMaterial> materials{};

        auto& aw2_scene = m_Scenes.emplace_back();

        std::vector<u32> indices{};

        // Each of vectors above will have the same size regardless of absence of accessor!
        std::vector<glm::vec3> positions{};
        std::vector<glm::vec4> colors0{};
        std::vector<glm::vec2> uvs0{};
        std::vector<glm::vec3> normals{};
        std::vector<glm::vec4> tangents{};

        const auto& asset = maybeAsset.get();
        for (const auto& fg_mesh : asset.meshes)
        {
            positions.clear();
            colors0.clear();
            uvs0.clear();
            normals.clear();
            tangents.clear();
            indices.clear();

            auto& aw2_meshNode = aw2_scene.AllNodes.emplace_back();

            for (const auto& fg_primitive : fg_mesh.primitives)
            {
                RDNT_ASSERT(fg_primitive.indicesAccessor.has_value(), "Non-indexed geometry isn't supported!");
                RDNT_ASSERT(fg_primitive.type == fastgltf::PrimitiveType::Triangles,
                            "Primitive topology other than <Triangles> isn't supported!");

                auto& aw2_primitive       = aw2_meshNode.Primitives.emplace_back();
                aw2_primitive.IndexOffset = indices.size();

                // TODO: if material isn't present should be defaulted.
                aw2_primitive.MaterialID = fg_primitive.materialIndex.value_or(0);
                const auto& fg_material  = asset.materials[aw2_primitive.MaterialID];

                aw2_primitive.AlphaMode = FastGltfUtils::ExtractAlphaMode(fg_material.alphaMode);
                aw2_primitive.CullMode  = fg_material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;

                const auto initialVtxVectorSize{positions.size()};

                // Indices
                {
                    const auto& indexAccessor = asset.accessors[fg_primitive.indicesAccessor.value()];
                    aw2_primitive.IndexCount  = indexAccessor.count;

                    const auto initialIndexVectorSize = indices.size();
                    indices.resize(indices.size() + aw2_primitive.IndexCount);

                    fastgltf::iterateAccessorWithIndex<u32>(asset, indexAccessor,
                                                            [&](const u32 index, const u64 idx)
                                                            {
                                                                indices[initialIndexVectorSize + idx] =
                                                                    initialVtxVectorSize +
                                                                    index;  // real index + offset inside positions vector
                                                            });
                }

                // Positions
                {
                    const auto& it = fg_primitive.findAttribute("POSITION");
                    RDNT_ASSERT(it != fg_primitive.attributes.end(), "Primitive doesn't have positions!");

                    const auto& posAccessor = asset.accessors[it->accessorIndex];
                    positions.resize(initialVtxVectorSize + posAccessor.count);

                    fastgltf::iterateAccessorWithIndex<glm::vec3>(
                        asset, posAccessor, [&](const glm::vec3& v, const u64 idx) { positions[initialVtxVectorSize + idx] = v; });

                    aw2_primitive.BoundingSphere =
                        MeshoptimizerUtils::GenerateBoundingSphere(&positions[initialVtxVectorSize], posAccessor.count,
                                                                   AABB{.Min = FastGltfUtils::GetAccessorMinMax(posAccessor.min),
                                                                        .Max = FastGltfUtils::GetAccessorMinMax(posAccessor.max)});
                }

                const auto currentVtxCount = positions.size() - initialVtxVectorSize;

                // UVs0
                {
                    uvs0.resize(uvs0.size() + currentVtxCount, glm::vec2{0.0f});
                    if (const auto& it = fg_primitive.findAttribute("TEXCOORD_0"); it != fg_primitive.attributes.end())
                    {
                        const auto uvs0Accessor = asset.accessors[it->accessorIndex];

                        fastgltf::iterateAccessorWithIndex<glm::vec2>(
                            asset, uvs0Accessor, [&](const glm::vec2& uv0, const u64 idx) { uvs0[initialVtxVectorSize + idx] = uv0; });
                    }
                }

                // COLORs0
                {
                    colors0.resize(colors0.size() + currentVtxCount, glm::vec4{1.0f});
                    if (const auto& it = fg_primitive.findAttribute("COLOR_0"); it != fg_primitive.attributes.end())
                    {
                        const auto colors0Accessor = asset.accessors[it->accessorIndex];

                        fastgltf::iterateAccessorWithIndex<glm::vec4>(asset, colors0Accessor,
                                                                      [&](const glm::vec4& color0, const u64 idx)
                                                                      { colors0[initialVtxVectorSize + idx] = color0; });
                    }
                }

                // Normals
                {
                    normals.resize(normals.size() + currentVtxCount, glm::vec3{0.0f});
                    if (const auto& it = fg_primitive.findAttribute("NORMAL"); it != fg_primitive.attributes.end())
                    {
                        const auto& normalsAccessor = asset.accessors[it->accessorIndex];

                        fastgltf::iterateAccessorWithIndex<glm::vec3>(
                            asset, normalsAccessor, [&](const glm::vec3& n, const u64 idx) { normals[initialVtxVectorSize + idx] = n; });
                    }
                }

                // Tangents
                {
                    tangents.resize(tangents.size() + currentVtxCount, glm::vec4{0.0f});
                    if (const auto& it = fg_primitive.findAttribute("TANGENT"); it != fg_primitive.attributes.end())
                    {
                        const auto& tangentsAccessor = asset.accessors[it->accessorIndex];

                        fastgltf::iterateAccessorWithIndex<glm::vec4>(
                            asset, tangentsAccessor, [&](const glm::vec4& t, const u64 idx) { tangents[initialVtxVectorSize + idx] = t; });
                    }
                }
            }

            const auto maxMeshletCount = meshopt_buildMeshletsBound(indices.size(), MESHLET_MAX_VTX_COUNT, MESHLET_MAX_TRI_COUNT);
            std::vector<meshopt_Meshlet> meshlets(maxMeshletCount);
            std::vector<u32> meshletVertices(maxMeshletCount * MESHLET_MAX_VTX_COUNT);
            std::vector<u8> meshletTriangles(maxMeshletCount * MESHLET_MAX_TRI_COUNT * 3);

            const auto actualMeshletCount = meshopt_buildMeshlets(
                meshlets.data(), meshletVertices.data(), meshletTriangles.data(), indices.data(), indices.size(), &positions[0].x,
                positions.size(), sizeof(glm::vec3), MESHLET_MAX_VTX_COUNT, MESHLET_MAX_TRI_COUNT, MESHLET_CONE_WEIGHT);

            const meshopt_Meshlet& last = meshlets[actualMeshletCount - 1];

            meshletVertices.resize(last.vertex_offset + last.vertex_count);
            meshletTriangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
            meshlets.resize(actualMeshletCount);

            std::vector<Shaders::MeshletMainData> meshletMainData;
            std::vector<Shaders::MeshletCullData> meshletCullData;
            for (auto& meshopt_meshlet : meshlets)
            {
                meshopt_optimizeMeshlet(&meshletVertices[meshopt_meshlet.vertex_offset], &meshletTriangles[meshopt_meshlet.triangle_offset],
                                        meshopt_meshlet.triangle_count, meshopt_meshlet.vertex_count);

                meshletMainData.emplace_back(meshopt_meshlet.vertex_offset, meshopt_meshlet.triangle_offset, meshopt_meshlet.vertex_count,
                                             meshopt_meshlet.triangle_count);

                const auto meshopt_bounds = meshopt_computeMeshletBounds(
                    &meshletVertices[meshopt_meshlet.vertex_offset], &meshletTriangles[meshopt_meshlet.triangle_offset],
                    meshopt_meshlet.triangle_count, &positions[0].x, positions.size(), sizeof(glm::vec3));

                auto& cullData = meshletCullData.emplace_back();
                std::memcpy(&cullData.sphere.Origin, meshopt_bounds.center, sizeof(cullData.sphere.Origin));
                cullData.sphere.Radius = meshopt_bounds.radius;

                cullData.coneCutoffS8 = meshopt_bounds.cone_cutoff_s8;
                std::memcpy(&cullData.coneAxisS8, meshopt_bounds.cone_axis_s8, sizeof(cullData.coneAxisS8));
                std::memcpy(&cullData.coneApex, meshopt_bounds.cone_apex, sizeof(cullData.coneApex));
            }



        }

        for (const auto& fg_node : asset.nodes)
        {
        }
    }

}  // namespace Radiant
