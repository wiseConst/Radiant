#include <pch.h>
#include "CombinedRenderer.hpp"

#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

#include <clustered_shading/light_clusters_defines.hpp>
#include <csm/csm_defines.hpp>
#include <ssao/ssao_defines.hpp>
#include <bloom/bloom_defines.hpp>

namespace Radiant
{
    namespace ResourceNames
    {
        const std::string ShadowsDepthBoundsBuffer{"Resource_Shadows_Depth_Bounds_Buffer"};
        const std::string LightBuffer{"Resource_Light_Buffer"};
        const std::string CameraBuffer{"Resource_Camera_Buffer"};
        const std::string CSMDataBuffer{"Resource_CSMDataBuffer"};
        const std::string MainPassShaderDataBuffer{"Resource_MainPassShaderDataBuffer"};

        const std::string GBufferDepth{"Resource_DepthBuffer"};
        const std::string GBufferAlbedo{"Resource_LBuffer"};

        const std::string FinalPassTexture{"Resource_Final_Texture"};

        const std::string SSSTexture{"Resource_ScreenSpaceShadows"};
        const std::string SSAOTexture{"Resource_SSAO"};
        const std::string SSAOTextureBlurred{"Resource_SSAO_Blurred"};

        const std::string LightClusterDetectActiveBuffer{
            "Resource_Light_Cluster_Detect_Active_Buffer"};                     // Light cluster buffer after build stage
        const std::string LightClusterBuffer{"Resource_Light_Cluster_Buffer"};  // Light cluster buffer after detect active stage
        const std::string LightClusterListBuffer{
            "Resource_Light_Cluster_List_Buffer"};  // Light cluster list filled with light indices after cluster assignment stage

    }  // namespace ResourceNames

    static bool s_bEnableSSAO{false};
    static bool s_bSSAOComputeBased{false};
    static bool s_bBloomComputeBased{false};
    static bool s_bUpdateLights{true};
    static glm::vec3 s_SunColor{1.0f};

    static f32 s_MeshScale = 0.01f;

    static bool s_bComputeTightBounds{false};  // switches whole csm pipeline to GPU.(setup shadows, etc..)
    static bool s_bKeepCascadeConstantSize{false};
    static bool s_bKeepCascadeSquared{false};
    static bool s_bRoundCascadeToPixelSize{false};
    static f32 s_CascadeSplitDelta{0.955f};

    static u64 s_DrawCallCount{0};

    static constexpr glm::vec3 s_MinPointLightPos{-15, -4, -5};
    static constexpr glm::vec3 s_MaxPointLightPos{15, 14, 5};

    /*
        Calculate frustum split depths and matrices for the shadow map cascades
        Based on https://johanmedestrom.wordpress.com/2016/03/18/opengl-cascaded-shadow-maps/
    */
    static Shaders::CascadedShadowMapsData UpdateCSM(const glm::vec3& sunDirection, const f32 zNear, const f32 zFar, const f32 cameraZoom,
                                                     const f32 cameraAR, const glm::mat4& cameraView) noexcept
    {
        const auto viewProjMain = glm::perspective(glm::radians(cameraZoom), cameraAR, zNear, zFar) * cameraView;
        const auto invViewProj  = glm::inverse(viewProjMain);

#define OUTPUT_CSM_SPLITS_FRUSTUMS 0
        Shaders::CascadedShadowMapsData csmData = {};
        const f32 range                         = zFar - zNear;
        const f32 ratio                         = zFar / zNear;

        // Calculate split depths based on view camera frustum
        // Based on method presented in
        // https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus
        for (u32 splitIndex{}; splitIndex < SHADOW_MAP_CASCADE_COUNT; ++splitIndex)
        {
            const f32 p                       = (splitIndex + 1) / static_cast<f32>(SHADOW_MAP_CASCADE_COUNT);
            const f32 log                     = zNear * glm::pow(ratio, p);
            const f32 uniform                 = zNear + range * p;
            const f32 d                       = uniform + s_CascadeSplitDelta * (log - uniform);
            csmData.CascadeSplits[splitIndex] = (d - zNear) / range;
        }

        f32 lastCascadeSplit = 0.f;
        for (u32 cascadeIndex{}; cascadeIndex < SHADOW_MAP_CASCADE_COUNT; ++cascadeIndex)
        {
            f32 splitDist                           = csmData.CascadeSplits[cascadeIndex];
            std::array<glm::vec3, 8> frustumCorners = {
                glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(-1.0f, -1.0f, 0.0f),
                glm::vec3(-1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f, -1.0f, 1.0f), glm::vec3(-1.0f, -1.0f, 1.0f),
            };

            // Project frustum corners into world space
            for (u32 j = 0; j < 8; ++j)
            {
                glm::vec4 invCorner = invViewProj * glm::vec4(frustumCorners[j], 1.0f);
                frustumCorners[j]   = invCorner / invCorner.w;
            }

            // Get frustum part based on last & current splits.
            for (u32 j = 0; j < 4; ++j)
            {
                const auto dist       = frustumCorners[j + 4] - frustumCorners[j];
                frustumCorners[j + 4] = frustumCorners[j] + (dist * splitDist);
                frustumCorners[j]     = frustumCorners[j] + (dist * lastCascadeSplit);
            }

#if OUTPUT_CSM_SPLITS_FRUSTUMS
            LOG_WARN("cascade: {}", cascadeIndex);
            for (uint32_t j = 0; j < 8; j++)
            {
                LOG_WARN("{}", glm::to_string(frustumCorners[j]));
            }
#endif

            // Get frustum center
            glm::vec3 frustumCenter = glm::vec3(0.0f);
            for (u32 j = 0; j < 8; j++)
            {
                frustumCenter += frustumCorners[j];
            }
            frustumCenter /= 8.0f;

            glm::vec3 minPointLVS{std::numeric_limits<f32>::max()};
            glm::vec3 maxPointLVS{std::numeric_limits<f32>::lowest()};
            for (const auto& frustumCorner : frustumCorners)
            {
                minPointLVS = glm::min(minPointLVS, frustumCorner);
                maxPointLVS = glm::max(maxPointLVS, frustumCorner);
            }

            glm::vec3 minExtents = minPointLVS;
            glm::vec3 maxExtents = maxPointLVS;

#if 1
            f32 radius = 0.0f;
            for (u32 j = 0; j < 8; ++j)
            {
                const f32 distance = glm::length(frustumCorners[j] - frustumCenter);
                radius             = glm::max(radius, distance);
            }
            radius              = std::ceil(radius * 16.0f) / 16.0f;
            const f32 pixelSize = radius / SHADOW_MAP_CASCADE_SIZE;
            radius              = glm::round(radius / pixelSize) * pixelSize;

            maxExtents = glm::vec3(radius);
            minExtents = -maxExtents;
#endif
            f32 actualSize{0.f};
            if (s_bKeepCascadeConstantSize)
            {
                // keep constant world-size resolution, side length = diagonal of largest face of frustum
                // the other option looks good at high resolutions, but can result in shimmering as you look in different directions and the
                // cascade changes size
                float farFaceDiagonal = glm::length(frustumCorners[4] - frustumCorners[1]);
                float forwardDiagonal = glm::length(frustumCorners[4] - frustumCorners[0]);
                actualSize            = std::max(farFaceDiagonal, forwardDiagonal);

                maxExtents.x = actualSize;
                maxExtents.y = actualSize;

                minExtents.x = -actualSize;
                minExtents.y = -actualSize;
            }

            const auto lightViewMatrix =
                glm::lookAt(frustumCenter + sunDirection + Shaders::s_KINDA_SMALL_NUMBER, frustumCenter, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat4 lightOrthoMatrix = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z, minExtents.z) *
                                         glm::scale(glm::vec3(1.0f, -1.0f, 1.0f));

            // Store split distance and matrix in cascade
            csmData.CascadeSplits[cascadeIndex]        = (zNear + splitDist * range) * -1.0f;
            csmData.ViewProjectionMatrix[cascadeIndex] = lightOrthoMatrix * lightViewMatrix;
            lastCascadeSplit                           = splitDist;

#if 0
            // In order to avoid light shimmering we need to create a rounding matrix so we move in texel sized increments.
            // You can think of it as finding out how much we need to move the orthographic matrix so it matches up with shadow map,
            // it is done like this:
            glm::mat4 shadowMatrix = lightOrthoMatrix * lightViewMatrix;
            glm::vec4 shadowOrigin = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            shadowOrigin           = shadowMatrix * shadowOrigin;
            shadowOrigin *= SHADOW_MAP_CASCADE_SIZE / 2.0f;

            glm::vec4 roundedOrigin = glm::round(shadowOrigin);
            glm::vec4 roundOffset   = roundedOrigin - shadowOrigin;
            roundOffset *= 2.0f / SHADOW_MAP_CASCADE_SIZE;
            roundOffset.z = 0.0f;
            roundOffset.w = 0.0f;

            glm::mat4 shadowProj = lightOrthoMatrix;
            shadowProj[3] += roundOffset;
            lightOrthoMatrix = shadowProj;

            csmData.ViewProjectionMatrix[cascadeIndex] = lightOrthoMatrix * lightViewMatrix;
#endif

#if OUTPUT_CSM_SPLITS_FRUSTUMS
            LOG_WARN("Calculated split: {}", csmData.CascadeSplits[cascadeIndex]);
#endif
        }

#if 1
        const auto GetLightSpaceViewProjectionMatrixFunc = [&](const f32 zNear, const f32 zFar) noexcept
        {
            const auto GetFrustumCornersWorldSpaceFunc = [](const glm::mat4& projection, const glm::mat4& view) noexcept
            {
                const auto invViewProj = glm::inverse(projection * view);
                std::vector<glm::vec3> frustumCornersWS;
                for (u32 x{}; x < 2; ++x)
                {
                    for (u32 y{}; y < 2; ++y)
                    {
                        for (u32 z{}; z < 2; ++z)
                        {
                            const auto cornerWS = invViewProj * glm::vec4(x * 2.0f - 1.0f, y * 2.0f - 1.0f, /*(f32)z*/ z * 2.0 - 1.0, 1.0f);
                            frustumCornersWS.emplace_back(cornerWS / cornerWS.w);
                        }
                    }
                }

                return frustumCornersWS;
            };

            const auto subfrustumProjectionMatrix = glm::perspective(glm::radians(cameraZoom), cameraAR, zNear, zFar);
            const auto frustumCornersWS           = GetFrustumCornersWorldSpaceFunc(subfrustumProjectionMatrix, cameraView);
            glm::vec3 frustumCenter{0.0f};
            for (const auto& frustumCornerWS : frustumCornersWS)
                frustumCenter += frustumCornerWS;

            frustumCenter /= frustumCornersWS.size();

            glm::vec3 minExtents{std::numeric_limits<f32>::max()};
            glm::vec3 maxExtents{std::numeric_limits<f32>::lowest()};

            if (s_bKeepCascadeConstantSize)
            {
                for (const auto& frustumCornerWS : frustumCornersWS)
                {
                    minExtents = glm::min(minExtents, frustumCornerWS);
                    maxExtents = glm::max(maxExtents, frustumCornerWS);
                }
            }

            if (s_bRoundCascadeToPixelSize)
            {
                const glm::vec3 pixelSize = frustumCenter / glm::vec3(SHADOW_MAP_CASCADE_SIZE);
                frustumCenter             = glm::round(frustumCenter / pixelSize) * pixelSize;
            }

            const auto lightViewMatrix = glm::lookAt(frustumCenter + sunDirection, frustumCenter, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::vec3 minPointLVS{std::numeric_limits<f32>::max()};
            glm::vec3 maxPointLVS{std::numeric_limits<f32>::lowest()};
            for (const auto& frustumCornerWS : frustumCornersWS)
            {
                const auto pointLVS = glm::vec3(lightViewMatrix * glm::vec4(frustumCornerWS, 1.0f));
                minPointLVS         = glm::min(minPointLVS, pointLVS);
                maxPointLVS         = glm::max(maxPointLVS, pointLVS);
            }

            if (!s_bKeepCascadeConstantSize)
            {
                // Tune this parameter according to the scene
                constexpr float zMult = 20.0f;
                if (minPointLVS.z < 0)
                    minPointLVS.z *= zMult;
                else
                    minPointLVS.z /= zMult;

                if (maxPointLVS.z < 0)
                    maxPointLVS.z /= zMult;
                else
                    maxPointLVS.z *= zMult;

                maxExtents = maxPointLVS;
                minExtents = minPointLVS;
            }

            if (s_bKeepCascadeSquared)
            {
                const f32 maxHorizontal = glm::max(glm::abs(minExtents.x), glm::abs(maxExtents.x));
                const f32 maxVertical   = glm::max(glm::abs(minExtents.y), glm::abs(maxExtents.y));

                minExtents = glm::vec3(-maxHorizontal, -maxVertical, minExtents.z);
                maxExtents = glm::vec3(maxHorizontal, maxVertical, maxExtents.z);
            }

            if (s_bRoundCascadeToPixelSize)
            {
                const glm::vec2 pixelSizeMin{minExtents.x / SHADOW_MAP_CASCADE_SIZE, minExtents.y / SHADOW_MAP_CASCADE_SIZE};
                const glm::vec2 pixelSizeMax{maxExtents.x / SHADOW_MAP_CASCADE_SIZE, maxExtents.y / SHADOW_MAP_CASCADE_SIZE};
                minExtents.x = glm::round(minExtents.x / pixelSizeMin.x) * pixelSizeMin.x;
                minExtents.y = glm::round(minExtents.y / pixelSizeMin.y) * pixelSizeMin.y;

                maxExtents.x = glm::round(maxExtents.x / pixelSizeMax.x) * pixelSizeMax.x;
                maxExtents.y = glm::round(maxExtents.y / pixelSizeMax.y) * pixelSizeMax.y;
            }

            auto lightOrthoProjectionMatrix =
                glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z, minExtents.z) *
                glm::scale(glm::vec3(1.0f, -1.0f, 1.0f));

#if 0
            // Offset to texel space to avoid shimmering ->(https://stackoverflow.com/questions/33499053/cascaded-shadow-map-shimmering)
            auto shadowMatrix       = lightOrthoProjectionMatrix * lightViewMatrix;
            glm::vec4 shadowOrigin  = (shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)) * (f32)SHADOW_MAP_CASCADE_SIZE / 2.0f;
            glm::vec4 roundedOrigin = glm::round(shadowOrigin);
            glm::vec4 roundOffset   = roundedOrigin - shadowOrigin;
            roundOffset             = roundOffset * 2.0f / (f32)SHADOW_MAP_CASCADE_SIZE;
            roundOffset.z           = 0.0f;
            roundOffset.w           = 0.0f;
            lightOrthoProjectionMatrix[0] += roundOffset;
            lightOrthoProjectionMatrix[1] += roundOffset;
            lightOrthoProjectionMatrix[2] += roundOffset;
            lightOrthoProjectionMatrix[3] += roundOffset;
#endif

            return lightOrthoProjectionMatrix * lightViewMatrix;
        };

