#include <pch.h>
#include "CombinedRenderer.hpp"

#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

#include <clustered_shading/light_clusters_defines.hpp>
#include <csm/csm_defines.hpp>

namespace Radiant
{
    namespace ResourceNames
    {
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

    static bool s_bComputeSSAO{false};
    static bool s_bUpdateLights{true};
    static bool s_bBloomComputeBased{false};

    static f32 s_MeshScale = 0.01f;

    static bool s_bComputeTightFrustums{false};
    static bool s_bKeepCascadeConstantSize{false};
    static bool s_bKeepCascadeSquared{false};
    static bool s_bRoundCascadeToPixelSize{false};
    static f32 s_CascadeSplitDelta{0.910f};

    CombinedRenderer::CombinedRenderer() noexcept
    {
        m_MainCamera = MakeShared<Camera>(70.0f, static_cast<f32>(m_ViewportExtent.width) / static_cast<f32>(m_ViewportExtent.height),
                                          1000.0f, 0.0001f);
        m_Scene      = MakeUnique<Scene>("CombinedRendererTest");

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
                auto depthPrePassShader                   = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                                GfxShaderDescription{.Path = "../Assets/Shaders/depth_pre_pass.slang"});
                const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eD32Sfloat},
                                                             .DynamicStates{vk::DynamicState::eCullMode, vk::DynamicState::ePrimitiveTopology},
                                                             .CullMode{vk::CullModeFlagBits::eBack},
                                                             .FrontFace{vk::FrontFace::eCounterClockwise},
                                                             .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                             .PolygonMode{vk::PolygonMode::eFill},
                                                             .bDepthTest{true},
                                                             .bDepthWrite{true},
                                                             .DepthCompareOp{vk::CompareOp::eGreaterOrEqual}};
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName = "depth_pre_pass", .PipelineOptions = gpo, .Shader = depthPrePassShader};
                m_DepthPrePassPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                // NOTE: To not create many pipelines for objects, I switch depth compare op based on AlphaMode of object.
                auto pbrShader =
                    MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/PBR.slang"});
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
                    .BlendMode{GfxGraphicsPipelineOptions::EBlendMode::BLEND_MODE_ALPHA}};
                const GfxPipelineDescription pipelineDesc = {.DebugName = "PBR", .PipelineOptions = gpo, .Shader = pbrShader};
                m_PBRPipeline                             = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
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
                    .Shader          = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                    GfxShaderDescription{.Path = "../Assets/Shaders/sss/screen_space_shadows.slang"})};
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
                const GfxPipelineDescription pipelineDesc = {.DebugName = "SSAO", .PipelineOptions = gpo, .Shader = ssaoShader};
                m_SSAOPipeline                            = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
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
                    .DebugName = "SSAOBoxBlur", .PipelineOptions = gpo, .Shader = ssaoBoxBlurShader};
                m_SSAOBoxBlurPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
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
                    .DebugName = "BloomDownsample", .PipelineOptions = gpo, .Shader = bloomDownsampleShader};
                m_BloomDownsamplePipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
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
                                                             .BlendMode{GfxGraphicsPipelineOptions::EBlendMode::BLEND_MODE_ADDITIVE}};
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName = "BloomUpsampleBlur", .PipelineOptions = gpo, .Shader = bloomUpsampleBlurShader};
                m_BloomUpsampleBlurPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        // Compute optimized bloom
        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName       = "BloomDownsample",
                    .PipelineOptions = GfxComputePipelineOptions{},
                    .Shader          = MakeShared<GfxShader>(
                        m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/bloom/bloom_downsample_compute.slang"})};
                m_BloomDownsamplePipelineOptimized = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        pipelinesToCreate.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName       = "BloomUpsampleBlur",
                    .PipelineOptions = GfxComputePipelineOptions{},
                    .Shader =
                        MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                              GfxShaderDescription{.Path = "../Assets/Shaders/bloom/bloom_upsample_blur_compute.slang"})};
                m_BloomUpsampleBlurPipelineOptimized = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        const auto pipelineCreateBegin = Timer::Now();
        for (auto& pipeline : pipelinesToCreate)
        {
            pipeline.get();
        }
        LOG_INFO("Time taken to create {} pipelines: {} seconds.", pipelinesToCreate.size(),
                 Timer::GetElapsedSecondsFromNow(pipelineCreateBegin));

        m_LightData.Sun.bCastShadows = 1;
        m_LightData.Sun.Direction    = {0.0f, 0.8f, 0.5f};
        m_LightData.Sun.Intensity    = 1.0f;
        m_LightData.Sun.Color        = Shaders::PackUnorm4x8(glm::vec4(1.0f));
        m_LightData.PointLightCount  = MAX_POINT_LIGHT_COUNT;
        constexpr f32 radius         = 2.5f;
        constexpr f32 intensity      = 1.2f;
        for (auto& pl : m_LightData.PointLights)
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

        static bool bHotReloadQueued{false};
        if (bHotReloadQueued && mainWindow->IsKeyReleased(GLFW_KEY_V))  // Check state frame before and current
        {
            // m_DepthPrePassPipeline->HotReload();
            m_PBRPipeline->HotReload();
            m_FinalPassPipeline->HotReload();

            m_LightClustersBuildPipeline->HotReload();
            m_LightClustersDetectActivePipeline->HotReload();
            m_LightClustersAssignmentPipeline->HotReload();
            // m_SSSPipeline->HotReload();
            // m_SSAOPipeline->HotReload();
            // m_SSAOBoxBlurPipeline->HotReload();

            m_BloomDownsamplePipeline->HotReload();
            m_BloomUpsampleBlurPipeline->HotReload();

            m_BloomDownsamplePipelineOptimized->HotReload();
            m_BloomUpsampleBlurPipelineOptimized->HotReload();
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
                    for (auto& pl : m_LightData.PointLights)
                    {
                        pl.sphere.Origin += glm::vec3(0, 3.0f, 0) * Application::Get().GetDeltaTime();
                        if (pl.sphere.Origin.y > s_MaxPointLightPos.y) pl.sphere.Origin.y -= (s_MaxPointLightPos.y - s_MinPointLightPos.y);
                    }
                }

                auto& lightUBO = scheduler.GetBuffer(fpPassData.LightBuffer);
                lightUBO->SetData(&m_LightData, sizeof(m_LightData));
            });

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
                            const auto cornerWS = invViewProj * glm::vec4(x * 2.0f - 1.0f, y * 2.0f - 1.0f, (f32)z, 1.0f);
                            frustumCornersWS.emplace_back(cornerWS / cornerWS.w);
                        }
                    }
                }

                return frustumCornersWS;
            };

            const auto subfrustumProjectionMatrix =
                glm::perspective(glm::radians(m_MainCamera->GetZoom()), m_MainCamera->GetAspectRatio(), zNear, zFar);
            const auto frustumCornersWS = GetFrustumCornersWorldSpaceFunc(subfrustumProjectionMatrix, m_MainCamera->GetViewMatrix());
            glm::vec3 frustumCenter{0.0f};
            for (const auto& frustumCornerWS : frustumCornersWS)
                frustumCenter += frustumCornerWS;

            frustumCenter /= frustumCornersWS.size();

            const auto lightViewMatrix = glm::lookAt(frustumCenter + m_LightData.Sun.Direction, frustumCenter, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::vec3 minPointLVS{std::numeric_limits<f32>::max()};
            glm::vec3 maxPointLVS{std::numeric_limits<f32>::lowest()};
            for (const auto& frustumCornerWS : frustumCornersWS)
            {
                const auto pointLVS = glm::vec3(lightViewMatrix * glm::vec4(frustumCornerWS, 1.0f));
                minPointLVS         = glm::min(minPointLVS, pointLVS);
                maxPointLVS         = glm::max(maxPointLVS, pointLVS);
            }

            // Tune this parameter according to the scene
            constexpr float zMult = 10.0f;
            if (minPointLVS.z < 0)
                minPointLVS.z *= zMult;
            else
                minPointLVS.z /= zMult;

            if (maxPointLVS.z < 0)
                maxPointLVS.z /= zMult;
            else
                maxPointLVS.z *= zMult;

            glm::vec3 leftBottomZnear{minPointLVS};
            glm::vec3 rightTopZfar{maxPointLVS};

            f32 actualSize{0.0f};
            if (s_bKeepCascadeConstantSize)
            {  // keep constant world-size resolution, side length = diagonal of largest face of frustum
                // the other option looks good at high resolutions, but can result in shimmering as you look in different directions and the
                // cascade changes size
                float farFaceDiagonal = glm::length(glm::vec3(frustumCornersWS[7]) - glm::vec3(frustumCornersWS[1]));
                float forwardDiagonal = glm::length(glm::vec3(frustumCornersWS[7]) - glm::vec3(frustumCornersWS[0]));
                actualSize            = std::max(farFaceDiagonal, forwardDiagonal);
            }
            else
            {
                actualSize = glm::max(rightTopZfar.x - leftBottomZnear.x, rightTopZfar.y - leftBottomZnear.y);
            }

            if (s_bKeepCascadeSquared)
            {
                const f32 W = rightTopZfar.x - leftBottomZnear.x, H = rightTopZfar.y - leftBottomZnear.y;
                float diff = actualSize - H;
                if (diff > 0)
                {
                    rightTopZfar.y += diff / 2.0f;
                    leftBottomZnear.y -= diff / 2.0f;
                }
                diff = actualSize - W;
                if (diff > 0)
                {
                    rightTopZfar.x += diff / 2.0f;
                    leftBottomZnear.x -= diff / 2.0f;
                }
            }

            if (s_bRoundCascadeToPixelSize)
            {
                float pixelSize   = actualSize / SHADOW_MAP_CASCADE_SIZE;
                leftBottomZnear.x = glm::ceil(leftBottomZnear.x / pixelSize) * pixelSize;
                rightTopZfar.x    = glm::ceil(rightTopZfar.x / pixelSize) * pixelSize;
                leftBottomZnear.y = glm::ceil(leftBottomZnear.y / pixelSize) * pixelSize;
                rightTopZfar.y    = glm::ceil(rightTopZfar.y / pixelSize) * pixelSize;
            }

#if 0
            glm::vec3 leftBottomZnear{minPointLVS.x, minPointLVS.y, minPointLVS.z};
            glm::vec3 rightTopZfar{maxPointLVS.x, maxPointLVS.y, maxPointLVS.z};

            const f32 maxLeftRightSide = glm::max(glm::abs(minPointLVS.x), glm::abs(maxPointLVS.x));
            const f32 maxBottomTopSide = glm::max(glm::abs(minPointLVS.y), glm::abs(maxPointLVS.y));
            f32 actualSize             = glm::max(maxLeftRightSide, maxBottomTopSide);
            if (s_bKeepCascadeConstantSize)
            {
                float farFaceDiagonal = glm::length(glm::vec3(frustumCornersWS[7]) - glm::vec3(frustumCornersWS[1]));
                float forwardDiagonal = glm::length(glm::vec3(frustumCornersWS[7]) - glm::vec3(frustumCornersWS[0]));
                actualSize            = std::max(farFaceDiagonal, forwardDiagonal);

                /*            glm::vec3 minPointWS{std::numeric_limits<f32>::max()};
                                        glm::vec3 maxPointWS{std::numeric_limits<f32>::lowest()};
                                        for (const auto& frustumCornerWS : frustumCornersWS)
                                        {
                                            minPointWS = glm::min(minPointWS, frustumCornerWS);
                                            maxPointWS = glm::max(maxPointWS, frustumCornerWS);
                                        }
                                        const f32 diagonalLength = glm::distance(minPointWS, maxPointWS);
                                        leftBottomZnear          = minPointWS;
                                        rightTopZfar             = maxPointWS;*/
            }

            if (s_bKeepCascadeSquared)
            {
                leftBottomZnear.x = -actualSize;
                leftBottomZnear.y = -actualSize;
                rightTopZfar.x    = actualSize;
                rightTopZfar.y    = actualSize;
            }

            if (s_bRoundCascadeToPixelSize)
            {
                const f32 csmPixelSize = actualSize / SHADOW_MAP_CASCADE_SIZE;
                leftBottomZnear.x      = glm::round(leftBottomZnear.x / csmPixelSize) * csmPixelSize;
                leftBottomZnear.y      = glm::round(leftBottomZnear.y / csmPixelSize) * csmPixelSize;
                rightTopZfar.x         = glm::round(rightTopZfar.x / csmPixelSize) * csmPixelSize;
                rightTopZfar.y         = glm::round(rightTopZfar.y / csmPixelSize) * csmPixelSize;
            }
#endif

            const auto lightOrthoProjectionMatrix =
                glm::ortho(leftBottomZnear.x, rightTopZfar.x, leftBottomZnear.y, rightTopZfar.y, rightTopZfar.z, leftBottomZnear.z) *
                glm::scale(glm::vec3(1.0f, -1.0f, 1.0f));
            return lightOrthoProjectionMatrix * lightViewMatrix;
        };

        // reversed z
        std::vector<f32> shadowCascadeLevels{m_MainCamera->GetZFar(), m_MainCamera->GetZNear() / 50.0f, m_MainCamera->GetZNear() / 10.0f,
                                             m_MainCamera->GetZNear() / 2.0f, m_MainCamera->GetZNear()};

        Shaders::CascadedShadowMapsData csmShaderData = {};
        for (u32 k{}; k < shadowCascadeLevels.size() - 1; ++k)
        {
            csmShaderData.ViewProjectionMatrix[k] =
                GetLightSpaceViewProjectionMatrixFunc(shadowCascadeLevels[k], shadowCascadeLevels[k + 1]);
        }
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
                    if (!m_LightData.Sun.bCastShadows) return;

                    auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                    pipelineStateCache.Bind(cmd, m_DepthPrePassPipeline.get());

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

                        cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
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

                    cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
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

                cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetDevice()->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
                                                     0, pc);
                cmd.dispatch(glm::ceil(LIGHT_CLUSTERS_SUBDIVISION_X / (f32)LIGHT_CLUSTERS_BUILD_WG_SIZE),
                             glm::ceil(LIGHT_CLUSTERS_SUBDIVISION_Y / (f32)LIGHT_CLUSTERS_BUILD_WG_SIZE),
                             glm::ceil(LIGHT_CLUSTERS_SUBDIVISION_Z / (f32)LIGHT_CLUSTERS_BUILD_WG_SIZE));
            });

