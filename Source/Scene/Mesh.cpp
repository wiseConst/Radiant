#include <pch.h>
#include "Mesh.hpp"

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/core.hpp>

#include <Render/GfxContext.hpp>

namespace Radiant
{

    namespace FastGltfUtils
    {
        NODISCARD static vk::Filter FastGltfFilterToVulkan(const fastgltf::Filter filter)
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

        NODISCARD static vk::SamplerMipmapMode FastGltfMipMapModeToVulkan(const fastgltf::Filter filter)
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

    }  // namespace FastGltfUtils

    StaticMesh::StaticMesh(const Unique<GfxContext>& gfxContext, const std::filesystem::path& meshFilePath) noexcept
    {
        constexpr auto gltfLoadOptions =
            fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers | fastgltf::Options::GenerateMeshIndices;

        auto gltfFile = fastgltf::MappedGltfFile::FromPath(meshFilePath);
        RDNT_ASSERT(gltfFile.error() == fastgltf::Error::None, "fastgltf: failed to open glTF file: {}",
                    fastgltf::getErrorMessage(gltfFile.error()));

        fastgltf::Parser parser;
        auto asset = parser.loadGltf(gltfFile.get(), meshFilePath.parent_path(), gltfLoadOptions);
        RDNT_ASSERT(asset.error() == fastgltf::Error::None, "fastgltf: failed to load glTF file: {}",
                    fastgltf::getErrorMessage(asset.error()));

        // use the same vectors for all meshes so that the memory doesnt reallocate as
        // often
        std::vector<std::uint32_t> indices;
        std::vector<VertexPosition> vertexPositions;
        std::vector<VertexAttribute> vertexAttributes;
        LOG_INFO("Loading mesh: {}", meshFilePath.string());
        for (auto& mesh : asset->meshes)
        {
            LOG_INFO("Loading submesh: {}", mesh.name);

            indices.clear();
            vertexPositions.clear();
            vertexAttributes.clear();
            for (auto primIt = mesh.primitives.cbegin(); primIt != mesh.primitives.cend(); ++primIt)
            {
                auto* positionIt = primIt->findAttribute("POSITION");
                RDNT_ASSERT(positionIt != primIt->attributes.end(),
                            "fastgltf: A mesh primitive is required to hold the POSITION attribute.");
                RDNT_ASSERT(primIt->indicesAccessor.has_value(),
                            "fastgltf: We specify GenerateMeshIndices, so we should always have indices.");

                // Loading indices
                {
                    auto& indicesAccessor = asset->accessors[primIt->indicesAccessor.value()];
                    indices.reserve(indices.size() + indicesAccessor.count);

                    fastgltf::iterateAccessor<std::uint32_t>(asset.get(), indicesAccessor,
                                                             [&](std::uint32_t idx) { indices.emplace_back(idx); });
                }

                // Load vertex positions
                {
                    auto& posAccessor = asset->accessors[positionIt->accessorIndex];
                    vertexPositions.resize(posAccessor.count);
                    vertexAttributes.resize(posAccessor.count, VertexAttribute{.Normal{0, 0, 0}, .Color{1.0f, 1.0f, 1.0f, 1.0f}});

                    fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), posAccessor,
                                                                  [&](const glm::vec3& v, const std::size_t index)
                                                                  { vertexPositions[index].Position = v; });
                }

                // Load vertex attributes
                {
                    // 1. Normals

                    if (auto* normalsAttribute = primIt->findAttribute("NORMAL"); normalsAttribute != primIt->attributes.end())
                    {

                        fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), asset->accessors[normalsAttribute->accessorIndex],
                                                                      [&](const glm::vec3& n, const std::size_t index)
                                                                      { vertexAttributes[index].Normal = n; });
                    }

                    // 2. Vertex colors

