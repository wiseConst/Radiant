#include <pch.hpp>
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

    namespace MeshUtils
    {
        static AABB GenerateAABB(const std::vector<VertexPosition>& positions) noexcept
        {
            RDNT_ASSERT(!positions.empty(), "Can't generate AABB from empty positions vector!");
            glm::vec3 min{0.0f};
            glm::vec3 max{0.0f};

            // NOTE: It assumes AVX2 instructions are supported.
#define AABB_GENERATOR_USE_AVX2 1
#if _MSC_VER && AABB_GENERATOR_USE_AVX2
            size_t i       = 0;
            __m256 minVecX = _mm256_set1_ps(std::numeric_limits<f32>::max()), minVecY = _mm256_set1_ps(std::numeric_limits<f32>::max()),
                   minVecZ = _mm256_set1_ps(std::numeric_limits<f32>::max());
            __m256 maxVecX = _mm256_set1_ps(std::numeric_limits<f32>::lowest()),
                   maxVecY = _mm256_set1_ps(std::numeric_limits<f32>::lowest()),
                   maxVecZ = _mm256_set1_ps(std::numeric_limits<f32>::lowest());

            const size_t alignedDataSize = (positions.size() & ~7);
            for (i = 0; i < alignedDataSize; i += 8)
            {
                const auto* pointPtr = &positions[i];

                const __m256 pointX =
                    _mm256_set_ps(pointPtr[7].Position.x, pointPtr[6].Position.x, pointPtr[5].Position.x, pointPtr[4].Position.x,
                                  pointPtr[3].Position.x, pointPtr[2].Position.x, pointPtr[1].Position.x, pointPtr[0].Position.x);
                const __m256 pointY =
                    _mm256_set_ps(pointPtr[7].Position.y, pointPtr[6].Position.y, pointPtr[5].Position.y, pointPtr[4].Position.y,
                                  pointPtr[3].Position.y, pointPtr[2].Position.y, pointPtr[1].Position.y, pointPtr[0].Position.y);
                const __m256 pointZ =
                    _mm256_set_ps(pointPtr[7].Position.z, pointPtr[6].Position.z, pointPtr[5].Position.z, pointPtr[4].Position.z,
                                  pointPtr[3].Position.z, pointPtr[2].Position.z, pointPtr[1].Position.z, pointPtr[0].Position.z);

                minVecX = _mm256_min_ps(minVecX, pointX);
                minVecY = _mm256_min_ps(minVecY, pointY);
                minVecZ = _mm256_min_ps(minVecZ, pointZ);

                maxVecX = _mm256_max_ps(maxVecX, pointX);
                maxVecY = _mm256_max_ps(maxVecY, pointY);
                maxVecZ = _mm256_max_ps(maxVecZ, pointZ);
            }

            // Gather results and prepare them.
            f32 minX[8] = {0.0f}, minY[8] = {0.0f}, minZ[8] = {0.0f};
            _mm256_storeu_ps(minX, minVecX);
            _mm256_storeu_ps(minY, minVecY);
            _mm256_storeu_ps(minZ, minVecZ);

            f32 maxX[8] = {0.0f}, maxY[8] = {0.0f}, maxZ[8] = {0.0f};
            _mm256_storeu_ps(maxX, maxVecX);
            _mm256_storeu_ps(maxY, maxVecY);
            _mm256_storeu_ps(maxZ, maxVecZ);

            min = glm::vec3(minX[0], minY[0], minZ[0]);
            max = glm::vec3(maxX[0], maxY[0], maxZ[0]);
            for (i = 1; i < 8; ++i)
            {
                min = glm::min(min, glm::vec3{minX[i], minY[i], minZ[i]});
                max = glm::max(max, glm::vec3{maxX[i], maxY[i], maxZ[i]});
            }

            // Take into account the remainder.
            for (i = alignedDataSize; i < positions.size(); ++i)
            {
                min = glm::min(positions[i].Position, min);
                max = glm::max(positions[i].Position, max);
            }
#else
            min = glm::vec3(std::numeric_limits<f32>::max());
            max = glm::vec3(std::numeric_limits<f32>::lowest());
            for (const auto& point : points)
            {
                min = glm::min(point.Position, min);
                max = glm::max(point.Position, max);
            }
#endif

            return {.Min = min, .Max = max};
        }

        static Sphere GenerateBoundingSphere(const std::vector<VertexPosition>& positions) noexcept
        {
            RDNT_ASSERT(!positions.empty(), "Can't generate bounding sphere from empty positions vector!");

            // First pass - find averaged vertex pos.
            glm::vec3 averagedVertexPos(0.0f);
            for (const auto& point : positions)
                averagedVertexPos += point.Position;

            averagedVertexPos /= positions.size();
            const auto aabb       = GenerateAABB(positions);
            const auto aabbCenter = (aabb.Max + aabb.Min) * 0.5f;

            // Second pass - find farthest vertices for both averaged vertex position and AABB centroid.
            glm::vec3 farthestVtx[2] = {positions[0].Position, positions[0].Position};
            for (const auto& point : positions)
            {
                if (glm::distance2(averagedVertexPos, point.Position) > glm::distance2(averagedVertexPos, farthestVtx[0]))
                    farthestVtx[0] = point.Position;
                if (glm::distance2(aabbCenter, point.Position) > glm::distance2(aabbCenter, farthestVtx[1]))
                    farthestVtx[1] = point.Position;
            }

            const f32 averagedVtxToFarthestDistance  = glm::distance(farthestVtx[0], averagedVertexPos);
            const f32 aabbCentroidToFarthestDistance = glm::distance(farthestVtx[1], aabbCenter);

            const Sphere sphere = {.Origin =
                                       averagedVtxToFarthestDistance < aabbCentroidToFarthestDistance ? averagedVertexPos : aabbCenter,
                                   .Radius = glm::min(averagedVtxToFarthestDistance, aabbCentroidToFarthestDistance)};
            return sphere;
        }

    }  // namespace MeshUtils

    namespace MeshoptimizerUtils
    {

        template <typename T>
        static constexpr void RemapVertexStream(const u64 uniqueVertexCount, std::vector<T>& vertexStream,
                                                const std::vector<u32>& remapTable) noexcept
        {
            std::vector<T> newVertexStream(uniqueVertexCount);
            meshopt_remapVertexBuffer(newVertexStream.data(), vertexStream.data(), vertexStream.size(), sizeof(T), remapTable.data());
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

        constexpr bool c_bGenerateMipMaps      = true;
        constexpr bool c_bUseSamplerAnisotropy = true;
        // NOTE: For simplicity, usage of the same texture with multiple samplers isn't supported at least for now!
        NODISCARD static std::string LoadTexture(std::mutex& loaderMutex, UnorderedMap<std::string, Shared<GfxTexture>>& textureMap,
                                                 const std::filesystem::path& meshParentPath, const Unique<GfxContext>& gfxContext,
                                                 const fastgltf::Asset& asset, const fastgltf::Texture& texture,
                                                 const std::optional<vk::SamplerCreateInfo>& samplerCI, const vk::Format format) noexcept
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

            std::string textureName{s_DEFAULT_STRING};
            Shared<GfxTexture> loadedTexture{nullptr};
            std::visit(
                fastgltf::visitor{
                    [](auto& arg) { RDNT_ASSERT(false, "fastgltf: Default argument when loading image! This shouldn't happen!"); },
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

                        std::vector<GfxTextureUtils::TextureCompressor::TextureInfo> mips{};
                        if constexpr (s_bUseTextureCompressionBC)
                        {
                            mips = GfxTextureUtils::TextureCompressor::LoadTextureCache(textureFilePath.string(), format);
                        }
                        else
                        {
                            i32 width{1}, height{1}, channels{4};

                            void* stbImageData = GfxTextureUtils::LoadImage(textureFilePath.string(), width, height, channels);
                            RDNT_ASSERT(stbImageData, "fastgltf: Failed to load image data!");

                            auto& mip      = mips.emplace_back();
                            mip.Dimensions = glm::uvec2{width, height};

                            const auto stbImageDataSizeBytes = static_cast<u64>(width * height * channels * sizeof(u8));
                            mip.Data.resize(stbImageDataSizeBytes);
                            std::memcpy(mip.Data.data(), stbImageData, stbImageDataSizeBytes);

                            GfxTextureUtils::UnloadImage(stbImageData);
                        }

                        u32 width = mips[0].Dimensions.x, height = mips[0].Dimensions.y;

                        {
                            std::scoped_lock lock(loaderMutex);  // Synchronizing access to textureMap by loading actual texture
                            loadedTexture = MakeShared<GfxTexture>(
                                gfxContext->GetDevice(),
                                GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(width, height, 1), format,
                                                      vk::ImageUsageFlagBits::eTransferDst, samplerCI, 1, vk::SampleCountFlagBits::e1,
                                                      EResourceCreateBits::RESOURCE_CREATE_DONT_TOUCH_SAMPLED_IMAGES_BIT |
                                                          (c_bGenerateMipMaps ? EResourceCreateBits::RESOURCE_CREATE_CREATE_MIPS_BIT : 0)));
                            textureMap[textureName] = loadedTexture;
                            gfxContext->GetDevice()->SetDebugName(textureName, (const vk::Image&)*loadedTexture);
                        }

                        auto executionContext = gfxContext->CreateImmediateExecuteContext(ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL);
                        executionContext.CommandBuffer.begin(
                            vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

                        // NOTE: Currently BCn mips are loaded by hand, and RGBA8 are blitted, so mipsToIterateCount will be > 1 for BCn.
                        const u32 mipCount            = c_bGenerateMipMaps
                                                            ? glm::max(GfxTextureUtils::GetMipLevelCount(width, height), static_cast<u32>(mips.size()))
                                                            : 1;
                        const auto mipsToIterateCount = s_bUseTextureCompressionBC ? mipCount : 1;

                        executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                            vk::ImageMemoryBarrier2()
                                .setImage(*loadedTexture)
                                .setSubresourceRange(vk::ImageSubresourceRange()
                                                         .setBaseArrayLayer(0)
                                                         .setBaseMipLevel(0)
                                                         .setLevelCount(mipCount)
                                                         .setLayerCount(1)
                                                         .setAspectMask(vk::ImageAspectFlagBits::eColor))
                                .setOldLayout(vk::ImageLayout::eUndefined)
                                .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                                .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                                .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                                .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
                                .setDstStageMask(vk::PipelineStageFlagBits2::eAllTransfer)));

                        std::vector<Unique<GfxBuffer>> stagingBuffers(mipsToIterateCount);
                        for (u32 i{}; i < mipsToIterateCount; ++i)
                        {
                            const u64 imageSize = mips[i].Data.size() * sizeof(mips[i].Data[0]);

                            auto& stagingBuffer = stagingBuffers[i];
                            stagingBuffer       = MakeUnique<GfxBuffer>(gfxContext->GetDevice(),
                                                                  GfxBufferDescription(imageSize, /* placeholder */ 1,
                                                                                             vk::BufferUsageFlagBits::eTransferSrc,
                                                                                             EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT));
                            stagingBuffer->SetData(mips[i].Data.data(), imageSize);

                            executionContext.CommandBuffer.copyBufferToImage(
                                *stagingBuffer, *loadedTexture, vk::ImageLayout::eTransferDstOptimal,
                                vk::BufferImageCopy()
                                    .setImageSubresource(vk::ImageSubresourceLayers()
                                                             .setLayerCount(1)
                                                             .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                             .setBaseArrayLayer(0)
                                                             .setMipLevel(i))
                                    .setImageExtent(vk::Extent3D(mips[i].Dimensions.x, mips[i].Dimensions.y, 1)));
                        }

                        if (c_bGenerateMipMaps && !s_bUseTextureCompressionBC)
                            loadedTexture->GenerateMipMaps(executionContext.CommandBuffer);
                        else
                        {
                            executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                                vk::ImageMemoryBarrier2()
                                    .setImage(*loadedTexture)
                                    .setSubresourceRange(vk::ImageSubresourceRange()
                                                             .setBaseArrayLayer(0)
                                                             .setBaseMipLevel(0)
                                                             .setLevelCount(mipsToIterateCount)
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
                        GfxContext::Get().SubmitImmediateExecuteContext(executionContext);
                    },
                    [&](const fastgltf::sources::Vector& vector)
                    {
                        RDNT_ASSERT(false, "{}: NOT IMPLEMENTED!", __FUNCTION__);

                        i32 width{1}, height{1}, channels{4};

                        void* imageData = GfxTextureUtils::LoadImage(vector.bytes.data(), vector.bytes.size(), width, height, channels);
                        RDNT_ASSERT(imageData, "Failed to load image data!");

                        GfxTextureUtils::UnloadImage(imageData);
                    },
                    [&](const fastgltf::sources::BufferView& view)
                    {
                        RDNT_ASSERT(false, "{}: NOT IMPLEMENTED!", __FUNCTION__);

                        const auto& bufferView = asset.bufferViews[view.bufferViewIndex];
                        const auto& buffer     = asset.buffers[bufferView.bufferIndex];

                        i32 width{1}, height{1}, channels{4};
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
                    .setMaxLod(FastGltfUtils::c_bGenerateMipMaps ? vk::LodClampNone : 0.0f)
                    .setMagFilter(FastGltfUtils::ConvertFilterToVulkan(currentSamplerInfo.magFilter.value_or(fastgltf::Filter::Linear)))
                    .setMinFilter(FastGltfUtils::ConvertFilterToVulkan(currentSamplerInfo.minFilter.value_or(fastgltf::Filter::Linear)))
                    .setMipmapMode(
                        FastGltfUtils::ConvertMipMapModeToVulkan(currentSamplerInfo.minFilter.value_or(fastgltf::Filter::Linear)))
                    .setAddressModeU(FastGltfUtils::ConvertWrapToVulkan(currentSamplerInfo.wrapS))
                    .setAddressModeV(FastGltfUtils::ConvertWrapToVulkan(currentSamplerInfo.wrapT))
                    .setAddressModeW(FastGltfUtils::ConvertWrapToVulkan(currentSamplerInfo.wrapT))
                    .setBorderColor(vk::BorderColor::eFloatOpaqueWhite)
                    .setAnisotropyEnable(FastGltfUtils::c_bUseSamplerAnisotropy)
                    .setMaxAnisotropy(FastGltfUtils::c_bUseSamplerAnisotropy
                                          ? gfxContext->GetDevice()->GetGPUProperties().limits.maxSamplerAnisotropy
                                          : 0.0f);
        }

        UnorderedMap<u64, vk::Format> imageIndexToFormatMap{};
        {
            // Prepare textures for compression.
            GfxTextureUtils::TextureCompressor textureCompressor = {};
            // Need to track image IDs to compress by myself to avoid duplicates.
            UnorderedSet<u64> queuedTextures;
            for (const auto& material : asset->materials)
            {
                constexpr auto albedoEmissiveFormat = vk::Format::eBc7UnormBlock;
                constexpr auto occlusionFormat      = vk::Format::eBc4UnormBlock;
                constexpr auto normalMapFormat      = vk::Format::eBc5UnormBlock;

                const auto pushTextureFunc = [&](const auto& texture, const auto format) noexcept
                {
                    // I don't use any rough extensions, so it's guaranteed by fastgltf to have an image index.
                    const auto imageIndex = *texture.imageIndex;

                    if (!queuedTextures.contains(imageIndex))
                    {
                        queuedTextures.emplace(imageIndex);
                        if constexpr (!s_bUseTextureCompressionBC) return;

                        imageIndexToFormatMap[imageIndex] = format;

                        std::visit(
                            fastgltf::visitor{
                                [](const auto& arg)
                                { RDNT_ASSERT(false, "fastgltf: Default argument when loading image! This shouldn't happen!"); },
                                [&](const fastgltf::sources::URI& filePath)
                                {
                                    RDNT_ASSERT(filePath.fileByteOffset == 0, "fastgltf: We don't support offsets with stbi!");
                                    RDNT_ASSERT(filePath.uri.isLocalPath(), "fastgltf: We're only capable of loading local files!");

                                    const auto textureName = filePath.uri.path();
                                    RDNT_ASSERT(!textureName.empty(), "fastgltf: Texture name is empty!");

                                    const auto textureFilePath = meshParentPath / textureName;
                                    textureCompressor.PushTextureIntoBatchList(textureFilePath.string(), format);
                                }},
                            asset->images[imageIndex].data);
                    }
                };

                if (material.pbrData.baseColorTexture.has_value())
                {
                    const auto textureIndex = material.pbrData.baseColorTexture->textureIndex;
                    pushTextureFunc(asset->textures[textureIndex], albedoEmissiveFormat);
                }

                if (material.normalTexture.has_value())
                {
                    const auto textureIndex = material.normalTexture->textureIndex;
                    pushTextureFunc(asset->textures[textureIndex], normalMapFormat);
                }

                if (material.emissiveTexture.has_value())
                {
                    const auto textureIndex = material.emissiveTexture->textureIndex;
                    pushTextureFunc(asset->textures[textureIndex], albedoEmissiveFormat);
                }

                if (material.occlusionTexture.has_value())
                {
                    const auto textureIndex = material.occlusionTexture->textureIndex;
                    pushTextureFunc(asset->textures[textureIndex], occlusionFormat);
                }

                // NOTE: For now metallic/roughness stored in BC1.
                constexpr auto metallicRoughnessFormat = vk::Format::eBc1RgbUnormBlock;
                if (material.pbrData.metallicRoughnessTexture.has_value())
                {
                    const auto textureIndex = material.pbrData.metallicRoughnessTexture->textureIndex;
                    pushTextureFunc(asset->textures[textureIndex], metallicRoughnessFormat);
                }
            }

            textureCompressor.CompressAndCache();
        }

        // NOTE: textureIndex -> name, since multiple materials can reference the same textures but with different samplers, so
        // there's no need to load same texture N times.
        UnorderedMap<u64, std::string> textureNameLUT;
        // Parallel texture loading.
        {
            std::vector<std::future<std::string>> textureFutures;
            std::mutex loaderMutex          = {};
            const auto textureLoadBeginTime = Timer::Now();
            for (u32 textureIndex{}; textureIndex < asset->textures.size(); ++textureIndex)
            {
                textureFutures.emplace_back(Application::Get().GetThreadPool()->Submit(
                    [&, textureIndex]() noexcept
                    {
                        auto& texture = asset->textures[textureIndex];
                        std::optional<vk::SamplerCreateInfo> samplerCI{std::nullopt};
                        if (texture.samplerIndex.has_value()) samplerCI = samplerCIs[*texture.samplerIndex];

                        return FastGltfUtils::LoadTexture(
                            loaderMutex, TextureMap, meshParentPath, gfxContext, asset.get(), texture, samplerCI,
                            s_bUseTextureCompressionBC ? imageIndexToFormatMap[*asset->textures[textureIndex].imageIndex]
                                                       : vk::Format::eR8G8B8A8Unorm);
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
        std::vector<VertexPosition> vertexPositionsPerPrimitive;  // Used only for bounding sphere generation.
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

                vertexPositionsPerPrimitive.clear();
                currentMeshAsset->Surfaces.emplace_back(static_cast<u32>(indicesUint32.size()),
                                                        static_cast<u32>(asset->accessors[*primitive.indicesAccessor].count), Sphere{},
                                                        static_cast<u32>(primitive.materialIndex.value_or(0)),
                                                        FastGltfUtils::ConvertPrimitiveTypeToVulkanPrimitiveTopology(primitive.type));

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
                    const u32 prevIndexOffset   = indicesUint32.size();
                    u32 idxCounter{0};
                    indicesUint32.resize(indicesUint32.size() + indicesAccessor.count);

                    fastgltf::iterateAccessor<u32>(asset.get(), indicesAccessor,
                                                   [&](u32 idx)
                                                   {
                                                       auto& currentIndexBufferValue = indicesUint32[prevIndexOffset + idxCounter];
                                                       currentIndexBufferValue       = initialVertexIndex + idx;
                                                       ++idxCounter;
                                                   });

                    // NOTE: Check if index > u16/u8::max() and then switch to index type u32/u16.
                    const u32 maxIdx = *std::max_element(indicesUint32.cbegin(), indicesUint32.cend());
                    if (currentMeshAsset->IndexType == vk::IndexType::eNoneKHR) currentMeshAsset->IndexType = vk::IndexType::eUint8EXT;

                    if (maxIdx >= std::numeric_limits<u8>::max()) currentMeshAsset->IndexType = vk::IndexType::eUint16;
                    if (maxIdx >= std::numeric_limits<u16>::max()) currentMeshAsset->IndexType = vk::IndexType::eUint32;
                }

                // Load vertex positions.
                {
                    const auto& posAccessor = asset->accessors[positionIt->accessorIndex];
                    vertexPositionsPerPrimitive.resize(posAccessor.count);
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), posAccessor,
                                                                  [&](const glm::vec3& v, const u64 index)
                                                                  { vertexPositionsPerPrimitive[index].Position = v; });
                    currentMeshAsset->Surfaces.back().Bounds = MeshUtils::GenerateBoundingSphere(vertexPositionsPerPrimitive);

                    // Extend current vertex buffers
                    vertexPositions.resize(vertexPositions.size() + posAccessor.count);
                    vertexAttributes.resize(vertexAttributes.size() + posAccessor.count);

                    for (u64 vtxIndex{}; vtxIndex < posAccessor.count; ++vtxIndex)
                    {
                        vertexPositions[initialVertexIndex + vtxIndex].Position = vertexPositionsPerPrimitive[vtxIndex].Position;
                    }
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

            // I store indices as uint32, but in case index type is different, then encoding also different.
            // Handle indices with different types.
            u64 ibElementSize{sizeof(indicesUint32[0])};
            u64 ibSize{indicesUint32.size() * ibElementSize};
            void* ibData{indicesUint32.data()};
            std::vector<u16> indicesUint16{};
            std::vector<u8> indicesUint8{};
            if (currentMeshAsset->IndexType == vk::IndexType::eUint32)
            {
                MeshoptimizerUtils::OptimizeMesh(indicesUint32, vertexPositions, vertexAttributes);
            }
            else if (currentMeshAsset->IndexType == vk::IndexType::eUint16)
            {
                indicesUint16.resize(indicesUint32.size());
                for (u32 i{}; i < indicesUint32.size(); ++i)
                {
                    indicesUint16[i] = static_cast<u16>(indicesUint32[i]);
                }
                MeshoptimizerUtils::OptimizeMesh(indicesUint16, vertexPositions, vertexAttributes);

                ibElementSize = sizeof(indicesUint16[0]);
                ibSize        = indicesUint16.size() * ibElementSize;
                ibData        = indicesUint16.data();
            }
            else if (currentMeshAsset->IndexType == vk::IndexType::eUint8EXT)
            {
                indicesUint8.resize(indicesUint32.size());
                for (u32 i{}; i < indicesUint32.size(); ++i)
                {
                    indicesUint8[i] = static_cast<u8>(indicesUint32[i]);
                }
                MeshoptimizerUtils::OptimizeMesh(indicesUint8, vertexPositions, vertexAttributes);

                ibElementSize = sizeof(indicesUint8[0]);
                ibSize        = indicesUint8.size() * ibElementSize;
                ibData        = indicesUint8.data();
            }

            const auto [cmd, queue] =
                gfxContext->AllocateSingleUseCommandBufferWithQueue(ECommandQueueType::COMMAND_QUEUE_TYPE_DEDICATED_TRANSFER);
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

            auto ibStagingBuffer = MakeUnique<GfxBuffer>(gfxContext->GetDevice(),
                                                         GfxBufferDescription(ibSize, ibElementSize, vk::BufferUsageFlagBits::eTransferSrc,
                                                                              EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT));
            ibStagingBuffer->SetData(ibData, ibSize);

            auto& ibBuffer = IndexBuffers.emplace_back(MakeShared<GfxBuffer>(
                gfxContext->GetDevice(), GfxBufferDescription(ibSize, ibElementSize, vk::BufferUsageFlagBits::eIndexBuffer,
                                                              EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT)));
            cmd->copyBuffer(*ibStagingBuffer, *ibBuffer, vk::BufferCopy().setSize(ibSize));

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
                                                 "fastgltf: Default argument when parsing transformation matrices! This shouldn't happen!");
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