#if LIGHT_CLUSTERS_DETECT_ACTIVE
        struct LightClustersDetectActivePassData
        {
            RGResourceID CameraBuffer;
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

                lcdaPassData.CameraBuffer =
                    scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT |
                                                                          EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);

                lcdaPassData.LightClusterBuffer = scheduler.ReadBuffer(ResourceNames::LightClusterBuffer,
                                                                       EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT |
                                                                           EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);

                lcdaPassData.DepthTexture = scheduler.ReadTexture(ResourceNames::GBufferDepth, MipSet::FirstMip(),
                                                                  EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_LightClustersDetectActivePipeline.get());

                struct PushConstantBlock
                {
                    u32 DepthTextureID;
                    const Shaders::CameraData* CameraData;
                    const AABB* Clusters;
                    Shaders::LightClusterActiveList* ActiveLightClusters;
                } pc = {};

                pc.DepthTextureID = scheduler.GetTexture(lcdaPassData.DepthTexture)->GetBindlessTextureID();
                pc.CameraData     = (const Shaders::CameraData*)scheduler.GetBuffer(lcdaPassData.CameraBuffer)->GetBDA();
                pc.Clusters       = (const AABB*)scheduler.GetBuffer(lcdaPassData.LightClusterBuffer)->GetBDA();
                pc.ActiveLightClusters =
                    (Shaders::LightClusterActiveList*)scheduler.GetBuffer(lcdaPassData.LightClusterDetectActiveBuffer)->GetBDA();

                cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetDevice()->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
                                                     0, pc);
                cmd.dispatch(glm::ceil(LIGHT_CLUSTERS_SUBDIVISION_X / (f32)LIGHT_CLUSTERS_DETECT_ACTIVE_WG_SIZE_X),
                             glm::ceil(LIGHT_CLUSTERS_SUBDIVISION_Y / (f32)LIGHT_CLUSTERS_DETECT_ACTIVE_WG_SIZE_Y), 1);
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
                    (m_LightData.PointLightCount + LIGHT_CLUSTERS_MAX_BATCH_LIGHT_COUNT - 1) / LIGHT_CLUSTERS_MAX_BATCH_LIGHT_COUNT;
                for (u64 lightBatchIndex{}; lightBatchIndex < lightBatchCount; ++lightBatchIndex)
                {
                    const u64 pointLightBatchCount =
                        glm::min((u64)LIGHT_CLUSTERS_MAX_BATCH_LIGHT_COUNT,
                                 m_LightData.PointLightCount - LIGHT_CLUSTERS_MAX_BATCH_LIGHT_COUNT * lightBatchIndex);
                    pc.PointLightBatchCount = pointLightBatchCount;

                    cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                         vk::ShaderStageFlagBits::eAll, 0, pc);
                    cmd.dispatch(glm::ceil(LIGHT_CLUSTERS_COUNT / (f32)LIGHT_CLUSTERS_ASSIGNMENT_WG_SIZE), 1, 1);

                    pc.PointLightBatchOffset += pointLightBatchCount;
                }