        for (u32 cascadeIndex{}; cascadeIndex < SHADOW_MAP_CASCADE_COUNT - 1; ++cascadeIndex)
        {
            if (cascadeIndex == 0)
            {
                csmData.ViewProjectionMatrix[cascadeIndex] =
                    GetLightSpaceViewProjectionMatrixFunc(zNear, glm::abs(csmData.CascadeSplits[cascadeIndex]));
            }
            else if (cascadeIndex + 1 >= SHADOW_MAP_CASCADE_COUNT - 1)
            {
                csmData.ViewProjectionMatrix[cascadeIndex] =
                    GetLightSpaceViewProjectionMatrixFunc(glm::abs(csmData.CascadeSplits[cascadeIndex]), zFar);
            }
            else
            {
                csmData.ViewProjectionMatrix[cascadeIndex] = GetLightSpaceViewProjectionMatrixFunc(
                    glm::abs(csmData.CascadeSplits[cascadeIndex]), glm::abs(csmData.CascadeSplits[cascadeIndex + 1]));
            }
        }
#endif

        return csmData;
    }

    CombinedRenderer::CombinedRenderer() noexcept
    {
        m_MainCamera = MakeShared<Camera>(70.0f, static_cast<f32>(m_ViewportExtent.width) / static_cast<f32>(m_ViewportExtent.height),
                                          1000.0f, 0.0001f);
        m_Scene      = MakeUnique<Scene>("CombinedRendererTest");

#if 0
        {
            // prepare shaders for transforming equirectangular to cubemap
            Unique<GfxPipeline> equirectangularToCubemapPipeline{nullptr};
            {
                auto equirectangularToCubemapShader =
                    MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                          GfxShaderDescription{.Path = "../Assets/Shaders/ibl_utils/equirectangular_to_cubemap.slang"});
                const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eR32G32B32A32Sfloat},
                                               //              .CullMode{vk::CullModeFlagBits::eBack},
                                                             .FrontFace{vk::FrontFace::eCounterClockwise},
                                                             .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                             .PolygonMode{vk::PolygonMode::eFill}};
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName = "equirectangular_to_cubemap", .PipelineOptions = gpo, .Shader = equirectangularToCubemapShader};
                equirectangularToCubemapPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }
            // prepare shaders for generating irradiance cube map
            Unique<GfxPipeline> irradianceCubemapPipeline{nullptr};
            {
                auto irradianceCubemapShader = MakeShared<GfxShader>(
                    m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/ibl_utils/generate_irradiance_cube.slang"});
                const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eR32G32B32A32Sfloat},
                                                          //   .CullMode{vk::CullModeFlagBits::eBack},
                                                             .FrontFace{vk::FrontFace::eCounterClockwise},
                                                             .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                             .PolygonMode{vk::PolygonMode::eFill}};
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName = "generate_irradiance_cube", .PipelineOptions = gpo, .Shader = irradianceCubemapShader};
                irradianceCubemapPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }

            // final convoluted env map
            m_EnvMapTexture = MakeUnique<GfxTexture>(
                m_GfxContext->GetDevice(),
                GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(s_IrradianceCubeMapSize, s_IrradianceCubeMapSize, 1),
                                      vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eColorAttachment,
                                      vk::SamplerCreateInfo()
                                          .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                          .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                          .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
                                          .setMagFilter(vk::Filter::eLinear)
                                          .setMinFilter(vk::Filter::eLinear),
                                      6));

            auto executionContext = m_GfxContext->CreateImmediateExecuteContext(ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL);
            executionContext.CommandBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
            executionContext.CommandBuffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics, m_GfxContext->GetDevice()->GetBindlessPipelineLayout(), 0,
                m_GfxContext->GetDevice()->GetCurrentFrameBindlessResources().DescriptorSet, {});

            // load equirectangular hdr texture
            Unique<GfxTexture> equirectangularEnvMap{nullptr};
            {
                i32 width{1}, height{1}, channels{4};
                void* hdrImageData = GfxTextureUtils::LoadImage("../Assets/env_maps/victoria_sunset_2k.hdr", width, height, channels);

                equirectangularEnvMap = MakeUnique<GfxTexture>(
                    m_GfxContext->GetDevice(), GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(width, height, 1),
                                                                     vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eTransferDst,
                                                                     vk::SamplerCreateInfo()
                                                                         .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                                                         .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                                                         .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
                                                                         .setMagFilter(vk::Filter::eLinear)
                                                                         .setMinFilter(vk::Filter::eLinear)));

                const auto imageSize = static_cast<u64>(width * height * channels * sizeof(f32));
                auto stagingBuffer   = MakeUnique<GfxBuffer>(
                    m_GfxContext->GetDevice(), GfxBufferDescription(imageSize, /* placeholder */ 1, vk::BufferUsageFlagBits::eTransferSrc,
                                                                      EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT));
                stagingBuffer->SetData(hdrImageData, imageSize);
                GfxTextureUtils::UnloadImage(hdrImageData);

                executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                    vk::ImageMemoryBarrier2()
                        .setImage(*equirectangularEnvMap)
                        .setSubresourceRange(vk::ImageSubresourceRange()
                                                 .setBaseArrayLayer(0)
                                                 .setBaseMipLevel(0)
                                                 .setLevelCount(1)
                                                 .setLayerCount(1)
                                                 .setAspectMask(vk::ImageAspectFlagBits::eColor))
                        .setOldLayout(vk::ImageLayout::eUndefined)
                        .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                        .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                        .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                        .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
                        .setDstStageMask(vk::PipelineStageFlagBits2::eCopy)));

                executionContext.CommandBuffer.copyBufferToImage(
                    *stagingBuffer, *equirectangularEnvMap, vk::ImageLayout::eTransferDstOptimal,
                    vk::BufferImageCopy()
                        .setImageSubresource(vk::ImageSubresourceLayers()
                                                 .setLayerCount(1)
                                                 .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                 .setBaseArrayLayer(0)
                                                 .setMipLevel(0))
                        .setImageExtent(vk::Extent3D(width, height, 1)));

                executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                    vk::ImageMemoryBarrier2()
                        .setImage(*equirectangularEnvMap)
                        .setSubresourceRange(vk::ImageSubresourceRange()
                                                 .setBaseArrayLayer(0)
                                                 .setBaseMipLevel(0)
                                                 .setLevelCount(1)
                                                 .setLayerCount(1)
                                                 .setAspectMask(vk::ImageAspectFlagBits::eColor))
                        .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                        .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
                        .setSrcStageMask(vk::PipelineStageFlagBits2::eCopy)
                        .setDstAccessMask(vk::AccessFlagBits2::eShaderSampledRead)
                        .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader)));
            }

            // Indices for the cube (total 36 indices)
            constexpr std::array<u8, 36> cubeIndices = {
                0,  1,  2,  0,  2,  3,   // Back face
                4,  5,  6,  4,  6,  7,   // Front face
                8,  9,  10, 8,  10, 11,  // Left face
                12, 13, 14, 12, 14, 15,  // Right face
                16, 17, 18, 16, 18, 19,  // Top face
                20, 21, 22, 20, 22, 23   // Bottom face
            };

            auto indexBufferReBAR = MakeUnique<GfxBuffer>(m_GfxContext->GetDevice(),
                                                          GfxBufferDescription(sizeof(u8) * cubeIndices.size(), sizeof(u8),
                                                                               vk::BufferUsageFlagBits::eIndexBuffer,
                                                                               EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT));
            indexBufferReBAR->SetData(cubeIndices.data(), sizeof(u8) * cubeIndices.size());

            struct EquirectangularToCubemapShaderData
            {
                glm::mat4 CaptureViewMatrices[6];
                glm::mat4 ProjectionMatrix;
            } etcsd                = {};
            etcsd.ProjectionMatrix = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f) * glm::scale(glm::vec3(1.0f, -1.0f, 1.0f));

            /*          etcsd.CaptureViewMatrices[0] = glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f,
                         0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)); etcsd.CaptureViewMatrices[1] =
                         glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f),
                         glm::vec3(1.0f, 0.0f, 0.0f)); etcsd.CaptureViewMatrices[2] = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f),
                         glm::vec3(1.0f, 0.0f, 0.0f)); etcsd.CaptureViewMatrices[3] = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f),
                         glm::vec3(1.0f, 0.0f, 0.0f)); etcsd.CaptureViewMatrices[4] = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f),
                         glm::vec3(1.0f, 0.0f, 0.0f)); etcsd.CaptureViewMatrices[5] = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f),
                         glm::vec3(0.0f, 0.0f, 1.0f));*/
            etcsd.CaptureViewMatrices[0] =
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
            etcsd.CaptureViewMatrices[1] =
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
            etcsd.CaptureViewMatrices[2] =
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
            etcsd.CaptureViewMatrices[3] =
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
            etcsd.CaptureViewMatrices[4] =
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
            etcsd.CaptureViewMatrices[5] =
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f));

            auto etcsDataBuffer = MakeUnique<GfxBuffer>(m_GfxContext->GetDevice(),
                                                        GfxBufferDescription(sizeof(EquirectangularToCubemapShaderData),
                                                                             sizeof(EquirectangularToCubemapShaderData),
                                                                             vk::BufferUsageFlagBits::eUniformBuffer,
                                                                             EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT |
                                                                                 EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_ADDRESSABLE_BIT));
            etcsDataBuffer->SetData(&etcsd, sizeof(etcsd));

            auto envCubeMap = MakeUnique<GfxTexture>(
                m_GfxContext->GetDevice(), GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(512, 512, 1),
                                                                 vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eColorAttachment,
                                                                 vk::SamplerCreateInfo()
                                                                     .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                                                     .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                                                     .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
                                                                     .setMagFilter(vk::Filter::eLinear)
                                                                     .setMinFilter(vk::Filter::eLinear),
                                                                 6));

            struct PushConstantBlock
            {
                const EquirectangularToCubemapShaderData* ETCSData;
                u32 SrcTextureID;
            } pc = {};
            // transform equirectangular map to cubemap
            {
                executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                    vk::ImageMemoryBarrier2()
                        .setImage(*envCubeMap)
                        .setSubresourceRange(vk::ImageSubresourceRange()
                                                 .setBaseArrayLayer(0)
                                                 .setBaseMipLevel(0)
                                                 .setLevelCount(1)
                                                 .setLayerCount(6)
                                                 .setAspectMask(vk::ImageAspectFlagBits::eColor))
                        .setOldLayout(vk::ImageLayout::eUndefined)
                        .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                        .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                        .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eColorAttachmentRead)
                        .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)));

                executionContext.CommandBuffer.beginRendering(
                    vk::RenderingInfo()
                        .setLayerCount(6)
                        .setColorAttachments((vk::RenderingAttachmentInfo&)envCubeMap->GetRenderingAttachmentInfo(
                            vk::ImageLayout::eColorAttachmentOptimal,
                            vk::ClearValue().setColor(vk::ClearColorValue().setFloat32({0.0f, 0.0f, 0.0f, 1.0f})),
                            vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore))
                        .setRenderArea(vk::Rect2D().setExtent(vk::Extent2D().setWidth(512).setHeight(512))));

                executionContext.CommandBuffer.setViewportWithCount(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(512).setHeight(512));
                executionContext.CommandBuffer.setScissorWithCount(vk::Rect2D().setExtent(vk::Extent2D().setWidth(512).setHeight(512)));
                executionContext.CommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *equirectangularToCubemapPipeline);

                pc              = {};
                pc.SrcTextureID = equirectangularEnvMap->GetBindlessTextureID();
                pc.ETCSData     = (const EquirectangularToCubemapShaderData*)etcsDataBuffer->GetBDA();

                executionContext.CommandBuffer.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                                                vk::ShaderStageFlagBits::eAll, 0, pc);
                executionContext.CommandBuffer.bindIndexBuffer(*indexBufferReBAR, 0, vk::IndexType::eUint8EXT);
                executionContext.CommandBuffer.drawIndexed(cubeIndices.size(), 6, 0, 0, 0);

                executionContext.CommandBuffer.endRendering();
            }

            // convolute env cubemap
            {
                executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                    vk::ImageMemoryBarrier2()
                        .setImage(*envCubeMap)
                        .setSubresourceRange(vk::ImageSubresourceRange()
                                                 .setBaseArrayLayer(0)
                                                 .setBaseMipLevel(0)
                                                 .setLevelCount(1)
                                                 .setLayerCount(6)
                                                 .setAspectMask(vk::ImageAspectFlagBits::eColor))
                        .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                        .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eColorAttachmentRead)
                        .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                        .setDstAccessMask(vk::AccessFlagBits2::eShaderSampledRead)
                        .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader)));

                executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                    vk::ImageMemoryBarrier2()
                        .setImage(*m_EnvMapTexture)
                        .setSubresourceRange(vk::ImageSubresourceRange()
                                                 .setBaseArrayLayer(0)
                                                 .setBaseMipLevel(0)
                                                 .setLevelCount(1)
                                                 .setLayerCount(6)
                                                 .setAspectMask(vk::ImageAspectFlagBits::eColor))
                        .setOldLayout(vk::ImageLayout::eUndefined)
                        .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                        .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                        .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eColorAttachmentRead)
                        .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)));

                executionContext.CommandBuffer.beginRendering(
                    vk::RenderingInfo()
                        .setLayerCount(6)
                        .setColorAttachments((vk::RenderingAttachmentInfo&)m_EnvMapTexture->GetRenderingAttachmentInfo(
                            vk::ImageLayout::eColorAttachmentOptimal,
                            vk::ClearValue().setColor(vk::ClearColorValue().setFloat32({0.0f, 0.0f, 0.0f, 1.0f})),
                            vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore))
                        .setRenderArea(
                            vk::Rect2D().setExtent(vk::Extent2D().setWidth(s_IrradianceCubeMapSize).setHeight(s_IrradianceCubeMapSize))));

                executionContext.CommandBuffer.setViewportWithCount(vk::Viewport()
                                                                        .setMinDepth(0.0f)
                                                                        .setMaxDepth(1.0f)
                                                                        .setWidth(s_IrradianceCubeMapSize)
                                                                        .setHeight(s_IrradianceCubeMapSize));
                executionContext.CommandBuffer.setScissorWithCount(
                    vk::Rect2D().setExtent(vk::Extent2D().setWidth(s_IrradianceCubeMapSize).setHeight(s_IrradianceCubeMapSize)));

                executionContext.CommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *irradianceCubemapPipeline);

                pc              = {};
                pc.SrcTextureID = envCubeMap->GetBindlessTextureID();
                pc.ETCSData     = (const EquirectangularToCubemapShaderData*)etcsDataBuffer->GetBDA();
                executionContext.CommandBuffer.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                                                vk::ShaderStageFlagBits::eAll, 0, pc);
                executionContext.CommandBuffer.bindIndexBuffer(*indexBufferReBAR, 0, vk::IndexType::eUint8EXT);
                executionContext.CommandBuffer.drawIndexed(cubeIndices.size(), 6, 0, 0, 0);

                executionContext.CommandBuffer.endRendering();

                executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                    vk::ImageMemoryBarrier2()
                        .setImage(*m_EnvMapTexture)
                        .setSubresourceRange(vk::ImageSubresourceRange()
                                                 .setBaseArrayLayer(0)
                                                 .setBaseMipLevel(0)
                                                 .setLevelCount(1)
                                                 .setLayerCount(6)
                                                 .setAspectMask(vk::ImageAspectFlagBits::eColor))
                        .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                        .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eColorAttachmentRead)
                        .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                        .setDstAccessMask(vk::AccessFlagBits2::eShaderSampledRead)
                        .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader)));
            }

            executionContext.CommandBuffer.end();
            m_GfxContext->SubmitImmediateExecuteContext(executionContext);
        }
