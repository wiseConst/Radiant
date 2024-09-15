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
                                                const std::vector<u32>& rempaTable) noexcept
        {
            std::vector<T> newVertexStream(uniqueVertexCount);
            meshopt_remapVertexBuffer(newVertexStream.data(), vertexStream.data(), vertexStream.size(), sizeof(T), rempaTable.data());
            vertexStream = std::move(newVertexStream);
        }

        template <typename IndexType>
        static void OptimizeMesh(std::vector<IndexType>& indices, std::vector<VertexPosition>& vertexPositions,
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
            i32 width{1}, height{1}, channels{4};

            GfxImmediateExecuteContext executionContext = {};
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
                        if (texture.samplerIndex.has_value()) samplerCI = samplerCIs[*texture.samplerIndex];

                        {
                            std::scoped_lock lock(loaderMutex);  // Synchronizing access to textureMap by loading actual texture
                            loadedTexture = MakeShared<GfxTexture>(
                                gfxContext->GetDevice(),
                                GfxTextureDescription(vk::ImageType::e2D, {width, height, 1}, vk::Format::eR8G8B8A8Unorm,
                                                      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
                                                      samplerCI, 1, vk::SampleCountFlagBits::e1,
                                                      EResourceCreateBits::RESOURCE_CREATE_GENERATE_MIPS_BIT));
                            textureMap[textureName] = loadedTexture;
                            gfxContext->GetDevice()->SetDebugName(textureName, (const vk::Image&)*loadedTexture);
                        }

                        const auto imageSize = static_cast<u64>(width * height * channels);
                        auto stagingBuffer   = MakeUnique<GfxBuffer>(gfxContext->GetDevice(),
                                                                   GfxBufferDescription(imageSize, /* placeholder */ 1,
                                                                                          vk::BufferUsageFlagBits::eTransferSrc,
                                                                                          EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT));
                        stagingBuffer->SetData(imageData, imageSize);

                        executionContext =
                            gfxContext->CreateImmediateExecuteContext(ECommandBufferTypeBits::COMMAND_BUFFER_TYPE_GENERAL_BIT);
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
                    .setUnnormalizedCoordinates(vk::False)
                    .setMinLod(0.0f)
                    .setMaxLod(vk::LodClampNone)
                    .setMagFilter(FastGltfUtils::ConvertFilterToVulkan(currentSamplerInfo.magFilter.value_or(fastgltf::Filter::Linear)))
                    .setMinFilter(FastGltfUtils::ConvertFilterToVulkan(currentSamplerInfo.minFilter.value_or(fastgltf::Filter::Linear)))
                    .setMipmapMode(
                        FastGltfUtils::ConvertMipMapModeToVulkan(currentSamplerInfo.minFilter.value_or(fastgltf::Filter::Linear)))
                    .setAddressModeU(FastGltfUtils::ConvertWrapToVulkan(currentSamplerInfo.wrapS))
                    .setAddressModeV(FastGltfUtils::ConvertWrapToVulkan(currentSamplerInfo.wrapT))
                    .setAddressModeW(FastGltfUtils::ConvertWrapToVulkan(currentSamplerInfo.wrapT))
                    .setBorderColor(vk::BorderColor::eFloatOpaqueWhite)
                    .setAnisotropyEnable(vk::True)
                    .setMaxAnisotropy(gfxContext->GetDevice()->GetGPUProperties().limits.maxSamplerAnisotropy);
        }

        // NOTE: textureIndex -> name, since multiple materials can reference the same textures but with different samplers, so
        // there's no need to load same texture N times.
        UnorderedMap<u64, std::string> textureNameLUT{};
        textureNameLUT.reserve(asset->textures.size());
        TextureMap.reserve(asset->textures.size());
        // Parallel texture loading.
        {
            std::vector<std::future<std::string>> textureFutures;
            std::mutex loaderMutex          = {};
            const auto textureLoadBeginTime = Timer::Now();
            for (u64 textureIndex{}; textureIndex < asset->textures.size(); ++textureIndex)
            {
                textureFutures.emplace_back(Application::Get().GetThreadPool()->Submit(
                    [&, textureIndex]() noexcept
                    {
                        return FastGltfUtils::LoadTexture(loaderMutex, TextureMap, meshParentPath, gfxContext, asset.get(),
                                                          asset->textures[textureIndex], samplerCIs);
                    }));
            }

            for (u64 textureIndex{}; textureIndex < textureFutures.size(); ++textureIndex)
            {
                auto textureName             = textureFutures[textureIndex].get();
                textureNameLUT[textureIndex] = textureName;
            }

            const auto textureLoadEndTime = Timer::Now();
            LOG_INFO("Loaded ({}) textures in [{:.3f}] ms", TextureMap.size(),
                     std::chrono::duration<f32, std::chrono::milliseconds::period>(textureLoadEndTime - textureLoadBeginTime).count());
        }

        UnorderedMap<std::string, Shaders::GLTFMaterial> materialMap;
        materialMap.reserve(asset->materials.size());
        for (const auto& fastgltfMaterial : asset->materials)
        {
            RDNT_ASSERT(!fastgltfMaterial.name.empty(), "fastgltf: Material has no name!");

            u32 albedoTextureID{0};
            if (fastgltfMaterial.pbrData.baseColorTexture.has_value())
                albedoTextureID =
                    TextureMap[textureNameLUT[fastgltfMaterial.pbrData.baseColorTexture->textureIndex]]->GetBindlessTextureID();

            u32 metallicRoughnessTextureID{0};
            if (fastgltfMaterial.pbrData.metallicRoughnessTexture.has_value())
                metallicRoughnessTextureID =
                    TextureMap[textureNameLUT[fastgltfMaterial.pbrData.metallicRoughnessTexture->textureIndex]]->GetBindlessTextureID();

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
                emissiveTextureID = TextureMap[textureNameLUT[fastgltfMaterial.emissiveTexture->textureIndex]]->GetBindlessTextureID();

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
        std::vector<u32> indicesUint32;
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

            indicesUint32.clear();
            vertexPositions.clear();
            vertexAttributes.clear();

            meshAssetLUT.emplace_back(meshName);

            auto& currentMeshAsset = MeshAssetMap[meshName];
            currentMeshAsset       = MakeShared<MeshAsset>();
            currentMeshAsset->Name = meshName;

            for (const auto& primitive : fastgltfMesh.primitives)
            {
                RDNT_ASSERT(primitive.indicesAccessor.has_value(),
                            "fastgltf: We specify GenerateMeshIndices, so we should always have indices!");
                const auto* positionIt = primitive.findAttribute("POSITION");
                RDNT_ASSERT(positionIt != primitive.attributes.end(),
                            "fastgltf: A mesh primitive is required to hold the POSITION attribute.");

                currentMeshAsset->Surfaces.emplace_back(
                    static_cast<u32>(indicesUint32.size()), static_cast<u32>(asset->accessors[*primitive.indicesAccessor].count), Sphere{},
                    primitive.materialIndex.value_or(0), FastGltfUtils::ConvertPrimitiveTypeToVulkanPrimitiveTopology(primitive.type));

                if (primitive.materialIndex.has_value())
                {
                    currentMeshAsset->Surfaces.back().CullMode =
                        asset->materials[*primitive.materialIndex].doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
                    currentMeshAsset->Surfaces.back().AlphaMode =
                        FastGltfUtils::ConvertAlphaModeToRadiant(asset->materials[primitive.materialIndex.value_or(0)].alphaMode);
                }

                const auto initialVertexIndex{vertexPositions.size()};

                // Loading indices.
                {
                    const auto& indicesAccessor = asset->accessors[*primitive.indicesAccessor];

                    const auto parsedIndexType  = (indicesAccessor.componentType == fastgltf::ComponentType::UnsignedByte ||
                                                  indicesAccessor.componentType == fastgltf::ComponentType::UnsignedShort)
                                                      ? vk::IndexType::eUint16
                                                      : vk::IndexType::eUint32;
                    currentMeshAsset->IndexType = currentMeshAsset->IndexType == vk::IndexType::eNoneKHR
                                                      ? parsedIndexType
                                                      : std::max(currentMeshAsset->IndexType, parsedIndexType);
                    indicesUint32.reserve(indicesUint32.size() + indicesAccessor.count);

                    fastgltf::iterateAccessor<u32>(asset.get(), indicesAccessor,
                                                   [&](u32 idx)
                                                   {
                                                       indicesUint32.emplace_back(initialVertexIndex + idx);
                                                       // NOTE: Check if index > u16::max() and then switch to index type u32.
                                                       currentMeshAsset->IndexType = indicesUint32.back() > std::numeric_limits<u16>::max()
                                                                                         ? vk::IndexType::eUint32
                                                                                         : currentMeshAsset->IndexType;
                                                   });
                }

                // Load vertex positions.
                {
                    const auto& posAccessor = asset->accessors[positionIt->accessorIndex];

                    // Extend current vertex buffers
                    vertexPositions.resize(vertexPositions.size() + posAccessor.count);
                    vertexAttributes.resize(vertexAttributes.size() + posAccessor.count);

                    fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), posAccessor,
                                                                  [&](const glm::vec3& v, const u64 index)
                                                                  { vertexPositions[initialVertexIndex + index].Position = v; });
                }

                // Load vertex attributes.
                // 1. Vertex colors
                if (const auto* colorsAttribute = primitive.findAttribute("COLOR_0"); colorsAttribute != primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(asset.get(), asset->accessors[colorsAttribute->accessorIndex],
                                                                  [&](const glm::vec4& vertexColor, const u64 index) {
                                                                      vertexAttributes[initialVertexIndex + index].Color =
                                                                          Shaders::PackUnorm4x8(vertexColor);
                                                                  });
                }

                // 2. Normals
                if (const auto* normalsAttribute = primitive.findAttribute("NORMAL"); normalsAttribute != primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), asset->accessors[normalsAttribute->accessorIndex],
                                                                  [&](const glm::vec3& n, const u64 index) {
                                                                      vertexAttributes[initialVertexIndex + index].Normal =
                                                                          glm::packHalf(Shaders::EncodeOct(n));
                                                                  });
                }

                // 3. Tangents
                if (const auto* tangentAttribute = primitive.findAttribute("TANGENT"); tangentAttribute != primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(asset.get(), asset->accessors[tangentAttribute->accessorIndex],
                                                                  [&](const glm::vec4& t, const u64 index)
                                                                  {
                                                                      vertexAttributes[initialVertexIndex + index].TSign = t.w;
                                                                      vertexAttributes[initialVertexIndex + index].Tangent =
                                                                          glm::packHalf(Shaders::EncodeOct(t));
                                                                  });
                }

                // 4. UV
                if (const auto* uvAttribute = primitive.findAttribute("TEXCOORD_0"); uvAttribute != primitive.attributes.end())
                {
                    fastgltf::iterateAccessorWithIndex<glm::vec2>(asset.get(), asset->accessors[uvAttribute->accessorIndex],
                                                                  [&](const glm::vec2& uv, const u64 index)
                                                                  { vertexAttributes[initialVertexIndex + index].UV = glm::packHalf(uv); });
                }
            }

            currentMeshAsset->IndexBufferID           = IndexBuffers.size();
            currentMeshAsset->VertexPositionBufferID  = VertexPositionBuffers.size();
            currentMeshAsset->VertexAttributeBufferID = VertexAttributeBuffers.size();

            // NOTE:
            // I store indices as uint32, but in case index type is different, then encoding also different.
            // For now I do expose only u16/u32, in future will be u8 as well.
            std::vector<u16> indicesUint16{};
            if (currentMeshAsset->IndexType != vk::IndexType::eUint32)
            {
                indicesUint16.reserve(indicesUint32.size());
                std::transform(indicesUint32.begin(), indicesUint32.end(), std::back_inserter(indicesUint16),
                               [](const u32 indexUint32) noexcept { return static_cast<u16>(indexUint32); });
                indicesUint32.clear();  // Clear to save memory footprint.
                MeshoptimizerUtils::OptimizeMesh(indicesUint16, vertexPositions, vertexAttributes);
            }
            else
                MeshoptimizerUtils::OptimizeMesh(indicesUint32, vertexPositions, vertexAttributes);

            const auto [cmd, queue] =
                gfxContext->AllocateSingleUseCommandBufferWithQueue(ECommandBufferTypeBits::COMMAND_BUFFER_TYPE_DEDICATED_TRANSFER_BIT);
            cmd->begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

            // Handle vertex positions.
            auto vbpStagingBuffer = MakeUnique<GfxBuffer>(
                gfxContext->GetDevice(),
                GfxBufferDescription(vertexPositions.size() * sizeof(vertexPositions[0]), sizeof(vertexPositions[0]),
                                     vk::BufferUsageFlagBits::eTransferSrc, EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT));
            vbpStagingBuffer->SetData(vertexPositions.data(), vertexPositions.size() * sizeof(vertexPositions[0]));

            auto& vtxPosBuffer = VertexPositionBuffers.emplace_back(MakeShared<GfxBuffer>(
                gfxContext->GetDevice(),
                GfxBufferDescription(vertexPositions.size() * sizeof(vertexPositions[0]), sizeof(vertexPositions[0]),
                                     vk::BufferUsageFlagBits::eVertexBuffer, EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT)));
            cmd->copyBuffer(*vbpStagingBuffer, *vtxPosBuffer,
                            vk::BufferCopy().setSize(vertexPositions.size() * sizeof(vertexPositions[0])));

            // Handle vertex attributes.
            auto vabStagingBuffer = MakeUnique<GfxBuffer>(
                gfxContext->GetDevice(),
                GfxBufferDescription(vertexAttributes.size() * sizeof(vertexAttributes[0]), sizeof(vertexAttributes[0]),
                                     vk::BufferUsageFlagBits::eTransferSrc, EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT));
            vabStagingBuffer->SetData(vertexAttributes.data(), vertexAttributes.size() * sizeof(vertexAttributes[0]));

            auto& vtxAttribBuffer = VertexAttributeBuffers.emplace_back(MakeShared<GfxBuffer>(
                gfxContext->GetDevice(),
                GfxBufferDescription(vertexAttributes.size() * sizeof(vertexAttributes[0]), sizeof(vertexAttributes[0]),
                                     vk::BufferUsageFlagBits::eVertexBuffer, EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT)));
            cmd->copyBuffer(*vabStagingBuffer, *vtxAttribBuffer,
                            vk::BufferCopy().setSize(vertexAttributes.size() * sizeof(vertexAttributes[0])));

            // Handle indices with different types.
            const auto ibBufferElementSize =
                currentMeshAsset->IndexType == vk::IndexType::eUint32 ? sizeof(indicesUint32[0]) : sizeof(indicesUint16[0]);
            const auto ibBufferSize = currentMeshAsset->IndexType == vk::IndexType::eUint32 ? indicesUint32.size() * ibBufferElementSize
                                                                                            : indicesUint16.size() * ibBufferElementSize;
            auto ibStagingBuffer    = MakeUnique<GfxBuffer>(
                gfxContext->GetDevice(), GfxBufferDescription(ibBufferSize, ibBufferElementSize, vk::BufferUsageFlagBits::eTransferSrc,
                                                                 EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT));
            ibStagingBuffer->SetData(currentMeshAsset->IndexType == vk::IndexType::eUint32 ? (const void*)indicesUint32.data()
                                                                                           : (const void*)indicesUint16.data(),
                                     ibBufferSize);

            auto& ibBuffer = IndexBuffers.emplace_back(MakeShared<GfxBuffer>(
                gfxContext->GetDevice(), GfxBufferDescription(ibBufferSize, ibBufferElementSize, vk::BufferUsageFlagBits::eIndexBuffer,
                                                              EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT)));
            cmd->copyBuffer(*ibStagingBuffer, *ibBuffer, vk::BufferCopy().setSize(ibBufferSize));

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

        // Load material buffers(marking as ReBAR btw).
        for (const auto& [_, gltfMaterial] : materialMap)
        {
            MaterialBuffers.emplace_back(MakeShared<GfxBuffer>(
                gfxContext->GetDevice(),
                GfxBufferDescription(sizeof(Shaders::GLTFMaterial), sizeof(Shaders::GLTFMaterial), vk::BufferUsageFlagBits::eUniformBuffer,
                                     EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT)));
            MaterialBuffers.back()->SetData(&gltfMaterial, sizeof(gltfMaterial));
        }
    }

}  // namespace Radiant