#else
                cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetDevice()->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
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
        } ssaoPassData = {};

        struct SSAOBoxBlurPassData
        {
            RGResourceID SSAOTexture;
        } ssaoBoxBlurPassData = {};
        if (s_bComputeSSAO)
        {
            m_RenderGraph->AddPass(
                "SSAOPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
                [&](RenderGraphResourceScheduler& scheduler)
                {
                    scheduler.CreateTexture(ResourceNames::SSAOTexture,
                                            GfxTextureDescription(vk::ImageType::e2D,
                                                                  glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                                  vk::Format::eR8Unorm, vk::ImageUsageFlagBits::eColorAttachment));

                    scheduler.WriteRenderTarget(ResourceNames::SSAOTexture, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                                vk::AttachmentStoreOp::eStore, vk::ClearColorValue().setFloat32({1.0f, 1.0f, 1.0f, 1.0f}));
                    ssaoPassData.DepthTexture = scheduler.ReadTexture(ResourceNames::GBufferDepth, MipSet::FirstMip(),
                                                                      EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);
                    ssaoPassData.CameraBuffer = scheduler.ReadBuffer(ResourceNames::CameraBuffer,
                                                                     EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT |
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
                    pipelineStateCache.Bind(cmd, m_SSAOPipeline.get());

                    struct PushConstantBlock
                    {
                        const Shaders::CameraData* CameraData{nullptr};
                        u32 DepthTextureID{0};
                    } pc = {};

                    auto& cameraUBO    = scheduler.GetBuffer(ssaoPassData.CameraBuffer);
                    pc.CameraData      = (const Shaders::CameraData*)cameraUBO->GetBDA();
                    auto& depthTexture = scheduler.GetTexture(ssaoPassData.DepthTexture);
                    pc.DepthTextureID  = depthTexture->GetBindlessTextureID();

                    cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                         vk::ShaderStageFlagBits::eAll, 0, pc);
                    cmd.draw(3, 1, 0, 0);
                });

            m_RenderGraph->AddPass(
                "SSAOBoxBlurPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
                [&](RenderGraphResourceScheduler& scheduler)
                {
                    scheduler.CreateTexture(ResourceNames::SSAOTextureBlurred,
                                            GfxTextureDescription(vk::ImageType::e2D,
                                                                  glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                                  vk::Format::eR8Unorm, vk::ImageUsageFlagBits::eColorAttachment));

                    scheduler.WriteRenderTarget(ResourceNames::SSAOTextureBlurred, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                                vk::AttachmentStoreOp::eStore, vk::ClearColorValue().setFloat32({1.0f, 1.0f, 1.0f, 1.0f}));

                    ssaoBoxBlurPassData.SSAOTexture = scheduler.ReadTexture(
                        ResourceNames::SSAOTexture, MipSet::FirstMip(), EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);

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
                    pipelineStateCache.Bind(cmd, m_SSAOBoxBlurPipeline.get());

                    struct PushConstantBlock
                    {
                        u32 TextureID{0};
                    } pc         = {};
                    pc.TextureID = scheduler.GetTexture(ssaoBoxBlurPassData.SSAOTexture)->GetBindlessTextureID();

                    cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                         vk::ShaderStageFlagBits::eAll, 0, pc);
                    cmd.draw(3, 1, 0, 0);
                });
        }

        struct MainPassShaderData
        {
            u32 SSAOTextureID;
            u32 SSSTextureID;
            float2 ScaleBias;  // For clustered shading, x - scale, y - bias
            const Shaders::CascadedShadowMapsData* CSMData;
            u32 CSMTextureIDs[SHADOW_MAP_CASCADE_COUNT];
            float PlaneDistances[SHADOW_MAP_CASCADE_COUNT];
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
                                            vk::AttachmentStoreOp::eStore, vk::ClearColorValue().setFloat32({1.0f, 0.5f, 0.0f, 1.0f}));
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

                if (s_bComputeSSAO)
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
                pipelineStateCache.Bind(cmd, m_PBRPipeline.get());

                auto& cameraUBO              = scheduler.GetBuffer(mainPassData.CameraBuffer);
                auto& lightUBO               = scheduler.GetBuffer(mainPassData.LightBuffer);
                auto& lightClusterListBuffer = scheduler.GetBuffer(mainPassData.LightClusterListBuffer);

                auto& mainPassShaderDataBuffer = scheduler.GetBuffer(mainPassData.MainPassShaderDataBuffer);

                MainPassShaderData mpsData = {};
                for (u32 k{1}; k < shadowCascadeLevels.size(); ++k)
                {
                    mpsData.PlaneDistances[k - 1] = shadowCascadeLevels[k];
                }
                for (u32 k{}; k < SHADOW_MAP_CASCADE_COUNT; ++k)
                {
                    mpsData.CSMTextureIDs[k] = scheduler.GetTexture(mainPassData.CSMTextures[k])->GetBindlessTextureID();
                }

                const auto zFar   = m_MainCamera->GetZFar();
                const auto zNear  = m_MainCamera->GetZNear();
                mpsData.ScaleBias = {static_cast<f32>(LIGHT_CLUSTERS_SUBDIVISION_Z) / glm::log2(zFar / zNear),
                                     -static_cast<f32>(LIGHT_CLUSTERS_SUBDIVISION_Z) * glm::log2(zNear) / glm::log2(zFar / zNear)};
                mpsData.CSMData   = (const Shaders::CascadedShadowMapsData*)scheduler.GetBuffer(mainPassData.CSMDataBuffer)->GetBDA();
                if (s_bComputeSSAO)
                {
                    mpsData.SSAOTextureID = scheduler.GetTexture(mainPassData.SSAOTexture)->GetBindlessTextureID();
                }
                //   mpsData.SSSTextureID  = scheduler.GetTexture(mainPassData.SSSTexture)->GetBindlessTextureID();

                mainPassShaderDataBuffer->SetData(&mpsData, sizeof(mpsData));

                for (const auto& ro : m_DrawContext.RenderObjects)
                {
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

                    cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                         vk::ShaderStageFlagBits::eAll, 0, pc);
                    pipelineStateCache.Bind(cmd, ro.IndexBuffer.get(), 0, ro.IndexType);
                    cmd.drawIndexed(ro.IndexCount, 1, ro.FirstIndex, 0, 0);
                }
            });

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
                bloomMipChain[i].Size = {m_ViewportExtent.width, m_ViewportExtent.height};
            else
                bloomMipChain[i].Size = bloomMipChain[i - 1].Size;

            bloomMipChain[i].Size = glm::ceil(bloomMipChain[i].Size / 2.0f);
            bloomMipChain[i].Size = glm::max(bloomMipChain[i].Size, 1.0f);

            const auto currentViewportExtent = vk::Extent2D(bloomMipChain[i].Size.x, bloomMipChain[i].Size.y);
            const std::string passName       = "BloomDownsample" + std::to_string(i);
            const std::string textureName    = "BloomDownsampleTexture";

            if (!s_bBloomComputeBased)
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
                                                     .setAddressModeV(vk::SamplerAddressMode::eClampToEdge),
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
                                                    vk::ClearColorValue().setFloat32({0.0f, 0.0f, 0.0f, 0.0f}));

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
                        pipelineStateCache.Bind(cmd, m_BloomDownsamplePipeline.get());

                        struct PushConstantBlock
                        {
                            u32 SrcTextureID;
                            glm::vec2 SrcTexelSize;  // rcp(SrcTextureResolution)
                            u32 MipLevel;
                        } pc            = {};
                        pc.SrcTextureID = scheduler.GetTexture(bdPassDatas[i].SrcTexture)->GetBindlessTextureID(i);
                        pc.MipLevel     = i;
                        if (i == 0)
                            pc.SrcTexelSize = 1.0f / glm::vec2(m_ViewportExtent.width, m_ViewportExtent.height);
                        else
                            pc.SrcTexelSize = 1.0f / (glm::vec2)bloomMipChain[i].Size;

                        cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                             vk::ShaderStageFlagBits::eAll, 0, pc);
                        cmd.draw(3, 1, 0, 0);
                    });
            }
            else
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
                                                 vk::Format::eB10G11R11UfloatPack32,
                                                 vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage,
                                                 vk::SamplerCreateInfo()
                                                     .setMinFilter(vk::Filter::eLinear)
                                                     .setMagFilter(vk::Filter::eLinear)
                                                     .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                                     .setAddressModeV(vk::SamplerAddressMode::eClampToEdge),
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
                        pipelineStateCache.Bind(cmd, m_BloomDownsamplePipelineOptimized.get());

                        struct PushConstantBlock
                        {
                            u32 SrcTextureID;
                            u32 DstTextureID;
                            u32 MipLevel;
                            glm::vec2 SrcTexelSize;  // rcp(SrcTextureResolution)
                        } pc            = {};
                        pc.SrcTextureID = scheduler.GetTexture(bdPassDatas[i].SrcTexture)->GetBindlessTextureID(i);
                        pc.DstTextureID = scheduler.GetTexture(bdPassDatas[i].DstTexture)->GetBindlessTextureID(i + 1);
                        pc.MipLevel     = i;
                        if (i == 0)
                            pc.SrcTexelSize = 1.0f / glm::vec2(m_ViewportExtent.width, m_ViewportExtent.height);
                        else
                            pc.SrcTexelSize = 1.0f / (glm::vec2)bloomMipChain[i].Size;