#endif

        LOG_INFO("Light clusters subdivision Z slices: {}", LIGHT_CLUSTERS_SUBDIVISION_Z);
        for (u32 slice{}; slice < LIGHT_CLUSTERS_SUBDIVISION_Z; ++slice)
        {
            const auto sd         = m_MainCamera->GetShaderData();
            const auto ZSliceNear = sd.zNearFar.x * glm::pow(sd.zNearFar.y / sd.zNearFar.x, (f32)slice / (f32)LIGHT_CLUSTERS_SUBDIVISION_Z);
            const auto ZSliceFar =
                sd.zNearFar.x * glm::pow(sd.zNearFar.y / sd.zNearFar.x, (f32)(slice + 1) / (f32)LIGHT_CLUSTERS_SUBDIVISION_Z);

            LOG_TRACE("Slice: {}, Froxel dimensions: [{:.4f}, {:.4f}].", slice, ZSliceNear, ZSliceFar);
        }

        std::vector<std::future<void>> pipelinesToCreate{};
        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName       = "LightClustersBuild",
                    .PipelineOptions = GfxComputePipelineOptions{},
                    .Shader          = MakeShared<GfxShader>(
                        m_GfxContext->GetDevice(),
                        GfxShaderDescription{.Path = "../Assets/Shaders/clustered_shading/light_clusters_build.slang"})};
                m_LightClustersBuildPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName       = "LightClustersDetectActive",
                    .PipelineOptions = GfxComputePipelineOptions{},
                    .Shader          = MakeShared<GfxShader>(
                        m_GfxContext->GetDevice(),
                        GfxShaderDescription{.Path = "../Assets/Shaders/clustered_shading/light_clusters_detect_active.slang"})};
                m_LightClustersDetectActivePipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName       = "LightClustersAssignment",
                    .PipelineOptions = GfxComputePipelineOptions{},
                    .Shader          = MakeShared<GfxShader>(
                        m_GfxContext->GetDevice(),
                        GfxShaderDescription{.Path = "../Assets/Shaders/clustered_shading/light_clusters_assignment.slang"})};
                m_LightClustersAssignmentPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                auto depthPrePassShader              = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                                GfxShaderDescription{.Path = "../Assets/Shaders/depth_pre_pass.slang"});
                const GfxGraphicsPipelineOptions gpo = {
                    .RenderingFormats{vk::Format::eD32Sfloat},
                    .DynamicStates{vk::DynamicState::eCullMode, vk::DynamicState::ePrimitiveTopology,
                                   vk::DynamicState::eDepthClampEnableEXT},
                    .CullMode{vk::CullModeFlagBits::eBack},
                    .FrontFace{vk::FrontFace::eCounterClockwise},
                    .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                    .PolygonMode{vk::PolygonMode::eFill},
                    .bDepthClamp{true},
                    .bDepthTest{true},
                    .bDepthWrite{true},
                    .DepthCompareOp{vk::CompareOp::eGreaterOrEqual},
                };
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName = "depth_pre_pass", .PipelineOptions = gpo, .Shader = depthPrePassShader};
                m_DepthPrePassPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                // NOTE: To not create many pipelines for objects, I switch depth compare op based on AlphaMode of object.
                auto pbrShader                       = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                       GfxShaderDescription{.Path = "../Assets/Shaders/main_pass_bc_compressed.slang"});
                const GfxGraphicsPipelineOptions gpo = {
                    .RenderingFormats{vk::Format::eR16G16B16A16Sfloat, vk::Format::eD32Sfloat},
                    .DynamicStates{vk::DynamicState::eCullMode, vk::DynamicState::ePrimitiveTopology, vk::DynamicState::eDepthCompareOp},
                    .CullMode{vk::CullModeFlagBits::eBack},
                    .FrontFace{vk::FrontFace::eCounterClockwise},
                    .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                    .PolygonMode{vk::PolygonMode::eFill},
                    .bDepthTest{true},
                    .bDepthWrite{false},
                    .DepthCompareOp{vk::CompareOp::eEqual},
                    .BlendModes{GfxGraphicsPipelineOptions::EBlendMode::BLEND_MODE_ALPHA}};
                const GfxPipelineDescription pipelineDesc = {.DebugName = "MainPassPBR", .PipelineOptions = gpo, .Shader = pbrShader};
                m_MainLightingPassPipeline                = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                auto finalPassShader =
                    MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/final.slang"});
                const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eA2B10G10R10UnormPack32},
                                                             .CullMode{vk::CullModeFlagBits::eNone},
                                                             .FrontFace{vk::FrontFace::eCounterClockwise},
                                                             .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                             .PolygonMode{vk::PolygonMode::eFill}};
                const GfxPipelineDescription pipelineDesc = {.DebugName = "FinalPass", .PipelineOptions = gpo, .Shader = finalPassShader};
                m_FinalPassPipeline                       = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName       = "SSS",
                    .PipelineOptions = GfxComputePipelineOptions{},
                    .Shader =
                        MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/sss/sss.slang"})};
                m_SSSPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                auto ssaoShader =
                    MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/ssao/ssao.slang"});

                const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eR8Unorm},
                                                             .CullMode{vk::CullModeFlagBits::eNone},
                                                             .FrontFace{vk::FrontFace::eCounterClockwise},
                                                             .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                             .PolygonMode{vk::PolygonMode::eFill}};
                const GfxPipelineDescription pipelineDesc = {.DebugName = "SSAO_Graphics", .PipelineOptions = gpo, .Shader = ssaoShader};
                m_SSAOPipelineGraphics                    = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                auto ssaoShader =
                    MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/ssao/ssao_cs.slang"});

                const GfxPipelineDescription pipelineDesc = {
                    .DebugName = "SSAO_Compute", .PipelineOptions = GfxComputePipelineOptions{}, .Shader = ssaoShader};
                m_SSAOPipelineCompute = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                auto ssaoBoxBlurShader = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                               GfxShaderDescription{.Path = "../Assets/Shaders/ssao/ssao_box_blur.slang"});

                const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eR8Unorm},
                                                             .CullMode{vk::CullModeFlagBits::eNone},
                                                             .FrontFace{vk::FrontFace::eCounterClockwise},
                                                             .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                             .PolygonMode{vk::PolygonMode::eFill}};
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName = "SSAOBoxBlur_Graphics", .PipelineOptions = gpo, .Shader = ssaoBoxBlurShader};
                m_SSAOBoxBlurPipelineGraphics = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                auto ssaoBoxBlurShader = MakeShared<GfxShader>(
                    m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/ssao/ssao_box_blur_cs.slang"});

                const GfxPipelineDescription pipelineDesc = {
                    .DebugName = "SSAOBoxBlur_Compute", .PipelineOptions = GfxComputePipelineOptions{}, .Shader = ssaoBoxBlurShader};
                m_SSAOBoxBlurPipelineCompute = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        // Default bloom
        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                auto bloomDownsampleShader = MakeShared<GfxShader>(
                    m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/bloom/pbr_bloom_downsample.slang"});
                const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eB10G11R11UfloatPack32},
                                                             .CullMode{vk::CullModeFlagBits::eNone},
                                                             .FrontFace{vk::FrontFace::eCounterClockwise},
                                                             .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                             .PolygonMode{vk::PolygonMode::eFill}};
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName = "BloomDownsampleGraphics", .PipelineOptions = gpo, .Shader = bloomDownsampleShader};
                m_BloomDownsamplePipelineGraphics = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                auto bloomUpsampleBlurShader = MakeShared<GfxShader>(
                    m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/bloom/pbr_bloom_upsample_blur.slang"});
                const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eB10G11R11UfloatPack32},
                                                             .CullMode{vk::CullModeFlagBits::eNone},
                                                             .FrontFace{vk::FrontFace::eCounterClockwise},
                                                             .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                             .PolygonMode{vk::PolygonMode::eFill},
                                                             .BlendModes{GfxGraphicsPipelineOptions::EBlendMode::BLEND_MODE_ADDITIVE}};
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName = "BloomUpsampleBlurGraphics", .PipelineOptions = gpo, .Shader = bloomUpsampleBlurShader};
                m_BloomUpsampleBlurPipelineGraphics = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        // Compute optimized bloom
        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName       = "BloomDownsampleCompute",
                    .PipelineOptions = GfxComputePipelineOptions{},
                    .Shader          = MakeShared<GfxShader>(
                        m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/bloom/bloom_downsample_compute.slang"})};
                m_BloomDownsamplePipelineCompute = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName       = "BloomUpsampleBlurCompute",
                    .PipelineOptions = GfxComputePipelineOptions{},
                    .Shader =
                        MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                              GfxShaderDescription{.Path = "../Assets/Shaders/bloom/bloom_upsample_blur_compute.slang"})};
                m_BloomUpsampleBlurPipelineCompute = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName       = "SetupShadows",
                    .PipelineOptions = GfxComputePipelineOptions{},
                    .Shader          = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                    GfxShaderDescription{.Path = "../Assets/Shaders/csm/setup_csm.slang"})};
                m_ShadowsSetupPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName       = "DepthBoundsCompute",
                    .PipelineOptions = GfxComputePipelineOptions{},
                    .Shader          = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                    GfxShaderDescription{.Path = "../Assets/Shaders/csm/depth_reduction.slang"})};
                m_DepthBoundsComputePipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        const auto pipelineCreateBegin = Timer::Now();
        for (auto& pipeline : pipelinesToCreate)
        {
            pipeline.get();
        }
        LOG_INFO("Time taken to create {} pipelines: {} seconds.", pipelinesToCreate.size(),
                 Timer::GetElapsedSecondsFromNow(pipelineCreateBegin));

        m_LightData->Sun.bCastShadows = true;
        m_LightData->Sun.Direction    = {-0.5f, 0.8f, 0.08f};
        m_LightData->Sun.Intensity    = 1.0f;
        m_LightData->Sun.Color        = Shaders::PackUnorm4x8(glm::vec4(s_SunColor, 1.0f));
        m_LightData->PointLightCount  = MAX_POINT_LIGHT_COUNT;
        constexpr f32 radius          = 2.5f;
        constexpr f32 intensity       = 1.2f;
        for (auto& pl : m_LightData->PointLights)
        {
            pl.sphere.Origin = glm::linearRand(s_MinPointLightPos, s_MaxPointLightPos);
            pl.sphere.Radius = glm::linearRand(0.1f, radius);
            pl.Intensity     = glm::linearRand(0.8f, intensity);
            pl.Color         = Shaders::PackUnorm4x8(glm::vec4(glm::linearRand(glm::vec3(0.001f), glm::vec3(1.0f)), 1.0f));
        }

        m_Scene->LoadMesh(m_GfxContext, "../Assets/Models/sponza/scene.gltf");
        m_Scene->IterateObjects(m_DrawContext);
    }

    void CombinedRenderer::RenderFrame() noexcept
    {
        auto& mainWindow = Application::Get().GetMainWindow();

        s_DrawCallCount = 0;

        static bool bHotReloadQueued{false};
        if (bHotReloadQueued && mainWindow->IsKeyReleased(GLFW_KEY_V))  // Check state frame before and current
        {
            m_DebugRenderer->HotReload();

            // m_DepthPrePassPipeline->HotReload();
            m_MainLightingPassPipeline->HotReload();
            m_FinalPassPipeline->HotReload();

            m_LightClustersBuildPipeline->HotReload();
            m_LightClustersDetectActivePipeline->HotReload();
            m_LightClustersAssignmentPipeline->HotReload();
            // m_SSSPipeline->HotReload();

            m_SSAOPipelineGraphics->HotReload();
            m_SSAOPipelineCompute->HotReload();
            m_SSAOBoxBlurPipelineGraphics->HotReload();
            m_SSAOBoxBlurPipelineCompute->HotReload();

            m_BloomDownsamplePipelineGraphics->HotReload();
            m_BloomUpsampleBlurPipelineGraphics->HotReload();

            m_BloomDownsamplePipelineCompute->HotReload();
            m_BloomUpsampleBlurPipelineCompute->HotReload();
        }
        bHotReloadQueued = mainWindow->IsKeyPressed(GLFW_KEY_V);

        // Sort transparent objects back to front.
        std::sort(std::execution::par, m_DrawContext.RenderObjects.begin(), m_DrawContext.RenderObjects.end(),
                  [&](const RenderObject& lhs, const RenderObject& rhs)
                  {
                      if (lhs.AlphaMode == rhs.AlphaMode && lhs.AlphaMode != EAlphaMode::ALPHA_MODE_OPAQUE)
                      {
                          const f32 lhsDistToCam = glm::length(m_MainCamera->GetShaderData().Position - glm::vec3(lhs.TRS[3]));
                          const f32 rhsDistToCam = glm::length(m_MainCamera->GetShaderData().Position - glm::vec3(rhs.TRS[3]));
                          return lhsDistToCam > rhsDistToCam;
                      }

                      if (lhs.AlphaMode == rhs.AlphaMode) return lhs.IndexBuffer < rhs.IndexBuffer;

                      return lhs.AlphaMode < rhs.AlphaMode;
                  });

        struct FramePreparePassData
        {
            RGResourceID CameraBuffer;
            RGResourceID LightBuffer;
        } fpPassData = {};
        m_RenderGraph->AddPass(
            "FramePreparePass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_TRANSFER,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateBuffer(ResourceNames::CameraBuffer,
                                       GfxBufferDescription(sizeof(Shaders::CameraData), sizeof(Shaders::CameraData),
                                                            vk::BufferUsageFlagBits::eUniformBuffer,
                                                            EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT));
                fpPassData.CameraBuffer =
                    scheduler.WriteBuffer(ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT);

                scheduler.CreateBuffer(ResourceNames::LightBuffer,
                                       GfxBufferDescription(sizeof(Shaders::LightData), sizeof(Shaders::LightData),
                                                            vk::BufferUsageFlagBits::eUniformBuffer,
                                                            EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT));
                fpPassData.LightBuffer =
                    scheduler.WriteBuffer(ResourceNames::LightBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT);
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& cameraUBO = scheduler.GetBuffer(fpPassData.CameraBuffer);
                cameraUBO->SetData(&m_MainCamera->GetShaderData(), sizeof(Shaders::CameraData));

                if (s_bUpdateLights)
                {
                    for (auto& pl : m_LightData->PointLights)
                    {
                        pl.sphere.Origin += glm::vec3(0, 3.0f, 0) * Application::Get().GetDeltaTime();
                        if (pl.sphere.Origin.y > s_MaxPointLightPos.y) pl.sphere.Origin.y -= (s_MaxPointLightPos.y - s_MinPointLightPos.y);
                    }
                }

                auto& lightUBO = scheduler.GetBuffer(fpPassData.LightBuffer);
                lightUBO->SetData(m_LightData.get(), sizeof(Shaders::LightData));
            });

