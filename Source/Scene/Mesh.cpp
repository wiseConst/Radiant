#include <pch.h>
#include "Mesh.hpp"

#include <Core/Application.hpp>

#include <Render/GfxContext.hpp>
#include <Render/GfxTexture.hpp>
#include <Render/GfxBuffer.hpp>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/core.hpp>

#include <meshoptimizer.h>

namespace Radiant
{

    namespace MeshoptimizerUtils
    {

        template <typename T>
        static constexpr void RemapVertexStream(const u64 uniqueVertexCount, std::vector<T>& vertexStream,
                                                std::vector<u32>& indices) noexcept
        {
            std::vector<T> newVertexStream(uniqueVertexCount);
            meshopt_remapVertexBuffer(newVertexStream.data(), vertexStream.data(), vertexStream.size(), sizeof(T), indices.data());
            vertexStream = std::move(newVertexStream);
        }

        static void OptimizeMesh(std::vector<u32>& indices, std::vector<VertexPosition>& vertexPositions,
                                 std::vector<VertexAttribute>& vertexAttributes) noexcept
        {
            RDNT_ASSERT(vertexPositions.size() == vertexAttributes.size(),
                        "VertexPositions size should be equal to VertexAttributes size!");
            RDNT_ASSERT(!indices.empty() || !vertexPositions.empty() || !vertexAttributes.empty(), "Input params are empty!");

            const std::vector<meshopt_Stream> streams = {
                {vertexPositions.data(), sizeof(vertexPositions[0]), sizeof(vertexPositions[0])},
                {vertexAttributes.data(), sizeof(vertexAttributes[0]), sizeof(vertexAttributes[0])},
            };

            // #1 REINDEXING BUFFERS TO GET RID OF REDUNDANT VERTICES.
            std::vector<u32> remap(indices.size());
            const u64 uniqueVertexCount = meshopt_generateVertexRemapMulti(remap.data(), indices.data(), indices.size(),
                                                                           vertexPositions.size(), streams.data(), streams.size());
            meshopt_remapIndexBuffer(indices.data(), indices.data(), remap.size(), remap.data());

            RemapVertexStream(uniqueVertexCount, vertexPositions, remap);
            RemapVertexStream(uniqueVertexCount, vertexAttributes, remap);

            // #2 VERTEX CACHE OPTIMIZATION (REORDER TRIANGLES TO MAXIMIZE THE LOCALITY OF REUSED VERTEX REFERENCES IN VERTEX SHADERS)
            meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertexPositions.size());
        }

    }  // namespace MeshoptimizerUtils

    namespace FastGltfUtils
    {
        NODISCARD static EAlphaMode ConvertAlphaModeToRadiant(const fastgltf::AlphaMode alphaMode) noexcept
        {
            switch (alphaMode)
            {
                case fastgltf::AlphaMode::Opaque: return EAlphaMode::ALPHA_MODE_OPAQUE;
                case fastgltf::AlphaMode::Mask: return EAlphaMode::ALPHA_MODE_MASK;
                case fastgltf::AlphaMode::Blend: return EAlphaMode::ALPHA_MODE_BLEND;
                default: return EAlphaMode::ALPHA_MODE_OPAQUE;
            }
        }

        NODISCARD static vk::PrimitiveTopology ConvertPrimitiveTypeToVulkanPrimitiveTopology(
            const fastgltf::PrimitiveType primitiveType) noexcept
        {
            switch (primitiveType)
            {
                case fastgltf::PrimitiveType::Points: return vk::PrimitiveTopology::ePointList;
                case fastgltf::PrimitiveType::Lines:
                case fastgltf::PrimitiveType::LineLoop: return vk::PrimitiveTopology::eLineList;
                case fastgltf::PrimitiveType::LineStrip: return vk::PrimitiveTopology::eLineStrip;
                case fastgltf::PrimitiveType::Triangles: return vk::PrimitiveTopology::eTriangleList;
                case fastgltf::PrimitiveType::TriangleStrip: return vk::PrimitiveTopology::eTriangleStrip;
                case fastgltf::PrimitiveType::TriangleFan: return vk::PrimitiveTopology::eTriangleFan;
                default: return vk::PrimitiveTopology::eTriangleList;
            }
        }

        NODISCARD static vk::SamplerAddressMode ConvertWrapToVulkan(const fastgltf::Wrap wrap) noexcept
        {
            switch (wrap)
            {
                case fastgltf::Wrap::Repeat: return vk::SamplerAddressMode::eRepeat;
                case fastgltf::Wrap::MirroredRepeat: return vk::SamplerAddressMode::eMirroredRepeat;
                case fastgltf::Wrap::ClampToEdge: return vk::SamplerAddressMode::eClampToEdge;
                default: return vk::SamplerAddressMode::eRepeat;
            }
        }

        NODISCARD static vk::Filter ConvertFilterToVulkan(const fastgltf::Filter filter) noexcept
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

        NODISCARD static vk::SamplerMipmapMode ConvertMipMapModeToVulkan(const fastgltf::Filter filter) noexcept
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

        // NOTE: For simplicity, usage of the same texture with multiple samplers isn't supported at least for now!
        NODISCARD static std::string LoadTexture(std::mutex& loaderMutex, UnorderedMap<std::string, Shared<GfxTexture>>& textureMap,
                                                 const std::filesystem::path& meshParentPath, const Unique<GfxContext>& gfxContext,
                                                 const fastgltf::Asset& asset, const fastgltf::Texture& texture,
                                                 const std::vector<vk::SamplerCreateInfo>& samplerCIs) noexcept
        {
            if (!texture.imageIndex.has_value())
            {
                LOG_WARN("fastgltf: Texture has no image attached to it! Returning default white texture!");
                const std::string defaultWhiteTextureName{"RDNT_DEFAULT_WHITE_TEX"};

                std::scoped_lock lock(loaderMutex);  // Synchronizing access to textureMap
                if (textureMap.contains(defaultWhiteTextureName)) return defaultWhiteTextureName;

                textureMap[defaultWhiteTextureName] = gfxContext->GetDefaultWhiteTexture();
                return defaultWhiteTextureName;
            }
            int32_t width{1}, height{1}, channels{4};

            ImmediateExecuteContext executionContext = {};
            std::string textureName{s_DEFAULT_STRING};
            Shared<GfxTexture> loadedTexture{nullptr};
            std::visit(
                fastgltf::visitor{
                    [](auto& arg) { RDNT_ASSERT(false, "fastgltf: Default argument when loading image! This shouldn't happen!") },
                    [&](const fastgltf::sources::URI& filePath)
                    {
                        RDNT_ASSERT(filePath.fileByteOffset == 0, "fastgltf: We don't support offsets with stbi!");
                        RDNT_ASSERT(filePath.uri.isLocalPath(), "fastgltf: We're only capable of loading local files!");

                        textureName = filePath.uri.path();
                        RDNT_ASSERT(!textureName.empty(), "fastgltf: Texture name is empty!");
                        {
                            std::scoped_lock lock(loaderMutex);  // Synchronizing access to textureMap by putting dummy(null) texture
                            if (textureMap.contains(textureName))
                                return;
                            else
                                textureMap[textureName] = loadedTexture;
                        }

                        const auto textureFilePath = meshParentPath / textureName;
                        void* imageData            = GfxTextureUtils::LoadImage(textureFilePath.string(), width, height, channels);
                        RDNT_ASSERT(imageData, "fastgltf: Failed to load image data!");

                        constexpr bool bGenerateMipMaps = true;
                        std::optional<vk::SamplerCreateInfo> samplerCI{std::nullopt};
                        if (texture.samplerIndex.has_value())
                        {
                            samplerCI = samplerCIs[*texture.samplerIndex];
                            if (bGenerateMipMaps) samplerCI->maxLod = GfxTextureUtils::GetMipLevelCount(width, height);
                        }

                        {
                            std::scoped_lock lock(loaderMutex);  // Synchronizing access to textureMap by loading actual texture
                            loadedTexture = MakeShared<GfxTexture>(
                                gfxContext->GetDevice(), GfxTextureDescription{.Type{vk::ImageType::e2D},
                                                                               .Dimensions{width, height, 1},
                                                                               .bExposeMips{false},
                                                                               .bGenerateMips{bGenerateMipMaps},
                                                                               .LayerCount{1},
                                                                               .Format{vk::Format::eR8G8B8A8Unorm},
                                                                               .UsageFlags{vk::ImageUsageFlagBits::eColorAttachment |
                                                                                           vk::ImageUsageFlagBits::eTransferDst},
                                                                               .SamplerCreateInfo{samplerCI}});
                            textureMap[textureName] = loadedTexture;
                            gfxContext->GetDevice()->SetDebugName(textureName, (const vk::Image&)*loadedTexture);
                        }

                        const auto imageSize = static_cast<std::size_t>(width * height * channels);
                        auto stagingBuffer   = MakeUnique<GfxBuffer>(
                            gfxContext->GetDevice(), GfxBufferDescription{.Capacity = imageSize, .ExtraFlags = EXTRA_BUFFER_FLAG_MAPPED});

                        stagingBuffer->SetData(imageData, imageSize);

                        executionContext = gfxContext->CreateImmediateExecuteContext(ECommandBufferType::COMMAND_BUFFER_TYPE_GENERAL);
                        executionContext.CommandBuffer.begin(
                            vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

                        executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                            vk::ImageMemoryBarrier2()
                                .setImage(*loadedTexture)
                                .setSubresourceRange(
                                    vk::ImageSubresourceRange()
                                        .setBaseArrayLayer(0)
                                        .setBaseMipLevel(0)
                                        .setLevelCount(bGenerateMipMaps ? GfxTextureUtils::GetMipLevelCount(width, height) : 1)
                                        .setLayerCount(1)
                                        .setAspectMask(vk::ImageAspectFlagBits::eColor))
                                .setOldLayout(vk::ImageLayout::eUndefined)
                                .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                                .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                                .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                                .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
                                .setDstStageMask(vk::PipelineStageFlagBits2::eAllTransfer)));

                        executionContext.CommandBuffer.copyBufferToImage(
                            *stagingBuffer, *loadedTexture, vk::ImageLayout::eTransferDstOptimal,
                            vk::BufferImageCopy()
                                .setImageSubresource(vk::ImageSubresourceLayers()
                                                         .setLayerCount(1)
                                                         .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                         .setBaseArrayLayer(0)
                                                         .setMipLevel(0))
                                .setImageExtent(vk::Extent3D(width, height, 1)));

                        if (bGenerateMipMaps)
                            loadedTexture->GenerateMipMaps(executionContext.CommandBuffer);
                        else
                        {
                            executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                                vk::ImageMemoryBarrier2()
                                    .setImage(*loadedTexture)
                                    .setSubresourceRange(vk::ImageSubresourceRange()
                                                             .setBaseArrayLayer(0)
                                                             .setBaseMipLevel(0)
                                                             .setLevelCount(1)
                                                             .setLayerCount(1)
                                                             .setAspectMask(vk::ImageAspectFlagBits::eColor))
                                    .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                                    .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
                                    .setSrcStageMask(vk::PipelineStageFlagBits2::eCopy)
                                    .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                                    .setDstAccessMask(vk::AccessFlagBits2::eShaderSampledRead)
                                    .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader |
                                                     vk::PipelineStageFlagBits2::eComputeShader)));
                        }

                        executionContext.CommandBuffer.end();

                        {
                            std::scoped_lock lock(gfxContext->GetMutex());  // Synchronizing access to single queue
                            executionContext.Queue.submit(vk::SubmitInfo().setCommandBuffers(executionContext.CommandBuffer));
                            executionContext.Queue.waitIdle();
                        }

                        GfxTextureUtils::UnloadImage(imageData);
                    },
                    [&](const fastgltf::sources::Vector& vector)
                    {
                        RDNT_ASSERT(false, "{}: NOT IMPLEMENTED!", __FUNCTION__);

                        void* imageData = GfxTextureUtils::LoadImage(vector.bytes.data(), vector.bytes.size(), width, height, channels);
                        RDNT_ASSERT(imageData, "Failed to load image data!");

                        GfxTextureUtils::UnloadImage(imageData);
                    },
                    [&](const fastgltf::sources::BufferView& view)
                    {
                        RDNT_ASSERT(false, "{}: NOT IMPLEMENTED!", __FUNCTION__);

                        const auto& bufferView = asset.bufferViews[view.bufferViewIndex];
                        const auto& buffer     = asset.buffers[bufferView.bufferIndex];

                        std::visit(fastgltf::visitor{// We only care about VectorWithMime here, because we
                                                     // specify LoadExternalBuffers, meaning all buffers
                                                     // are already loaded into a vector.
                                                     [](auto& arg) {},
                                                     [&](fastgltf::sources::Vector& vector)
                                                     {
                                                         void* imageData =
                                                             GfxTextureUtils::LoadImage(vector.bytes.data() + bufferView.byteOffset,
                                                                                        bufferView.byteLength, width, height, channels);
                                                         RDNT_ASSERT(imageData, "Failed to load image data!");

                                                         GfxTextureUtils::UnloadImage(imageData);
                                                     }},
                                   buffer.data);
                    },
                },
                asset.images[*texture.imageIndex].data);

            return textureName;
        }

        // NOTE: Given only a normal vector, finds a valid tangent.
        // This uses the technique from "Improved accuracy when building an orthonormal
        // basis" by Nelson Max, https://jcgt.org/published/0006/01/02.
        // Any tangent-generating algorithm must produce at least one discontinuity
        // when operating on a sphere (due to the hairy ball theorem); this has a
        // small ring-shaped discontinuity at normal.z == -0.99998796.
        NODISCARD static glm::vec4 MakeFastTangent(const glm::vec3& n) noexcept
        {
            if (n.z < -0.99998796F)  // Handle the singularity
                return glm::vec4(0.0F, -1.0F, 0.0F, 1.0F);

            const f32 a = 1.0F / (1.0F + n.z);
            const f32 b = -n.x * n.y * a;
            return glm::vec4(1.0F - n.x * n.x * a, b, -n.x, 1.0F);
        }

    }  // namespace FastGltfUtils

    Mesh::Mesh(const Unique<GfxContext>& gfxContext, const std::filesystem::path& meshFilePath) noexcept
    {
        constexpr auto gltfLoadOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::LoadGLBBuffers |
                                         fastgltf::Options::LoadExternalBuffers | fastgltf::Options::GenerateMeshIndices;

        auto gltfFile = fastgltf::MappedGltfFile::FromPath(meshFilePath);
        RDNT_ASSERT(gltfFile.error() == fastgltf::Error::None, "fastgltf: failed to open glTF file: {}",
                    fastgltf::getErrorMessage(gltfFile.error()));

        constexpr fastgltf::Extensions gltfExtensions = fastgltf::Extensions::KHR_materials_emissive_strength;

        const auto meshParentPath = meshFilePath.parent_path();
        fastgltf::Parser parser(gltfExtensions);
        auto asset = parser.loadGltf(gltfFile.get(), meshParentPath, gltfLoadOptions);
        RDNT_ASSERT(asset.error() == fastgltf::Error::None, "fastgltf: failed to load glTF file: {}",
                    fastgltf::getErrorMessage(asset.error()));

        std::vector<vk::SamplerCreateInfo> samplerCIs(asset->samplers.size());
        for (u32 i{}; i < samplerCIs.size(); ++i)
        {
            const auto& currentSamplerInfo = asset->samplers[i];
            samplerCIs[i] =
                vk::SamplerCreateInfo()
                    .setMinLod(0.0f)
                    .setMaxLod(vk::LodClampNone)
                    .setMagFilter(FastGltfUtils::ConvertFilterToVulkan(currentSamplerInfo.magFilter.value_or(fastgltf::Filter::Linear)))
                    .setMinFilter(FastGltfUtils::ConvertFilterToVulkan(currentSamplerInfo.minFilter.value_or(fastgltf::Filter::Linear)))
                    .setMipmapMode(
                        FastGltfUtils::ConvertMipMapModeToVulkan(currentSamplerInfo.minFilter.value_or(fastgltf::Filter::Linear)))
                    .setAddressModeU(FastGltfUtils::ConvertWrapToVulkan(currentSamplerInfo.wrapS))
                    .setAddressModeV(FastGltfUtils::ConvertWrapToVulkan(currentSamplerInfo.wrapT))
                    .setAddressModeW(FastGltfUtils::ConvertWrapToVulkan(currentSamplerInfo.wrapT))
                    .setUnnormalizedCoordinates(vk::False)
                    .setBorderColor(vk::BorderColor::eFloatOpaqueWhite)
                    .setAnisotropyEnable(vk::True)
                    .setMaxAnisotropy(gfxContext->GetDevice()->GetGPUProperties().limits.maxSamplerAnisotropy);
        }

        // NOTE: textureIndex -> name, since multiple materials can reference the same textures but with different samplers, so
        // there's no need to load same texture N times.
        UnorderedMap<std::size_t, std::string> textureNameLUT{};
        textureNameLUT.reserve(asset->textures.size());
        TextureMap.reserve(asset->textures.size());
        // Parallel texture loading.
        {
            std::vector<std::future<std::string>> textureFutures;
            std::mutex loaderMutex          = {};
            const auto textureLoadBeginTime = Timer::Now();
            for (std::size_t textureIndex{}; textureIndex < asset->textures.size(); ++textureIndex)
            {
                textureFutures.emplace_back(Application::Get().GetThreadPool()->Submit(
                    [&, textureIndex]() noexcept
                    {
                        return FastGltfUtils::LoadTexture(loaderMutex, TextureMap, meshParentPath, gfxContext, asset.get(),
                                                          asset->textures[textureIndex], samplerCIs);
                    }));
            }

            for (std::size_t textureIndex{}; textureIndex < textureFutures.size(); ++textureIndex)
            {
                auto textureName             = textureFutures[textureIndex].get();
                textureNameLUT[textureIndex] = textureName;
            }

            const auto textureLoadEndTime = Timer::Now();
            LOG_INFO("Loaded ({}) textures in [{:.8f}] ms", TextureMap.size(),
                     std::chrono::duration<f32, std::chrono::milliseconds::period>(textureLoadEndTime - textureLoadBeginTime).count());
        }

        UnorderedMap<std::string, Shaders::GLTFMaterial> materialMap;
        materialMap.reserve(asset->materials.size());
        for (const auto& fastgltfMaterial : asset->materials)
        {
            RDNT_ASSERT(!fastgltfMaterial.name.empty(), "fastgltf: Material has no name!");

            u32 albedoTextureID{0};
            if (fastgltfMaterial.pbrData.baseColorTexture.has_value())
            {
                albedoTextureID =
                    TextureMap[textureNameLUT[fastgltfMaterial.pbrData.baseColorTexture->textureIndex]]->GetBindlessTextureID();
            }

            u32 metallicRoughnessTextureID{0};
            if (fastgltfMaterial.pbrData.metallicRoughnessTexture.has_value())
            {
                metallicRoughnessTextureID =
                    TextureMap[textureNameLUT[fastgltfMaterial.pbrData.metallicRoughnessTexture->textureIndex]]->GetBindlessTextureID();
            }

            u32 normalTextureID{0};
            f32 normalScale{1.0f};
            if (fastgltfMaterial.normalTexture.has_value())
            {
                normalTextureID = TextureMap[textureNameLUT[fastgltfMaterial.normalTexture->textureIndex]]->GetBindlessTextureID();
                normalScale     = fastgltfMaterial.normalTexture->scale;
            }

            u32 occlusionTextureID{0};
            f32 occlusionStrength{1.0f};
            if (fastgltfMaterial.occlusionTexture.has_value())
            {
                occlusionTextureID = TextureMap[textureNameLUT[fastgltfMaterial.occlusionTexture->textureIndex]]->GetBindlessTextureID();
                occlusionStrength  = fastgltfMaterial.occlusionTexture->strength;
            }

            u32 emissiveTextureID{0};
            if (fastgltfMaterial.emissiveTexture.has_value())
            {
                emissiveTextureID = TextureMap[textureNameLUT[fastgltfMaterial.emissiveTexture->textureIndex]]->GetBindlessTextureID();
            }

            const std::string materialName{fastgltfMaterial.name};
            materialMap[materialName] = {
                .PbrData{.BaseColorFactor{Shaders::PackUnorm4x8((glm::vec4&)fastgltfMaterial.pbrData.baseColorFactor)},
                         .MetallicFactor{Shaders::PackUnorm2x8(fastgltfMaterial.pbrData.metallicFactor)},
                         .RoughnessFactor{Shaders::PackUnorm2x8(fastgltfMaterial.pbrData.roughnessFactor)},
                         .AlbedoTextureID{albedoTextureID},
                         .MetallicRoughnessTextureID{metallicRoughnessTextureID}},
                .NormalTextureID{normalTextureID},
                .NormalScale{normalScale},
                .OcclusionTextureID{occlusionTextureID},
                .OcclusionStrength{Shaders::PackUnorm2x8(occlusionStrength)},
                .EmissiveTextureID{emissiveTextureID},
                .EmissiveFactor{(glm::vec3&)fastgltfMaterial.emissiveFactor * fastgltfMaterial.emissiveStrength},
                .AlphaCutoff{fastgltfMaterial.alphaCutoff}};
        }

        // Use the same vectors for all meshes so that the memory doesn't reallocate as often.
        std::vector<u32> indices;
        std::vector<VertexPosition> vertexPositions;
        std::vector<VertexAttribute> vertexAttributes;
        LOG_INFO("Loading scene: {}", meshFilePath.string());

        std::vector<std::string> meshAssetLUT{};
        MeshAssetMap.reserve(asset->meshes.size());
        for (const auto& fastgltfMesh : asset->meshes)
        {
            RDNT_ASSERT(!fastgltfMesh.name.empty(), "fastgltf: Mesh has no name!");

            const std::string meshName{fastgltfMesh.name};
            LOG_INFO("Loading submesh: {}", meshName);

            indices.clear();
            vertexPositions.clear();
            vertexAttributes.clear();

            meshAssetLUT.emplace_back(meshName);
            MeshAssetMap[meshName]       = MakeShared<MeshAsset>();
            MeshAssetMap[meshName]->Name = meshName;

            for (const auto& primitve : fastgltfMesh.primitives)
            {
                RDNT_ASSERT(primitve.indicesAccessor.has_value(),
                            "fastgltf: We specify GenerateMeshIndices, so we should always have indices!");
                const auto* positionIt = primitve.findAttribute("POSITION");
                RDNT_ASSERT(positionIt != primitve.attributes.end(),
                            "fastgltf: A mesh primitive is required to hold the POSITION attribute.");

                MeshAssetMap[meshName]->Surfaces.emplace_back(
                    GeometryData{.StartIndex{static_cast<u32>(indices.size())},
                                 .Count{static_cast<u32>(asset->accessors[*primitve.indicesAccessor].count)},
                                 .MaterialID{0},
                                 .PrimitiveTopology{FastGltfUtils::ConvertPrimitiveTypeToVulkanPrimitiveTopology(primitve.type)}});

                MeshAssetMap[meshName]->Surfaces.back().MaterialID = primitve.materialIndex.value_or(0);
                MeshAssetMap[meshName]->Surfaces.back().AlphaMode =
                    FastGltfUtils::ConvertAlphaModeToRadiant(asset->materials[primitve.materialIndex.value_or(0)].alphaMode);
                MeshAssetMap[meshName]->Surfaces.back().CullMode = asset->materials[primitve.materialIndex.value_or(0)].doubleSided
                                                                       ? vk::CullModeFlagBits::eNone
                                                                       : vk::CullModeFlagBits::eBack;

                const auto initialVertexIndex{vertexPositions.size()};

                // Loading indices.
                {
                    const auto& indicesAccessor = asset->accessors[*primitve.indicesAccessor];
                    indices.reserve(indices.size() + indicesAccessor.count);

                    fastgltf::iterateAccessor<u32>(asset.get(), indicesAccessor,
                                                   [&](u32 idx) { indices.emplace_back(initialVertexIndex + idx); });
                }

                // Load vertex positions.
                {
                    const auto& posAccessor = asset->accessors[positionIt->accessorIndex];

                    // Extend current vertex buffers
                    vertexPositions.resize(vertexPositions.size() + posAccessor.count);
                    vertexAttributes.resize(vertexAttributes.size() + posAccessor.count);

                    fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), posAccessor,
                                                                  [&](const glm::vec3& v, const std::size_t index)
                                                                  { vertexPositions[initialVertexIndex + index].Position = v; });
                }

                // Load vertex attributes.
                {
                    // 1. Vertex colors
                    if (const auto* colorsAttribute = primitve.findAttribute("COLOR_0"); colorsAttribute != primitve.attributes.end())
                    {
                        fastgltf::iterateAccessorWithIndex<glm::vec4>(asset.get(), asset->accessors[colorsAttribute->accessorIndex],
                                                                      [&](const glm::vec4& vertexColor, const std::size_t index) {
                                                                          vertexAttributes[initialVertexIndex + index].Color =
                                                                              Shaders::PackUnorm4x8(vertexColor);
                                                                      });
                    }

                    // 2. Normals
                    if (const auto* normalsAttribute = primitve.findAttribute("NORMAL"); normalsAttribute != primitve.attributes.end())
                    {
                        fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), asset->accessors[normalsAttribute->accessorIndex],
                                                                      [&](const glm::vec3& n, const std::size_t index) {
                                                                          vertexAttributes[initialVertexIndex + index].Normal =
                                                                              glm::packHalf(Shaders::EncodeOct(n));
                                                                      });
                    }

                    // 3. Tangents
                    if (const auto* tangentAttribute = primitve.findAttribute("TANGENT"); tangentAttribute != primitve.attributes.end())
                    {
                        fastgltf::iterateAccessorWithIndex<glm::vec4>(asset.get(), asset->accessors[tangentAttribute->accessorIndex],
                                                                      [&](const glm::vec4& t, const std::size_t index)
                                                                      {
                                                                          vertexAttributes[initialVertexIndex + index].TSign = t.w;
                                                                          vertexAttributes[initialVertexIndex + index].Tangent =
                                                                              glm::packHalf(Shaders::EncodeOct(t));
                                                                      });
                    }

                    // 4. UV
                    if (const auto* uvAttribute = primitve.findAttribute("TEXCOORD_0"); uvAttribute != primitve.attributes.end())
                    {
                        fastgltf::iterateAccessorWithIndex<glm::vec2>(asset.get(), asset->accessors[uvAttribute->accessorIndex],
                                                                      [&](const glm::vec2& uv, const std::size_t index) {
                                                                          vertexAttributes[initialVertexIndex + index].UV =
                                                                              glm::packHalf(uv);
                                                                      });
                    }
                }
            }

            MeshoptimizerUtils::OptimizeMesh(indices, vertexPositions, vertexAttributes);

            MeshAssetMap[meshName]->IndexBufferID           = IndexBuffers.size();
            MeshAssetMap[meshName]->VertexPositionBufferID  = VertexPositionBuffers.size();
            MeshAssetMap[meshName]->VertexAttributeBufferID = VertexAttributeBuffers.size();

            const auto [cmd, queue] =
                gfxContext->AllocateSingleUseCommandBufferWithQueue(ECommandBufferType::COMMAND_BUFFER_TYPE_DEDICATED_TRANSFER);
            cmd->begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

            // Handle vertex positions.
            auto vbpStagingBuffer = MakeUnique<GfxBuffer>(
                gfxContext->GetDevice(), GfxBufferDescription{.Capacity   = vertexPositions.size() * sizeof(vertexPositions[0]),
                                                              .UsageFlags = vk::BufferUsageFlagBits::eTransferSrc,
                                                              .ExtraFlags = EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED});
            vbpStagingBuffer->SetData(vertexPositions.data(), vertexPositions.size() * sizeof(vertexPositions[0]));

            auto& vtxPosBuffer = VertexPositionBuffers.emplace_back(MakeShared<GfxBuffer>(
                gfxContext->GetDevice(),
                GfxBufferDescription{.Capacity    = vertexPositions.size() * sizeof(vertexPositions[0]),
                                     .ElementSize = sizeof(vertexPositions[0]),
                                     .UsageFlags  = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer,
                                     .ExtraFlags  = EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL}));
            cmd->copyBuffer(*vbpStagingBuffer, *vtxPosBuffer,
                            vk::BufferCopy().setSize(vertexPositions.size() * sizeof(vertexPositions[0])));

            // Handle vertex attributes.
            auto vabStagingBuffer = MakeUnique<GfxBuffer>(
                gfxContext->GetDevice(), GfxBufferDescription{.Capacity   = vertexAttributes.size() * sizeof(vertexAttributes[0]),
                                                              .UsageFlags = vk::BufferUsageFlagBits::eTransferSrc,
                                                              .ExtraFlags = EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED});
            vabStagingBuffer->SetData(vertexAttributes.data(), vertexAttributes.size() * sizeof(vertexAttributes[0]));

            auto& vtxAttribBuffer = VertexAttributeBuffers.emplace_back(MakeShared<GfxBuffer>(
                gfxContext->GetDevice(),
                GfxBufferDescription{.Capacity    = vertexAttributes.size() * sizeof(vertexAttributes[0]),
                                     .ElementSize = sizeof(vertexAttributes[0]),
                                     .UsageFlags  = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer,
                                     .ExtraFlags  = EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL}));
            cmd->copyBuffer(*vabStagingBuffer, *vtxAttribBuffer,
                            vk::BufferCopy().setSize(vertexAttributes.size() * sizeof(vertexAttributes[0])));

            // Handle indices.
            auto ibStagingBuffer = MakeUnique<GfxBuffer>(gfxContext->GetDevice(),
                                                         GfxBufferDescription{.Capacity   = indices.size() * sizeof(indices[0]),
                                                                              .UsageFlags = vk::BufferUsageFlagBits::eTransferSrc,
                                                                              .ExtraFlags = EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED});
            ibStagingBuffer->SetData(indices.data(), indices.size() * sizeof(indices[0]));

            auto& ibBuffer = IndexBuffers.emplace_back(MakeShared<GfxBuffer>(
                gfxContext->GetDevice(),
                GfxBufferDescription{.Capacity    = indices.size() * sizeof(indices[0]),
                                     .ElementSize = sizeof(indices[0]),
                                     .UsageFlags  = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndexBuffer,
                                     .ExtraFlags  = EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL}));
            cmd->copyBuffer(*ibStagingBuffer, *ibBuffer, vk::BufferCopy().setSize(indices.size() * sizeof(indices[0])));

            cmd->end();
            queue.submit(vk::SubmitInfo().setCommandBuffers(*cmd));
            queue.waitIdle();
        }
        meshAssetLUT.shrink_to_fit();

        std::vector<Shared<RenderNode>> nodesToConfigureLater;
        for (const auto& gltfNode : asset->nodes)
        {
            RDNT_ASSERT(!gltfNode.name.empty(), "fastgltf: Node has no name!");

            const std::string nodeName{gltfNode.name};
            Shared<RenderNode> newNode{MakeShared<RenderNode>()};
            newNode->Name = nodeName;

            // Find if the node has a mesh, and if it does hook it to the mesh pointer and allocate it with the RenderNode class.
            if (gltfNode.meshIndex.has_value())
            {
                newNode->MeshAsset = MeshAssetMap[meshAssetLUT[*gltfNode.meshIndex]];
            }
            RenderNodes[nodeName] = newNode;
            nodesToConfigureLater.emplace_back(newNode);

            std::visit(fastgltf::visitor{[](auto& arg) {
                                             RDNT_ASSERT(
                                                 false,
                                                 "fastgltf: Default argument when parsing transformation matrices! This shouldn't happen!")
                                         },
                                         [&](const fastgltf::math::fmat4x4& trsMatrix)
                                         { memcpy(&newNode->LocalTransform, trsMatrix.data(), sizeof(trsMatrix)); },
                                         [&](const fastgltf::TRS& trsMatrix)
                                         {
                                             const glm::vec3 tl{(glm::vec3&)trsMatrix.translation};
                                             const glm::quat rot{(glm::quat&)trsMatrix.rotation};
                                             const glm::vec3 sc{(glm::vec3&)trsMatrix.scale};

                                             const glm::mat4 tm{glm::translate(glm::mat4(1.f), tl)};
                                             const glm::mat4 rm{glm::toMat4(rot)};
                                             const glm::mat4 sm{glm::scale(glm::mat4(1.f), sc)};

                                             newNode->LocalTransform = tm * rm * sm;
                                         }},
                       gltfNode.transform);
        }

        // Setup transform hierarchy.
        for (u32 i{}; i < asset->nodes.size(); ++i)
        {
            const auto& node = asset->nodes[i];
            auto& renderNode = nodesToConfigureLater[i];

            for (const auto& childrenIndex : node.children)
            {
                nodesToConfigureLater[childrenIndex]->Parent = renderNode;
                renderNode->Children.emplace_back(nodesToConfigureLater[childrenIndex]);
            }
        }

        // Find the root nodes.
        for (auto& renderNode : nodesToConfigureLater)
        {
            if (renderNode->Parent.lock()) continue;

            RootNodes.emplace_back(renderNode);
            renderNode->RefreshTransform(glm::mat4{1.f});
        }

        // TODO: Store materials in VRAM?
        // Load material buffers.
        for (const auto& [_, gltfMaterial] : materialMap)
        {
            MaterialBuffers.emplace_back(MakeShared<GfxBuffer>(
                gfxContext->GetDevice(), GfxBufferDescription{.Capacity    = sizeof(Shaders::GLTFMaterial),
                                                              .ElementSize = sizeof(Shaders::GLTFMaterial),
                                                              .UsageFlags  = vk::BufferUsageFlagBits::eUniformBuffer,
                                                              .ExtraFlags  = EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED |
                                                                            EExtraBufferFlag::EXTRA_BUFFER_FLAG_ADDRESSABLE}));
            MaterialBuffers.back()->SetData(&gltfMaterial, sizeof(gltfMaterial));
        }
    }

}  // namespace Radiant