                    if (auto* colorsAttribute = primIt->findAttribute("COLOR_0"); colorsAttribute != primIt->attributes.end())
                    {
                        fastgltf::iterateAccessorWithIndex<glm::vec4>(asset.get(), asset->accessors[colorsAttribute->accessorIndex],
                                                                      [&](const glm::vec4& vertexColor, const std::size_t index)
                                                                      { vertexAttributes[index].Color = vertexColor; });
                    }
                }

                auto& submesh = Submeshes.emplace_back();

                const auto& device       = gfxContext->GetDevice();
                auto loaderCommandBuffer = gfxContext->AllocateTransferCommandBuffer();
                loaderCommandBuffer->begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

                vk::Buffer vpbScratch{};
                VmaAllocation vpbScratchAllocation{};
                {
                    const auto bufferCI = vk::BufferCreateInfo()
                                              .setSharingMode(vk::SharingMode::eExclusive)
                                              .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
                                              .setSize(vertexPositions.size() * sizeof(vertexPositions[0]));
                    device->AllocateBuffer(EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED, bufferCI, *(VkBuffer*)&vpbScratch,
                                           vpbScratchAllocation);
                }
                void* vpbHostMemory = device->Map(vpbScratchAllocation);
                std::memcpy(vpbHostMemory, vertexPositions.data(), vertexPositions.size() * sizeof(vertexPositions[0]));
                device->Unmap(vpbScratchAllocation);

                const GfxBufferDescription vpb = {.Capacity    = vertexPositions.size() * sizeof(vertexPositions[0]),
                                                  .ElementSize = sizeof(vertexPositions[0]),
                                                  .UsageFlags  = vk::BufferUsageFlagBits::eStorageBuffer,
                                                  .ExtraFlags  = EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL};
                submesh.VertexPosBuffer        = MakeUnique<GfxBuffer>(gfxContext->GetDevice(), vpb);

                loaderCommandBuffer->copyBuffer(vpbScratch, *submesh.VertexPosBuffer,
                                                vk::BufferCopy().setSize(vertexPositions.size() * sizeof(vertexPositions[0])));

                vk::Buffer vabScratch{};
                VmaAllocation vabScratchAllocation{};
                {
                    const auto bufferCI = vk::BufferCreateInfo()
                                              .setSharingMode(vk::SharingMode::eExclusive)
                                              .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
                                              .setSize(vertexAttributes.size() * sizeof(vertexAttributes[0]));
                    device->AllocateBuffer(EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED, bufferCI, *(VkBuffer*)&vabScratch,
                                           vabScratchAllocation);
                }
                void* vabHostMemory = device->Map(vabScratchAllocation);
                std::memcpy(vabHostMemory, vertexAttributes.data(), vertexAttributes.size() * sizeof(vertexAttributes[0]));
                device->Unmap(vabScratchAllocation);

                const GfxBufferDescription vab = {.Capacity    = vertexAttributes.size() * sizeof(vertexAttributes[0]),
                                                  .ElementSize = sizeof(vertexAttributes[0]),
                                                  .UsageFlags  = vk::BufferUsageFlagBits::eStorageBuffer,
                                                  .ExtraFlags  = EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL};
                submesh.VertexAttribBuffer     = MakeUnique<GfxBuffer>(gfxContext->GetDevice(), vab);

                loaderCommandBuffer->copyBuffer(vabScratch, *submesh.VertexAttribBuffer,
                                                vk::BufferCopy().setSize(vertexAttributes.size() * sizeof(vertexAttributes[0])));

                vk::Buffer ibScratch{};
                VmaAllocation ibScratchAllocation{};
                {
                    const auto bufferCI = vk::BufferCreateInfo()
                                              .setSharingMode(vk::SharingMode::eExclusive)
                                              .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
                                              .setSize(indices.size() * sizeof(indices[0]));
                    device->AllocateBuffer(EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED, bufferCI, *(VkBuffer*)&ibScratch,
                                           ibScratchAllocation);
                }
                void* ibHostMemory = device->Map(ibScratchAllocation);
                std::memcpy(ibHostMemory, indices.data(), indices.size() * sizeof(indices[0]));
                device->Unmap(ibScratchAllocation);

                const GfxBufferDescription ib = {.Capacity    = indices.size() * sizeof(indices[0]),
                                                 .ElementSize = sizeof(indices[0]),
                                                 .UsageFlags =
                                                     vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndexBuffer,
                                                 .ExtraFlags = EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL};
                submesh.IndexBuffer           = MakeUnique<GfxBuffer>(gfxContext->GetDevice(), ib);

                loaderCommandBuffer->copyBuffer(ibScratch, *submesh.IndexBuffer,
                                                vk::BufferCopy().setSize(indices.size() * sizeof(indices[0])));

                loaderCommandBuffer->end();
                gfxContext->GetDevice()->GetTransferQueue().Handle.submit(vk::SubmitInfo().setCommandBuffers(*loaderCommandBuffer));
                gfxContext->GetDevice()->GetTransferQueue().Handle.waitIdle();

                device->DeallocateBuffer(*(VkBuffer*)&vpbScratch, vpbScratchAllocation);
                device->DeallocateBuffer(*(VkBuffer*)&vabScratch, vabScratchAllocation);
                device->DeallocateBuffer(*(VkBuffer*)&ibScratch, ibScratchAllocation);
            }
        }
    }

}  // namespace Radiant