#if 0
        struct ShadowsDepthReductionPassData
        {
            RGResourceID DepthTexture;
            RGResourceID DepthBoundsBuffer;
        } sdrPassData = {};
        m_RenderGraph->AddPass(
            "ShadowsDepthReductionPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_COMPUTE,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateBuffer(ResourceNames::ShadowsDepthBoundsBuffer,
                                       GfxBufferDescription(sizeof(Shaders::DepthBounds), sizeof(Shaders::DepthBounds),
                                                            vk::BufferUsageFlagBits::eStorageBuffer,
                                                            EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT));
                sdrPassData.DepthBoundsBuffer = scheduler.WriteBuffer(ResourceNames::ShadowsDepthBoundsBuffer,
                                                                      EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT |
                                                                          EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);

                sdrPassData.DepthTexture = scheduler.ReadTexture(ResourceNames::GBufferDepth, MipSet::FirstMip(),
                                                                 EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_DepthBoundsComputePipeline.get());

                struct PushConstantBlock
                {
                    u32 DepthTextureID;
                    glm::vec2 SrcTexelSize;
                    glm::vec2 DepthUnpackConsts;
                    Shaders::DepthBounds* DepthBounds;
                } pc = {};

                auto& depthTexture = scheduler.GetTexture(sdrPassData.DepthTexture);
                pc.DepthTextureID  = depthTexture->GetBindlessTextureID();

                const auto& dimensions = depthTexture->GetDescription().Dimensions;
                pc.SrcTexelSize        = 1.0f / glm::vec2(dimensions.x, dimensions.y);
                pc.DepthUnpackConsts   = m_MainCamera->GetShaderData().DepthUnpackConsts;
                pc.DepthBounds         = (Shaders::DepthBounds*)scheduler.GetBuffer(sdrPassData.DepthBoundsBuffer)->GetBDA();

                cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
                                                     0, pc);
                cmd.dispatch(glm::ceil(dimensions.x / (f32)DEPTH_REDUCTION_WG_SIZE_X),
                             glm::ceil(dimensions.y / (f32)DEPTH_REDUCTION_WG_SIZE_Y), 1);
            });