#define BLOOM_DOWNSAMPLE_WG_SIZE_X 8
#define BLOOM_DOWNSAMPLE_WG_SIZE_Y 4

                        cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                             vk::ShaderStageFlagBits::eAll, 0, pc);
                        cmd.dispatch(glm::ceil(currentViewportExtent.width / (float)BLOOM_DOWNSAMPLE_WG_SIZE_X),
                                     glm::ceil(currentViewportExtent.height / (float)BLOOM_DOWNSAMPLE_WG_SIZE_Y), 1);

                        // TODO: indirect dispatch??
                    });
            }
        }

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

            if (!s_bBloomComputeBased)
            {
                m_RenderGraph->AddPass(
                    passName, ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
                    [&](RenderGraphResourceScheduler& scheduler)
                    {
                        const std::string prevTextureName =
                            (i == s_BloomMipCount - 1 ? "BloomDownsampleTexture" : "BloomUpsampleBlurTexture" + std::to_string(i));
                        const auto loadOp = i - 1 == 0 ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
                        scheduler.WriteRenderTarget(prevTextureName, MipSet::Explicit(i - 1), loadOp, vk::AttachmentStoreOp::eStore,
                                                    vk::ClearColorValue().setFloat32({0.0f, 0.0f, 0.0f, 0.0f}), textureName);

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
                        pipelineStateCache.Bind(cmd, m_BloomUpsampleBlurPipeline.get());

                        struct PushConstantBlock
                        {
                            u32 SrcTextureID;
                            glm::vec2 SampleFilterRadius;  // NOTE: Make sure aspect ratio is taken into account!
                        } pc                  = {};
                        pc.SrcTextureID       = scheduler.GetTexture(bubPassDatas[i].SrcTexture)->GetBindlessTextureID(i);
                        pc.SampleFilterRadius = glm::vec2(0.003f) * glm::vec2(1, m_MainCamera->GetAspectRatio());

                        cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                             vk::ShaderStageFlagBits::eAll, 0, pc);
                        cmd.draw(3, 1, 0, 0);
                    });
            }
            else
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
                        pipelineStateCache.Bind(cmd, m_BloomUpsampleBlurPipeline.get());

                        struct PushConstantBlock
                        {
                            u32 SrcTextureID;
                            u32 DstTextureID;
                            u32 MipLevel;                  // I need this to prevent loading first mip level, since its unitialized!
                            glm::vec2 SampleFilterRadius;  // NOTE: Make sure aspect ratio is taken into account!
                        } pc                  = {};
                        pc.MipLevel           = i - 1;
                        pc.SrcTextureID       = scheduler.GetTexture(bubPassDatas[i].SrcTexture)->GetBindlessTextureID(i);
                        pc.DstTextureID       = scheduler.GetTexture(bubPassDatas[i].DstTexture)->GetBindlessTextureID(i - 1);
                        pc.SampleFilterRadius = glm::vec2(0.003f) * glm::vec2(1, m_MainCamera->GetAspectRatio());