#endif

        // reversed z
        const auto csmShaderData = UpdateCSM(glm::normalize(m_LightData->Sun.Direction), m_MainCamera->GetZFar(), m_MainCamera->GetZNear(),
                                             m_MainCamera->GetZoom(), m_MainCamera->GetAspectRatio(), m_MainCamera->GetViewMatrix());

        struct CascadedShadowMapsPassData
        {
            RGResourceID CSMDataBuffer;
        };
        std::vector<CascadedShadowMapsPassData> csmPassDatas(SHADOW_MAP_CASCADE_COUNT);
        for (u32 cascadeIndex{}; cascadeIndex < csmPassDatas.size(); ++cascadeIndex)
        {
            const auto passName    = "CSM" + std::to_string(cascadeIndex);
            const auto textureName = "CSMTexture" + std::to_string(cascadeIndex);
            m_RenderGraph->AddPass(
                passName, ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
                [&](RenderGraphResourceScheduler& scheduler)
                {
                    if (cascadeIndex == 0)
                    {
                        scheduler.CreateBuffer(ResourceNames::CSMDataBuffer,
                                               GfxBufferDescription(sizeof(Shaders::CascadedShadowMapsData),
                                                                    sizeof(Shaders::CascadedShadowMapsData),
                                                                    vk::BufferUsageFlagBits::eUniformBuffer,
                                                                    EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT));
                        csmPassDatas[cascadeIndex].CSMDataBuffer =
                            scheduler.WriteBuffer(ResourceNames::CSMDataBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT);
                    }
                    else
                    {
                        csmPassDatas[cascadeIndex].CSMDataBuffer = scheduler.ReadBuffer(
                            ResourceNames::CSMDataBuffer, EResourceStateBits::RESOURCE_STATE_VERTEX_SHADER_RESOURCE_BIT |
                                                              EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT);
                    }

                    // NOTE: Here I can create 1 texture with multiple mips or multiple textures,
                    // I went with multiple textures, don't want to bother with mips cuz memory aliaser will make things.
                    scheduler.CreateTexture(textureName,
                                            GfxTextureDescription(vk::ImageType::e2D,
                                                                  glm::uvec3(SHADOW_MAP_CASCADE_SIZE, SHADOW_MAP_CASCADE_SIZE, 1),
                                                                  vk::Format::eD32Sfloat, vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                                                  vk::SamplerCreateInfo()
                                                                      .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
                                                                      .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
                                                                      .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
                                                                      .setMagFilter(vk::Filter::eNearest)
                                                                      .setMinFilter(vk::Filter::eNearest)
                                                                      .setBorderColor(vk::BorderColor::eFloatOpaqueBlack)));
                    scheduler.WriteDepthStencil(textureName, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                                vk::AttachmentStoreOp::eStore, vk::ClearDepthStencilValue().setDepth(0.0f).setStencil(0));

                    scheduler.SetViewportScissors(
                        vk::Viewport()
                            .setMinDepth(0.0f)
                            .setMaxDepth(1.0f)
                            .setWidth(SHADOW_MAP_CASCADE_SIZE)
                            .setHeight(SHADOW_MAP_CASCADE_SIZE),
                        vk::Rect2D().setExtent(vk::Extent2D().setWidth(SHADOW_MAP_CASCADE_SIZE).setHeight(SHADOW_MAP_CASCADE_SIZE)));
                },
                [&, cascadeIndex](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
                {
                    if (!m_LightData->Sun.bCastShadows) return;

                    auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                    pipelineStateCache.Bind(cmd, m_DepthPrePassPipeline.get());
                    pipelineStateCache.SetDepthClamp(cmd, true);

                    auto& csmDataUBO = scheduler.GetBuffer(csmPassDatas[cascadeIndex].CSMDataBuffer);
                    if (cascadeIndex == 0)
                        csmDataUBO->SetData(&csmShaderData, sizeof(csmShaderData));  // NOTE: will be used further in main pass

                    for (const auto& ro : m_DrawContext.RenderObjects)
                    {
                        if (ro.AlphaMode != EAlphaMode::ALPHA_MODE_OPAQUE) continue;

                        struct PushConstantBlock
                        {
                            glm::vec3 scale{1.0f};
                            glm::vec3 translation{0.0f};
                            float4 orientation{1.0f};
                            glm::mat4 ViewProjectionMatrix{1.f};
                            const VertexPosition* VtxPositions{nullptr};
                        } pc = {};
                        glm::quat q{1.0f, 0.0f, 0.0f, 0.0f};
                        glm::vec3 decomposePlaceholder0{1.0f};
                        glm::vec4 decomposePlaceholder1{1.0f};
                        glm::decompose(ro.TRS, pc.scale, q, pc.translation, decomposePlaceholder0, decomposePlaceholder1);
                        pc.scale *= s_MeshScale;
                        pc.orientation = glm::vec4(q.w, q.x, q.y, q.z);

                        pc.VtxPositions         = (const VertexPosition*)ro.VertexPositionBuffer->GetBDA();
                        pc.ViewProjectionMatrix = csmShaderData.ViewProjectionMatrix[cascadeIndex];

                        pipelineStateCache.Set(cmd, ro.CullMode);
                        pipelineStateCache.Set(cmd, ro.PrimitiveTopology);

                        cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                             vk::ShaderStageFlagBits::eAll, 0, pc);
                        pipelineStateCache.Bind(cmd, ro.IndexBuffer.get(), 0, ro.IndexType);
                        cmd.drawIndexed(ro.IndexCount, 1, ro.FirstIndex, 0, 0);
                    }
                });
        }

        struct DepthPrePassData
        {
            RGResourceID CameraBuffer;
        } depthPrePassData = {};
        m_RenderGraph->AddPass(
            "DepthPrePass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateTexture(ResourceNames::GBufferDepth,
                                        GfxTextureDescription(vk::ImageType::e2D,
                                                              glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                              vk::Format::eD32Sfloat, vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                                              vk::SamplerCreateInfo()
                                                                  .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
                                                                  .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
                                                                  .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
                                                                  .setMagFilter(vk::Filter::eNearest)
                                                                  .setMinFilter(vk::Filter::eNearest)
                                                                  .setBorderColor(vk::BorderColor::eFloatOpaqueBlack)));
                scheduler.WriteDepthStencil(ResourceNames::GBufferDepth, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                            vk::AttachmentStoreOp::eStore, vk::ClearDepthStencilValue().setDepth(0.0f).setStencil(0));

                depthPrePassData.CameraBuffer =
                    scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_VERTEX_SHADER_RESOURCE_BIT);

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
                    vk::Rect2D().setExtent(m_ViewportExtent));
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_DepthPrePassPipeline.get());
                pipelineStateCache.SetDepthClamp(cmd, false);

                auto& cameraUBO = scheduler.GetBuffer(depthPrePassData.CameraBuffer);
                for (const auto& ro : m_DrawContext.RenderObjects)
                {
                    if (ro.AlphaMode != EAlphaMode::ALPHA_MODE_OPAQUE) continue;

                    struct PushConstantBlock
                    {
                        glm::vec3 scale{1.f};
                        glm::vec3 translation{0.f};
                        float4 orientation{1};
                        glm::mat4 ViewProjectionMatrix{1.f};
                        const VertexPosition* VtxPositions{nullptr};
                    } pc = {};

                    glm::quat q{1.0f, 0.0f, 0.0f, 0.0f};
                    glm::vec3 decomposePlaceholder0{1.0f};
                    glm::vec4 decomposePlaceholder1{1.0f};
                    glm::decompose(ro.TRS, pc.scale, q, pc.translation, decomposePlaceholder0, decomposePlaceholder1);
                    pc.scale *= s_MeshScale;
                    pc.orientation = glm::vec4(q.w, q.x, q.y, q.z);

                    pc.VtxPositions         = (const VertexPosition*)ro.VertexPositionBuffer->GetBDA();
                    pc.ViewProjectionMatrix = m_MainCamera->GetViewProjectionMatrix();

                    pipelineStateCache.Set(cmd, ro.CullMode);
                    pipelineStateCache.Set(cmd, ro.PrimitiveTopology);

                    cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                         vk::ShaderStageFlagBits::eAll, 0, pc);
                    pipelineStateCache.Bind(cmd, ro.IndexBuffer.get(), 0, ro.IndexType);
                    cmd.drawIndexed(ro.IndexCount, 1, ro.FirstIndex, 0, 0);
                }
            });

        struct LightClustersBuildPassData
        {
            RGResourceID CameraBuffer;
            RGResourceID LightClusterBuffer;
        } lcbPassData = {};
        m_RenderGraph->AddPass(
            "LightClustersBuildPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_COMPUTE,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                constexpr u64 lcbCapacity = sizeof(AABB) * LIGHT_CLUSTERS_COUNT;
                scheduler.CreateBuffer(ResourceNames::LightClusterBuffer,
                                       GfxBufferDescription(lcbCapacity, sizeof(AABB), vk::BufferUsageFlagBits::eStorageBuffer,
                                                            EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT));
                lcbPassData.LightClusterBuffer = scheduler.WriteBuffer(ResourceNames::LightClusterBuffer,
                                                                       EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT |
                                                                           EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);

                lcbPassData.CameraBuffer =
                    scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT |
                                                                          EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_LightClustersBuildPipeline.get());

                struct PushConstantBlock
                {
                    const Shaders::CameraData* CameraData;
                    AABB* Clusters;
                } pc = {};

                auto& cameraUBO      = scheduler.GetBuffer(lcbPassData.CameraBuffer);
                pc.CameraData        = (const Shaders::CameraData*)cameraUBO->GetBDA();
                auto& lightClusterSB = scheduler.GetBuffer(lcbPassData.LightClusterBuffer);
                pc.Clusters          = (AABB*)lightClusterSB->GetBDA();

                cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
                                                     0, pc);
                cmd.dispatch(glm::ceil(LIGHT_CLUSTERS_SUBDIVISION_X / (f32)LIGHT_CLUSTERS_BUILD_WG_SIZE),
                             glm::ceil(LIGHT_CLUSTERS_SUBDIVISION_Y / (f32)LIGHT_CLUSTERS_BUILD_WG_SIZE),
                             glm::ceil(LIGHT_CLUSTERS_SUBDIVISION_Z / (f32)LIGHT_CLUSTERS_BUILD_WG_SIZE));
            });

#if LIGHT_CLUSTERS_DETECT_ACTIVE
        struct LightClustersDetectActivePassData
        {
            RGResourceID DepthTexture;
            RGResourceID LightClusterBuffer;
            RGResourceID LightClusterDetectActiveBuffer;
        } lcdaPassData = {};
        m_RenderGraph->AddPass(
            "LightClustersDetectActive", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_COMPUTE,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateBuffer(ResourceNames::LightClusterDetectActiveBuffer,
                                       GfxBufferDescription(sizeof(Shaders::LightClusterActiveList),
                                                            sizeof(Shaders::LightClusterActiveList),
                                                            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                                                            EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT));
                lcdaPassData.LightClusterDetectActiveBuffer = scheduler.WriteBuffer(
                    ResourceNames::LightClusterDetectActiveBuffer,
                    EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT | EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);

                lcdaPassData.LightClusterBuffer = scheduler.ReadBuffer(ResourceNames::LightClusterBuffer,
                                                                       EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT |
                                                                           EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);

                lcdaPassData.DepthTexture = scheduler.ReadTexture(ResourceNames::GBufferDepth, MipSet::FirstMip(),
                                                                  EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);

                scheduler.ClearOnExecute(ResourceNames::LightClusterDetectActiveBuffer, 0, sizeof(Shaders::LightClusterActiveList));
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_LightClustersDetectActivePipeline.get());

                struct PushConstantBlock
                {
                    u32 DepthTextureID;
                    glm::vec2 SrcTexelSize;
                    glm::vec2 DepthUnpackConsts;
                    const AABB* Clusters;
                    Shaders::LightClusterActiveList* ActiveLightClusters;
                } pc = {};

                pc.DepthTextureID    = scheduler.GetTexture(lcdaPassData.DepthTexture)->GetBindlessTextureID();
                pc.SrcTexelSize      = 1.0f / glm::vec2(m_ViewportExtent.width, m_ViewportExtent.height);
                pc.DepthUnpackConsts = m_MainCamera->GetShaderData().DepthUnpackConsts;

                pc.Clusters = (const AABB*)scheduler.GetBuffer(lcdaPassData.LightClusterBuffer)->GetBDA();
                pc.ActiveLightClusters =
                    (Shaders::LightClusterActiveList*)scheduler.GetBuffer(lcdaPassData.LightClusterDetectActiveBuffer)->GetBDA();

                cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
                                                     0, pc);
                cmd.dispatch(glm::ceil(m_ViewportExtent.width / (f32)LIGHT_CLUSTERS_DETECT_ACTIVE_WG_SIZE_X),
                             glm::ceil(m_ViewportExtent.height / (f32)LIGHT_CLUSTERS_DETECT_ACTIVE_WG_SIZE_Y), 1);
            });
#endif

        struct LightClustersAssignmentPassData
        {
            RGResourceID CameraBuffer;
            RGResourceID LightClusterBuffer;
            RGResourceID LightClusterListBuffer;
            RGResourceID LightBuffer;
            RGResourceID LightClusterDetectActiveBuffer;
        } lcaPassData = {};
        m_RenderGraph->AddPass(
            "LightClustersAssignmentPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_COMPUTE,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateBuffer(ResourceNames::LightClusterListBuffer,
                                       GfxBufferDescription(sizeof(Shaders::LightClusterList) * LIGHT_CLUSTERS_COUNT,
                                                            sizeof(Shaders::LightClusterList), vk::BufferUsageFlagBits::eStorageBuffer,
                                                            EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT));
                lcaPassData.LightClusterListBuffer = scheduler.WriteBuffer(
                    ResourceNames::LightClusterListBuffer,
                    EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT | EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                lcaPassData.CameraBuffer =
                    scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT |
                                                                          EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                lcaPassData.LightClusterBuffer = scheduler.ReadBuffer(ResourceNames::LightClusterBuffer,
                                                                      EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT |
                                                                          EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                lcaPassData.LightBuffer =
                    scheduler.ReadBuffer(ResourceNames::LightBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT |
                                                                         EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
#if LIGHT_CLUSTERS_DETECT_ACTIVE
                lcaPassData.LightClusterDetectActiveBuffer = scheduler.ReadBuffer(
                    ResourceNames::LightClusterDetectActiveBuffer,
                    EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT | EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
#endif
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_LightClustersAssignmentPipeline.get());

                struct PushConstantBlock
                {
                    const Shaders::CameraData* CameraData;
                    const AABB* Clusters;
                    Shaders::LightClusterList* LightClusterList;
                    const Shaders::LightData* LightData;
#if LIGHT_CLUSTERS_SPLIT_DISPATCHES
                    u32 PointLightBatchOffset{0};
                    u32 PointLightBatchCount{0};
#endif
#if LIGHT_CLUSTERS_DETECT_ACTIVE
                    const Shaders::LightClusterActiveList* ActiveLightClusters;
#endif
                } pc = {};

                pc.CameraData       = (const Shaders::CameraData*)scheduler.GetBuffer(lcaPassData.CameraBuffer)->GetBDA();
                pc.Clusters         = (const AABB*)scheduler.GetBuffer(lcaPassData.LightClusterBuffer)->GetBDA();
                pc.LightData        = (const Shaders::LightData*)scheduler.GetBuffer(lcaPassData.LightBuffer)->GetBDA();
                pc.LightClusterList = (Shaders::LightClusterList*)scheduler.GetBuffer(lcaPassData.LightClusterListBuffer)->GetBDA();
#if LIGHT_CLUSTERS_DETECT_ACTIVE
                pc.ActiveLightClusters =
                    (Shaders::LightClusterActiveList*)scheduler.GetBuffer(lcaPassData.LightClusterDetectActiveBuffer)->GetBDA();
#endif

#if LIGHT_CLUSTERS_SPLIT_DISPATCHES
                const u32 lightBatchCount =
                    (m_LightData->PointLightCount + LIGHT_CLUSTERS_MAX_BATCH_LIGHT_COUNT - 1) / LIGHT_CLUSTERS_MAX_BATCH_LIGHT_COUNT;
                for (u64 lightBatchIndex{}; lightBatchIndex < lightBatchCount; ++lightBatchIndex)
                {
                    const u64 pointLightBatchCount =
                        glm::min((u64)LIGHT_CLUSTERS_MAX_BATCH_LIGHT_COUNT,
                                 m_LightData->PointLightCount - LIGHT_CLUSTERS_MAX_BATCH_LIGHT_COUNT * lightBatchIndex);
                    pc.PointLightBatchCount = pointLightBatchCount;

                    cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                         vk::ShaderStageFlagBits::eAll, 0, pc);
                    cmd.dispatch(glm::ceil(LIGHT_CLUSTERS_COUNT / (f32)LIGHT_CLUSTERS_ASSIGNMENT_WG_SIZE), 1, 1);

                    pc.PointLightBatchOffset += pointLightBatchCount;
                }
#else
                cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
                                                     0, pc);
                cmd.dispatch(glm::ceil(LIGHT_CLUSTERS_COUNT / (f32)LIGHT_CLUSTERS_ASSIGNMENT_WG_SIZE), 1, 1);
#endif
            });

#if 0
        struct ScreenSpaceShadowsPassData
        {
            RGResourceID CameraBuffer;
            RGResourceID DepthTexture;
            RGResourceID SSSTexture;
        } sssPassData = {};
        m_RenderGraph->AddPass(
            "ScreenSpaceShadowsPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_COMPUTE,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateTexture(
                    ResourceNames::SSSTexture,
                    GfxTextureDescription{.Type       = vk::ImageType::e2D,
                                          .Dimensions = glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                          .Format{vk::Format::eR8Unorm},
                                          .UsageFlags = vk::ImageUsageFlagBits::eStorage});
                sssPassData.SSSTexture =
                    scheduler.WriteTexture(ResourceNames::SSSTexture, EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                sssPassData.DepthTexture =
                    scheduler.ReadTexture(ResourceNames::GBufferDepth, EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                sssPassData.CameraBuffer =
                    scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT |
                                                                          EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_SSSPipeline.get());

                struct PushConstantBlock
                {
                    const Shaders::CameraData* CameraData{nullptr};
                    u32 DepthTextureID{0};
                    u32 SSSTextureID{0};
                    glm::vec3 SunDirection;
                } pc = {};

                auto& cameraUBO    = scheduler.GetBuffer(sssPassData.CameraBuffer);
                pc.CameraData      = (const Shaders::CameraData*)cameraUBO->GetBDA();
                auto& sssTexture   = scheduler.GetTexture(sssPassData.SSSTexture);
                pc.SSSTextureID    = sssTexture->GetBindlessImageID();
                auto& depthTexture = scheduler.GetTexture(sssPassData.DepthTexture);
                pc.DepthTextureID  = depthTexture->GetBindlessTextureID();
                pc.SunDirection    = m_LightData.Sun.Direction;

                cmd.pushConstants<PushConstantBlock>(* vk::ShaderStageFlagBits::eAll, 0, pc);
                cmd.dispatch(glm::ceil(sssTexture->GetDescription().Dimensions.x / 16.0f),
                             glm::ceil(sssTexture->GetDescription().Dimensions.y / 16.0f), 1);
            });