#define BLOOM_UPSAMPLE_BLUR_WG_SIZE_X 8
#define BLOOM_UPSAMPLE_BLUR_WG_SIZE_Y 4

                        cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                             vk::ShaderStageFlagBits::eAll, 0, pc);
                        cmd.dispatch(glm::ceil(currentViewportExtent.width / (float)BLOOM_UPSAMPLE_BLUR_WG_SIZE_X),
                                     glm::ceil(currentViewportExtent.height / (float)BLOOM_UPSAMPLE_BLUR_WG_SIZE_Y), 1);
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

                cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetDevice()->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
                                                     0, pc);
                cmd.draw(3, 1, 0, 0);
            });

        m_ProfilerWindow.m_GPUGraph.LoadFrameData(m_GfxContext->GetLastFrameGPUProfilerData());
        m_ProfilerWindow.m_CPUGraph.LoadFrameData(m_GfxContext->GetLastFrameCPUProfilerData());

        m_UIRenderer->RenderFrame(
            m_ViewportExtent, m_RenderGraph, ResourceNames::FinalPassTexture,
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
                        ImGui::DragFloat3("Direction", (f32*)&m_LightData.Sun.Direction, 0.01f, -1.0f, 1.0f);

                        ImGui::DragFloat("Intensity", &m_LightData.Sun.Intensity, 0.01f, 0.0f, 5.0f);
                        ImGui::Checkbox("Cast Shadows", &m_LightData.Sun.bCastShadows);

                        glm::vec3 sunColor{Shaders::UnpackUnorm4x8(m_LightData.Sun.Color)};
                        if (ImGui::DragFloat3("Radiance", (f32*)&sunColor, 0.01f, 0.0f, 1.0f))
                            m_LightData.Sun.Color = Shaders::PackUnorm4x8(glm::vec4(sunColor, 1.0f));

                        ImGui::TreePop();
                    }
                }

                ImGui::Separator();
                ImGui::Checkbox("Bloom Use Compute", &s_bBloomComputeBased);
                ImGui::Checkbox("Compute SSAO", &s_bComputeSSAO);
                ImGui::Checkbox("Update Lights", &s_bUpdateLights);

                if (ImGui::TreeNodeEx("Cascaded Shadow Maps", ImGuiTreeNodeFlags_Framed))
                {
                    ImGui::Checkbox("Compute Tight Frustums (SDSM)", &s_bComputeTightFrustums);
                    ImGui::Checkbox("Keep Cascades at Constant Size", &s_bKeepCascadeConstantSize);
                    ImGui::Checkbox("Keep Cascades Squared", &s_bKeepCascadeSquared);
                    ImGui::Checkbox("Round Cascades to Pixel Size", &s_bRoundCascadeToPixelSize);
                    ImGui::DragFloat("Cascade Split Delta", &s_CascadeSplitDelta, 0.01f, 0.0f, 1.0f);
                    ImGui::TreePop();
                }

                ImGui::End();
            });

        m_RenderGraph->Build();
        m_RenderGraph->Execute();

        m_RenderGraphStats = m_RenderGraph->GetStatistics();
    }

}  // namespace Radiant