#endif

        struct SSAOPassData
        {
            RGResourceID CameraBuffer;
            RGResourceID DepthTexture;
            RGResourceID SSAOTexture;
        } ssaoPassData = {};

        struct SSAOBoxBlurPassData
        {
            RGResourceID SSAOTexture;
            RGResourceID SSAOTextureBlurred;
        } ssaoBoxBlurPassData = {};

        if (s_bEnableSSAO)
        {
            if (s_bSSAOComputeBased)
            {
                m_RenderGraph->AddPass(
                    "SSAOPassCompute", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_COMPUTE,
                    [&](RenderGraphResourceScheduler& scheduler)
                    {
                        scheduler.CreateTexture(ResourceNames::SSAOTexture,
                                                GfxTextureDescription(vk::ImageType::e2D,
                                                                      glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                                      vk::Format::eR8Unorm, vk::ImageUsageFlagBits::eStorage,
                                                                      vk::SamplerCreateInfo()
                                                                          .setMinFilter(vk::Filter::eNearest)
                                                                          .setMagFilter(vk::Filter::eNearest)
                                                                          .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                                                          .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                                                          .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)));

                        ssaoPassData.SSAOTexture  = scheduler.WriteTexture(ResourceNames::SSAOTexture, MipSet::FirstMip(),
                                                                           EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                        ssaoPassData.DepthTexture = scheduler.ReadTexture(ResourceNames::GBufferDepth, MipSet::FirstMip(),
                                                                          EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                        ssaoPassData.CameraBuffer = scheduler.ReadBuffer(
                            ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT |
                                                             EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                    },
                    [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
                    {
                        auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                        pipelineStateCache.Bind(cmd, m_SSAOPipelineCompute.get());

                        struct PushConstantBlock
                        {
                            u32 DstSSAOTextureID{0};
                            u32 DepthTextureID{0};
                            const Shaders::CameraData* CameraData{nullptr};
#if USE_THREAD_GROUP_TILING_X
                            uint3 WorkGroupNum{0};
#endif
                        } pc = {};

                        const uint3 workGroupNum = uint3(glm::ceil(m_ViewportExtent.width / (f32)SSAO_WG_SIZE_X),
                                                         glm::ceil(m_ViewportExtent.height / (f32)SSAO_WG_SIZE_Y), 1);

#if USE_THREAD_GROUP_TILING_X
                        pc.WorkGroupNum = workGroupNum;
#endif
                        pc.DstSSAOTextureID = scheduler.GetTexture(ssaoPassData.SSAOTexture)->GetBindlessImageID();
                        pc.DepthTextureID   = scheduler.GetTexture(ssaoPassData.DepthTexture)->GetBindlessTextureID();
                        pc.CameraData       = (const Shaders::CameraData*)scheduler.GetBuffer(ssaoPassData.CameraBuffer)->GetBDA();

                        cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                             vk::ShaderStageFlagBits::eAll, 0, pc);
                        cmd.dispatch(workGroupNum.x, workGroupNum.y, workGroupNum.z);
                    });

                m_RenderGraph->AddPass(
                    "SSAOBoxBlurPassCompute", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_COMPUTE,
                    [&](RenderGraphResourceScheduler& scheduler)
                    {
                        scheduler.CreateTexture(ResourceNames::SSAOTextureBlurred,
                                                GfxTextureDescription(vk::ImageType::e2D,
                                                                      glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                                      vk::Format::eR8Unorm, vk::ImageUsageFlagBits::eStorage,
                                                                      vk::SamplerCreateInfo()
                                                                          .setMinFilter(vk::Filter::eNearest)
                                                                          .setMagFilter(vk::Filter::eNearest)
                                                                          .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                                                          .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                                                          .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)));

                        ssaoBoxBlurPassData.SSAOTextureBlurred =
                            scheduler.WriteTexture(ResourceNames::SSAOTextureBlurred, MipSet::FirstMip(),
                                                   EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);

                        ssaoBoxBlurPassData.SSAOTexture = scheduler.ReadTexture(
                            ResourceNames::SSAOTexture, MipSet::FirstMip(), EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                    },
                    [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
                    {
                        auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                        pipelineStateCache.Bind(cmd, m_SSAOBoxBlurPipelineCompute.get());

                        const auto& ssaoTexture = scheduler.GetTexture(ssaoBoxBlurPassData.SSAOTexture);
                        struct PushConstantBlock
                        {
                            u32 SSAOBlurredTextureID{0};
                            u32 SSAOTextureID{0};
                            glm::vec2 SrcTexelSize{1.f};
                        } pc                    = {};
                        pc.SSAOBlurredTextureID = scheduler.GetTexture(ssaoBoxBlurPassData.SSAOTextureBlurred)->GetBindlessImageID();
                        pc.SSAOTextureID        = ssaoTexture->GetBindlessTextureID();
                        pc.SrcTexelSize         = 1.0f / glm::vec2(ssaoTexture->GetDescription().Dimensions);

                        cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                             vk::ShaderStageFlagBits::eAll, 0, pc);
                        cmd.dispatch(glm::ceil(m_ViewportExtent.width / (f32)SSAO_WG_SIZE_X),
                                     glm::ceil(m_ViewportExtent.height / (f32)SSAO_WG_SIZE_Y), 1);
                    });
            }
            else
            {
                m_RenderGraph->AddPass(
                    "SSAOPassGraphics", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
                    [&](RenderGraphResourceScheduler& scheduler)
                    {
                        scheduler.CreateTexture(ResourceNames::SSAOTexture,
                                                GfxTextureDescription(vk::ImageType::e2D,
                                                                      glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                                      vk::Format::eR8Unorm, vk::ImageUsageFlagBits::eColorAttachment,
                                                                      vk::SamplerCreateInfo()
                                                                          .setMinFilter(vk::Filter::eNearest)
                                                                          .setMagFilter(vk::Filter::eNearest)
                                                                          .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                                                          .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                                                          .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)));

                        scheduler.WriteRenderTarget(ResourceNames::SSAOTexture, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                                    vk::AttachmentStoreOp::eStore,
                                                    vk::ClearColorValue().setFloat32({1.0f, 1.0f, 1.0f, 1.0f}));
                        ssaoPassData.DepthTexture = scheduler.ReadTexture(ResourceNames::GBufferDepth, MipSet::FirstMip(),
                                                                          EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);
                        ssaoPassData.CameraBuffer = scheduler.ReadBuffer(
                            ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT |
                                                             EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);

                        scheduler.SetViewportScissors(vk::Viewport()
                                                          .setMinDepth(0.0f)
                                                          .setMaxDepth(1.0f)
                                                          .setWidth(m_ViewportExtent.width)
                                                          .setHeight(m_ViewportExtent.height),
                                                      vk::Rect2D().setExtent(m_ViewportExtent));
                    },
                    [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
                    {
                        auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                        pipelineStateCache.Bind(cmd, m_SSAOPipelineGraphics.get());

                        struct PushConstantBlock
                        {
                            const Shaders::CameraData* CameraData{nullptr};
                            u32 DepthTextureID{0};
                        } pc = {};

                        pc.DepthTextureID = scheduler.GetTexture(ssaoPassData.DepthTexture)->GetBindlessTextureID();
                        pc.CameraData     = (const Shaders::CameraData*)scheduler.GetBuffer(ssaoPassData.CameraBuffer)->GetBDA();

                        cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                             vk::ShaderStageFlagBits::eAll, 0, pc);
                        cmd.draw(3, 1, 0, 0);
                    });

                m_RenderGraph->AddPass(
                    "SSAOBoxBlurPassGraphics", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
                    [&](RenderGraphResourceScheduler& scheduler)
                    {
                        scheduler.CreateTexture(ResourceNames::SSAOTextureBlurred,
                                                GfxTextureDescription(vk::ImageType::e2D,
                                                                      glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                                      vk::Format::eR8Unorm, vk::ImageUsageFlagBits::eColorAttachment,
                                                                      vk::SamplerCreateInfo()
                                                                          .setMinFilter(vk::Filter::eNearest)
                                                                          .setMagFilter(vk::Filter::eNearest)
                                                                          .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                                                          .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                                                          .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)));

                        scheduler.WriteRenderTarget(ResourceNames::SSAOTextureBlurred, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                                    vk::AttachmentStoreOp::eStore,
                                                    vk::ClearColorValue().setFloat32({1.0f, 1.0f, 1.0f, 1.0f}));

                        ssaoBoxBlurPassData.SSAOTexture =
                            scheduler.ReadTexture(ResourceNames::SSAOTexture, MipSet::FirstMip(),
                                                  EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);

                        scheduler.SetViewportScissors(vk::Viewport()
                                                          .setMinDepth(0.0f)
                                                          .setMaxDepth(1.0f)
                                                          .setWidth(m_ViewportExtent.width)
                                                          .setHeight(m_ViewportExtent.height),
                                                      vk::Rect2D().setExtent(m_ViewportExtent));
                    },
                    [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
                    {
                        auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                        pipelineStateCache.Bind(cmd, m_SSAOBoxBlurPipelineGraphics.get());

                        const auto& ssaoTexture = scheduler.GetTexture(ssaoBoxBlurPassData.SSAOTexture);
                        struct PushConstantBlock
                        {
                            u32 TextureID{0};
                            glm::vec2 SrcTexelSize{1.f};
                        } pc            = {};
                        pc.TextureID    = ssaoTexture->GetBindlessTextureID();
                        pc.SrcTexelSize = 1.0f / glm::vec2(ssaoTexture->GetDescription().Dimensions);

                        cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                             vk::ShaderStageFlagBits::eAll, 0, pc);
                        cmd.draw(3, 1, 0, 0);
                    });
            }
        }

        struct MainPassShaderData
        {
            u32 EnvironmentMapTextureCubeID{0};
            u32 SSAOTextureID{0};
            u32 SSSTextureID{0};
            float2 ScaleBias{0.0f, 0.0f};  // For clustered shading, x - scale, y - bias
            const Shaders::CascadedShadowMapsData* CSMData{nullptr};
            u32 CSMTextureIDs[SHADOW_MAP_CASCADE_COUNT] = {0};
        };

        struct MainPassData
        {
            RGResourceID DepthTexture;
            RGResourceID CameraBuffer;
            RGResourceID LightBuffer;
            RGResourceID LightClusterListBuffer;
            RGResourceID DebugDataBuffer;
            RGResourceID SSSTexture;
            RGResourceID SSAOTexture;

            RGResourceID CSMTextures[SHADOW_MAP_CASCADE_COUNT];
            RGResourceID CSMDataBuffer;
            RGResourceID MainPassShaderDataBuffer;
        } mainPassData = {};
        m_RenderGraph->AddPass(
            "MainPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateTexture(ResourceNames::GBufferAlbedo,
                                        GfxTextureDescription(vk::ImageType::e2D,
                                                              glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                              vk::Format::eR16G16B16A16Sfloat, vk::ImageUsageFlagBits::eColorAttachment));

                scheduler.WriteRenderTarget(ResourceNames::GBufferAlbedo, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                            vk::AttachmentStoreOp::eStore,
                                            vk::ClearColorValue().setFloat32({s_SunColor.x, s_SunColor.y, s_SunColor.z, 1.0f}));
                mainPassData.DepthTexture = scheduler.ReadTexture(ResourceNames::GBufferDepth, MipSet::FirstMip(),
                                                                  EResourceStateBits::RESOURCE_STATE_DEPTH_READ_BIT);

                mainPassData.CameraBuffer =
                    scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT |
                                                                          EResourceStateBits::RESOURCE_STATE_VERTEX_SHADER_RESOURCE_BIT |
                                                                          EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);

                mainPassData.LightBuffer =
                    scheduler.ReadBuffer(ResourceNames::LightBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT |
                                                                         EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);

                mainPassData.LightClusterListBuffer = scheduler.ReadBuffer(
                    ResourceNames::LightClusterListBuffer, EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT |
                                                               EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);

                mainPassData.CSMDataBuffer =
                    scheduler.ReadBuffer(ResourceNames::CSMDataBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT |
                                                                           EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);

                scheduler.CreateBuffer(ResourceNames::MainPassShaderDataBuffer,
                                       GfxBufferDescription(sizeof(MainPassShaderData), sizeof(MainPassShaderData),
                                                            vk::BufferUsageFlagBits::eUniformBuffer,
                                                            EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT));
                mainPassData.MainPassShaderDataBuffer = scheduler.WriteBuffer(
                    ResourceNames::MainPassShaderDataBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT |
                                                                 EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);

                for (u32 cascadeIndex{}; cascadeIndex < SHADOW_MAP_CASCADE_COUNT; ++cascadeIndex)
                {
                    const auto textureName                 = "CSMTexture" + std::to_string(cascadeIndex);
                    mainPassData.CSMTextures[cascadeIndex] = scheduler.ReadTexture(
                        textureName, MipSet::FirstMip(), EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);
                }

                // mainPassData.SSSTexture = scheduler.ReadTexture(ResourceNames::SSSTexture,
                // EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);

                if (s_bEnableSSAO)
                {
                    mainPassData.SSAOTexture = scheduler.ReadTexture(ResourceNames::SSAOTextureBlurred, MipSet::FirstMip(),
                                                                     EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);
                }

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
                    vk::Rect2D().setExtent(m_ViewportExtent));
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_MainLightingPassPipeline.get());

                auto& cameraUBO              = scheduler.GetBuffer(mainPassData.CameraBuffer);
                auto& lightUBO               = scheduler.GetBuffer(mainPassData.LightBuffer);
                auto& lightClusterListBuffer = scheduler.GetBuffer(mainPassData.LightClusterListBuffer);

                auto& mainPassShaderDataBuffer = scheduler.GetBuffer(mainPassData.MainPassShaderDataBuffer);

                MainPassShaderData mpsData = {};
                for (u32 k{}; k < SHADOW_MAP_CASCADE_COUNT; ++k)
                {
                    mpsData.CSMTextureIDs[k] = scheduler.GetTexture(mainPassData.CSMTextures[k])->GetBindlessTextureID();
                }

                const auto zFar   = m_MainCamera->GetZFar();
                const auto zNear  = m_MainCamera->GetZNear();
                mpsData.ScaleBias = {static_cast<f32>(LIGHT_CLUSTERS_SUBDIVISION_Z) / glm::log2(zFar / zNear),
                                     -static_cast<f32>(LIGHT_CLUSTERS_SUBDIVISION_Z) * glm::log2(zNear) / glm::log2(zFar / zNear)};
                mpsData.CSMData   = (const Shaders::CascadedShadowMapsData*)scheduler.GetBuffer(mainPassData.CSMDataBuffer)->GetBDA();
                if (s_bEnableSSAO)
                {
                    mpsData.SSAOTextureID = scheduler.GetTexture(mainPassData.SSAOTexture)->GetBindlessTextureID();
                }
                //   mpsData.SSSTextureID  = scheduler.GetTexture(mainPassData.SSSTexture)->GetBindlessTextureID();
                //                mpsData.EnvironmentMapTextureCubeID = m_EnvMapTexture->GetBindlessTextureID();

                mainPassShaderDataBuffer->SetData(&mpsData, sizeof(mpsData));

                for (const auto& ro : m_DrawContext.RenderObjects)
                {
                    ++s_DrawCallCount;

                    struct PushConstantBlock
                    {
                        glm::vec3 scale{1.f};
                        glm::vec3 translation{0.f};
                        float4 orientation{1};
                        const Shaders::CameraData* CameraData{nullptr};
                        const VertexPosition* VtxPositions{nullptr};
                        const VertexAttribute* VtxAttributes{nullptr};
                        const Shaders::GLTFMaterial* MaterialData{nullptr};
                        const Shaders::LightData* LightData{nullptr};
                        const Shaders::LightClusterList* LightClusterList{nullptr};
                        const MainPassShaderData* MPSData{nullptr};
                    } pc = {};

                    pc.MPSData = (const MainPassShaderData*)mainPassShaderDataBuffer->GetBDA();

                    pc.LightData        = (const Shaders::LightData*)lightUBO->GetBDA();
                    pc.LightClusterList = (const Shaders::LightClusterList*)lightClusterListBuffer->GetBDA();
                    pc.CameraData       = (const Shaders::CameraData*)cameraUBO->GetBDA();

                    glm::quat q{1.0f, 0.0f, 0.0f, 0.0f};
                    glm::vec3 decomposePlaceholder0{1.0f};
                    glm::vec4 decomposePlaceholder1{1.0f};
                    glm::decompose(ro.TRS, pc.scale, q, pc.translation, decomposePlaceholder0, decomposePlaceholder1);
                    pc.orientation = glm::packHalf(glm::vec4(q.w, q.x, q.y, q.z) * 0.5f + 0.5f);
                    pc.scale *= s_MeshScale;
                    pc.orientation = glm::vec4(q.w, q.x, q.y, q.z);

                    pc.VtxPositions  = (const VertexPosition*)ro.VertexPositionBuffer->GetBDA();
                    pc.VtxAttributes = (const VertexAttribute*)ro.VertexAttributeBuffer->GetBDA();
                    pc.MaterialData  = (const Shaders::GLTFMaterial*)ro.MaterialBuffer->GetBDA();

                    const auto currentDepthCompareOp =
                        ro.AlphaMode == EAlphaMode::ALPHA_MODE_OPAQUE ? vk::CompareOp::eEqual : vk::CompareOp::eGreaterOrEqual;
                    pipelineStateCache.Set(cmd, currentDepthCompareOp);
                    pipelineStateCache.Set(cmd, ro.CullMode);
                    pipelineStateCache.Set(cmd, ro.PrimitiveTopology);

                    cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                         vk::ShaderStageFlagBits::eAll, 0, pc);
                    pipelineStateCache.Bind(cmd, ro.IndexBuffer.get(), 0, ro.IndexType);
                    cmd.drawIndexed(ro.IndexCount, 1, ro.FirstIndex, 0, 0);
                }

                /*      auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                            pipelineStateCache.Bind(cmd, m_EnvMapSkyboxPipeline.get());*/
            });

        // TODO: Cleanup bloom code
        static constexpr u32 s_BloomMipCount{6};
        struct BloomMipChainData
        {
            glm::vec2 Size{1.0f};
        };
        std::vector<BloomMipChainData> bloomMipChain(s_BloomMipCount);

        // 1. Downsample
        struct BloomDownsamplePassData
        {
            RGResourceID SrcTexture;
            RGResourceID DstTexture;
        };
        std::vector<BloomDownsamplePassData> bdPassDatas(s_BloomMipCount);
        for (u32 i{}; i < s_BloomMipCount - 1; ++i)
        {
            if (i == 0)
            {
                bloomMipChain[i].Size = {m_ViewportExtent.width, m_ViewportExtent.height};
            }
            else
            {
                bloomMipChain[i].Size = bloomMipChain[i - 1].Size;
            }
            bloomMipChain[i].Size = glm::ceil(bloomMipChain[i].Size / 2.0f);
            bloomMipChain[i].Size = glm::max(bloomMipChain[i].Size, 1.0f);

            const auto currentViewportExtent = vk::Extent2D(bloomMipChain[i].Size.x, bloomMipChain[i].Size.y);
            const std::string passName       = "BloomDownsample" + std::to_string(i);
            const std::string textureName    = "BloomDownsampleTexture";

            if (s_bBloomComputeBased)
            {
                m_RenderGraph->AddPass(
                    passName, ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_COMPUTE,
                    [&, i](RenderGraphResourceScheduler& scheduler)
                    {
                        if (i == 0)
                        {
                            scheduler.CreateTexture(
                                textureName, GfxTextureDescription(
                                                 vk::ImageType::e2D, glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                 vk::Format::eB10G11R11UfloatPack32, vk::ImageUsageFlagBits::eStorage,
                                                 vk::SamplerCreateInfo()
                                                     .setMinFilter(vk::Filter::eLinear)
                                                     .setMagFilter(vk::Filter::eLinear)
                                                     .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                                     .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                                     .setAddressModeW(vk::SamplerAddressMode::eClampToEdge),
                                                 1, vk::SampleCountFlagBits::e1, EResourceCreateBits::RESOURCE_CREATE_EXPOSE_MIPS_BIT));

                            bdPassDatas[i].SrcTexture =
                                scheduler.ReadTexture(ResourceNames::GBufferAlbedo, MipSet::FirstMip(),
                                                      EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                        }
                        else
                        {
                            bdPassDatas[i].SrcTexture = scheduler.ReadTexture(
                                textureName, MipSet::Explicit(i), EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                        }

                        bdPassDatas[i].DstTexture = scheduler.WriteTexture(textureName, MipSet::Explicit(i + 1),
                                                                           EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                    },
                    [&, i](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
                    {
                        auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                        pipelineStateCache.Bind(cmd, m_BloomDownsamplePipelineCompute.get());

                        struct PushConstantBlock
                        {
                            u32 SrcTextureID;
                            u32 DstTextureID;
                            u32 MipLevel;
                            glm::vec2 SrcTexelSize;  // rcp(SrcTextureResolution)
                        } pc            = {};
                        pc.DstTextureID = scheduler.GetTexture(bdPassDatas[i].DstTexture)->GetBindlessImageID(i + 1);
                        pc.SrcTextureID = scheduler.GetTexture(bdPassDatas[i].SrcTexture)->GetBindlessTextureID(i);
                        pc.MipLevel     = i;
                        pc.SrcTexelSize = 1.0f / bloomMipChain[i].Size;

                        cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                             vk::ShaderStageFlagBits::eAll, 0, pc);
                        cmd.dispatch(glm::ceil(bloomMipChain[i].Size.x / (f32)BLOOM_WG_SIZE_X),
                                     glm::ceil(bloomMipChain[i].Size.y / (f32)BLOOM_WG_SIZE_Y), 1);
                    });
            }
            else
            {
                m_RenderGraph->AddPass(
                    passName, ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
                    [&, i](RenderGraphResourceScheduler& scheduler)
                    {
                        if (i == 0)
                        {
                            scheduler.CreateTexture(
                                textureName, GfxTextureDescription(
                                                 vk::ImageType::e2D, glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                 vk::Format::eB10G11R11UfloatPack32, vk::ImageUsageFlagBits::eColorAttachment,
                                                 vk::SamplerCreateInfo()
                                                     .setMinFilter(vk::Filter::eLinear)
                                                     .setMagFilter(vk::Filter::eLinear)
                                                     .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                                     .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                                     .setAddressModeW(vk::SamplerAddressMode::eClampToEdge),
                                                 1, vk::SampleCountFlagBits::e1, EResourceCreateBits::RESOURCE_CREATE_EXPOSE_MIPS_BIT));

                            bdPassDatas[i].SrcTexture =
                                scheduler.ReadTexture(ResourceNames::GBufferAlbedo, MipSet::FirstMip(),
                                                      EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);
                        }
                        else
                        {
                            bdPassDatas[i].SrcTexture = scheduler.ReadTexture(
                                textureName, MipSet::Explicit(i), EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);
                        }

                        scheduler.WriteRenderTarget(textureName, MipSet::Explicit(i + 1), vk::AttachmentLoadOp::eClear,
                                                    vk::AttachmentStoreOp::eStore,
                                                    vk::ClearColorValue().setFloat32({0.0f, 0.0f, 0.0f, 1.0f}));

                        scheduler.SetViewportScissors(vk::Viewport()
                                                          .setMinDepth(0.0f)
                                                          .setMaxDepth(1.0f)
                                                          .setWidth(currentViewportExtent.width)
                                                          .setHeight(currentViewportExtent.height),
                                                      vk::Rect2D().setExtent(currentViewportExtent));
                    },
                    [&, i](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
                    {
                        auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                        pipelineStateCache.Bind(cmd, m_BloomDownsamplePipelineGraphics.get());

                        struct PushConstantBlock
                        {
                            u32 SrcTextureID;
                            u32 MipLevel;
                            glm::vec2 SrcTexelSize;  // rcp(SrcTextureResolution)
                        } pc            = {};
                        pc.SrcTextureID = scheduler.GetTexture(bdPassDatas[i].SrcTexture)->GetBindlessTextureID(i);
                        pc.MipLevel     = i;
                        pc.SrcTexelSize = 1.0f / (bloomMipChain[i].Size * 2.0f);

                        cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                             vk::ShaderStageFlagBits::eAll, 0, pc);
                        cmd.draw(3, 1, 0, 0);
                    });
            }
        }

        // don't forget the smallest mip:
        bloomMipChain[s_BloomMipCount - 1].Size = glm::ceil(bloomMipChain[s_BloomMipCount - 2].Size / 2.0f);
        bloomMipChain[s_BloomMipCount - 1].Size = glm::max(bloomMipChain[s_BloomMipCount - 1].Size, 1.0f);

        // 2. Upsample + blur
        struct BloomUpsampleBlurPassData
        {
            RGResourceID SrcTexture;
            RGResourceID DstTexture;
        };
        std::vector<BloomUpsampleBlurPassData> bubPassDatas(s_BloomMipCount);
        for (i32 i = s_BloomMipCount - 1; i > 0; --i)
        {
            const glm::uvec2 nextMipSize =
                glm::min(bloomMipChain[i - 1].Size * 2.0f, glm::vec2(m_ViewportExtent.width, m_ViewportExtent.height));

            const auto currentViewportExtent = vk::Extent2D(nextMipSize.x, nextMipSize.y);
            const std::string passName       = "BloomUpsampleBlur" + std::to_string(i - 1);
            const std::string textureName    = "BloomUpsampleBlurTexture" + std::to_string(i - 1);

            if (s_bBloomComputeBased)
            {
                m_RenderGraph->AddPass(
                    passName, ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_COMPUTE,
                    [&](RenderGraphResourceScheduler& scheduler)
                    {
                        const std::string prevTextureName =
                            (i == s_BloomMipCount - 1 ? "BloomDownsampleTexture" : "BloomUpsampleBlurTexture" + std::to_string(i));

                        bubPassDatas[i].DstTexture =
                            scheduler.WriteTexture(prevTextureName, MipSet::Explicit(i - 1),
                                                   EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT, textureName);

                        bubPassDatas[i].SrcTexture = scheduler.ReadTexture(prevTextureName, MipSet::Explicit(i),
                                                                           EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                    },
                    [&, i, nextMipSize](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
                    {
                        auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                        pipelineStateCache.Bind(cmd, m_BloomUpsampleBlurPipelineCompute.get());

                        struct PushConstantBlock
                        {
                            u32 SrcTextureID;
                            u32 DstTextureID;
                            u32 MipLevel;         // I need this to prevent loading first mip level, since its unitialized!
                            float2 SrcTexelSize;  // rcp(SrcTextureResolution)
                        } pc            = {};
                        pc.MipLevel     = i - 1;
                        pc.DstTextureID = scheduler.GetTexture(bubPassDatas[i].DstTexture)->GetBindlessImageID(i - 1);
                        pc.SrcTextureID = scheduler.GetTexture(bubPassDatas[i].SrcTexture)->GetBindlessTextureID(i);
                        pc.SrcTexelSize = 1.0f / (bloomMipChain[i].Size * 4.0f);

                        cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                             vk::ShaderStageFlagBits::eAll, 0, pc);
                        cmd.dispatch(glm::ceil(nextMipSize.x / (f32)BLOOM_WG_SIZE_X), glm::ceil(nextMipSize.y / (f32)BLOOM_WG_SIZE_Y), 1);
                    });
            }
            else
            {
                m_RenderGraph->AddPass(
                    passName, ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
                    [&](RenderGraphResourceScheduler& scheduler)
                    {
                        const std::string prevTextureName =
                            (i == s_BloomMipCount - 1 ? "BloomDownsampleTexture" : "BloomUpsampleBlurTexture" + std::to_string(i));
                        const auto loadOp = i - 1 == 0 ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
                        scheduler.WriteRenderTarget(prevTextureName, MipSet::Explicit(i - 1), loadOp, vk::AttachmentStoreOp::eStore,
                                                    vk::ClearColorValue().setFloat32({0.0f, 0.0f, 0.0f, 1.0f}), textureName);

                        bubPassDatas[i].SrcTexture = scheduler.ReadTexture(prevTextureName, MipSet::Explicit(i),
                                                                           EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);
                        scheduler.SetViewportScissors(vk::Viewport()
                                                          .setMinDepth(0.0f)
                                                          .setMaxDepth(1.0f)
                                                          .setWidth(currentViewportExtent.width)
                                                          .setHeight(currentViewportExtent.height),
                                                      vk::Rect2D().setExtent(currentViewportExtent));
                    },
                    [&, i](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
                    {
                        auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                        pipelineStateCache.Bind(cmd, m_BloomUpsampleBlurPipelineGraphics.get());

                        struct PushConstantBlock
                        {
                            u32 SrcTextureID;
                            glm::vec2 SrcTexelSize;  // rcp(SrcTextureResolution)
                        } pc            = {};
                        pc.SrcTextureID = scheduler.GetTexture(bubPassDatas[i].SrcTexture)->GetBindlessTextureID(i);
                        pc.SrcTexelSize = 1.0f / bloomMipChain[i].Size;

                        cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                             vk::ShaderStageFlagBits::eAll, 0, pc);
                        cmd.draw(3, 1, 0, 0);
                    });
            }
        }

        struct FinalPassData
        {
            RGResourceID BloomTexture;
            RGResourceID MainPassTexture;
        } finalPassData = {};
        m_RenderGraph->AddPass(
            "FinalPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateTexture(
                    ResourceNames::FinalPassTexture,
                    GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                          vk::Format::eA2B10G10R10UnormPack32,
                                          vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc));
                scheduler.WriteRenderTarget(ResourceNames::FinalPassTexture, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                            vk::AttachmentStoreOp::eStore, vk::ClearColorValue().setFloat32({0.0f, 0.0f, 0.0f, 0.0f}));

                finalPassData.BloomTexture    = scheduler.ReadTexture("BloomUpsampleBlurTexture0", MipSet::FirstMip(),
                                                                      EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);
                finalPassData.MainPassTexture = scheduler.ReadTexture(ResourceNames::GBufferAlbedo, MipSet::FirstMip(),
                                                                      EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
                    vk::Rect2D().setExtent(m_ViewportExtent));
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_FinalPassPipeline.get());

                struct PushConstantBlock
                {
                    u32 MainPassTextureID;
                    u32 BloomTextureID;
                } pc                 = {};
                pc.MainPassTextureID = scheduler.GetTexture(finalPassData.MainPassTexture)->GetBindlessTextureID();
                pc.BloomTextureID    = scheduler.GetTexture(finalPassData.BloomTexture)->GetBindlessTextureID();

                cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
                                                     0, pc);
                cmd.draw(3, 1, 0, 0);
            });

        std::string finalPassAfterDebugTextureView = ResourceNames::FinalPassTexture;
        {
            std::vector<std::string> debugTextureViewNames;
            for (u32 cascadeIndex{}; cascadeIndex < csmPassDatas.size(); ++cascadeIndex)
            {
                const auto textureName = "CSMTexture" + std::to_string(cascadeIndex);
                debugTextureViewNames.emplace_back(textureName);
            }
            finalPassAfterDebugTextureView =
                m_DebugRenderer->DrawTextureView(m_ViewportExtent, m_RenderGraph, debugTextureViewNames, finalPassAfterDebugTextureView);
        }

        m_ProfilerWindow.m_GPUGraph.LoadFrameData(m_GfxContext->GetLastFrameGPUProfilerData());
        m_ProfilerWindow.m_CPUGraph.LoadFrameData(m_GfxContext->GetLastFrameCPUProfilerData());

        m_UIRenderer->RenderFrame(
            m_ViewportExtent, m_RenderGraph, finalPassAfterDebugTextureView,
            [&]()
            {
                static bool bShowDemoWindow = true;
                if (bShowDemoWindow) ImGui::ShowDemoWindow(&bShowDemoWindow);

                m_ProfilerWindow.Render();

                if (ImGui::Begin("Application Info"))
                {
                    const auto& io = ImGui::GetIO();
                    ImGui::Text("Application average [%.3f] ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

                    ImGui::Separator();
                    ImGui::Text("Renderer: %s", m_GfxContext->GetDevice()->GetGPUProperties().deviceName);

                    ImGui::Separator();
                    ImGui::Text("DrawCalls: %zu", s_DrawCallCount);

                    ImGui::Text("Swapchain Present Mode: ");
                    ImGui::SameLine();

                    const auto currentPresentModeStr = vk::to_string(m_GfxContext->GetPresentMode());
                    const auto& presentModes         = m_GfxContext->GetSupportedPresentModesList();
                    std::vector<std::string> presentModeStrs{};
                    for (auto& presentMode : presentModes)
                        presentModeStrs.emplace_back(vk::to_string(presentMode));

                    if (ImGui::BeginCombo("##Swapchain_Present_Mode", currentPresentModeStr.data(),
                                          ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_WidthFitPreview))
                    {
                        for (u32 i{}; i < presentModeStrs.size(); ++i)
                        {
                            const auto& presentModeStr = presentModeStrs[i];

                            const auto bIsSelected = currentPresentModeStr == presentModeStr;
                            if (ImGui::Selectable(presentModeStr.data(), bIsSelected)) m_GfxContext->SetPresentMode(presentModes[i]);
                            if (bIsSelected) ImGui::SetItemDefaultFocus();
                        }

                        ImGui::EndCombo();
                    }

                    if (ImGui::TreeNodeEx("Bindless Resources Statistics", ImGuiTreeNodeFlags_Framed))
                    {
                        ImGui::Text("Storage Images and Textures can overlap.");
                        const auto bindlessStatistics = m_GfxContext->GetDevice()->GetBindlessStatistics();
                        ImGui::Text("Storage Images Used: %zu", bindlessStatistics.ImagesUsed);
                        ImGui::Text("Textures Used: %zu", bindlessStatistics.TexturesUsed);
                        ImGui::Text("Samplers Used: %zu", bindlessStatistics.SamplersUsed);

                        ImGui::TreePop();
                    }

                    ImGui::Separator();
                    if (ImGui::TreeNodeEx("RenderGraph Statistics", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Text("Build Time: [%.3f] ms", m_RenderGraphStats.BuildTime);
                        ImGui::Text("Barrier Batch Count: %u", m_RenderGraphStats.BarrierBatchCount);
                        ImGui::Text("Barrier Count: %u", m_RenderGraphStats.BarrierCount);

                        m_RenderGraphResourcePool->UI_ShowResourceUsage();

                        ImGui::TreePop();
                    }

                    ImGui::Separator();
                    ImGui::Text("Camera Position: %s", glm::to_string(m_MainCamera->GetShaderData().Position).data());

                    if (ImGui::TreeNodeEx("Sun Parameters", ImGuiTreeNodeFlags_Framed))
                    {
                        ImGui::DragFloat3("Direction", (f32*)&m_LightData->Sun.Direction, 0.01f, -1.0f, 1.0f);

                        ImGui::DragFloat("Intensity", &m_LightData->Sun.Intensity, 0.01f, 0.0f, 5.0f);
                        ImGui::Checkbox("Cast Shadows", &m_LightData->Sun.bCastShadows);

                        if (ImGui::DragFloat3("Radiance", (f32*)&s_SunColor, 0.01f, 0.0f, 1.0f))
                        {
                            m_LightData->Sun.Color = Shaders::PackUnorm4x8(glm::vec4(s_SunColor, 1.0f));
                        }

                        ImGui::TreePop();
                    }
                }

                ImGui::Separator();
                ImGui::Checkbox("Bloom Use Compute", &s_bBloomComputeBased);
                ImGui::Checkbox("Enable SSAO", &s_bEnableSSAO);
                ImGui::Checkbox("SSAO Use Compute (Better Quality)", &s_bSSAOComputeBased);
                ImGui::Checkbox("Update Lights", &s_bUpdateLights);

                if (ImGui::TreeNodeEx("Cascaded Shadow Maps", ImGuiTreeNodeFlags_Framed))
                {
                    ImGui::Checkbox("Compute Tight Bounds (SDSM)", &s_bComputeTightBounds);
                    ImGui::Checkbox("Keep Cascades at Constant Size", &s_bKeepCascadeConstantSize);
                    ImGui::Checkbox("Keep Cascades Squared", &s_bKeepCascadeSquared);
                    ImGui::Checkbox("Round Cascades to Pixel Size", &s_bRoundCascadeToPixelSize);
                    ImGui::DragFloat("Cascade Split Delta", &s_CascadeSplitDelta, 0.001f, 0.0f, 1.0f);
                    ImGui::TreePop();
                }

                ImGui::End();
            });

        m_RenderGraph->Build();
        m_RenderGraph->Execute();

        m_RenderGraphStats = m_RenderGraph->GetStatistics();
    }

}  // namespace Radiant
