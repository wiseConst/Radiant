#include <pch.h>
#include "CombinedRenderer.hpp"

#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

#include <light_clusters_defines.hpp>

namespace Radiant
{
    namespace ResourceNames
    {
        const std::string LightBuffer{"Resource_Light_Buffer"};
        const std::string CameraBuffer{"Resource_Camera_Buffer"};

        const std::string GBufferDepth{"Resource_GBuffer_Depth"};

        const std::string GBufferAlbedo{"Resource_GBuffer_Albedo"};

        const std::string FinalPassTexture{"Resource_Final_Texture"};

        const std::string SSSTexture{"Resource_ScreenSpaceShadows"};
        const std::string SSAOTexture{"Resource_SSAO"};
        const std::string SSAOTextureBlurred{"Resource_SSAO_Blurred"};

        const std::string LightClusterBuffer{"Resource_Light_Cluster_Buffer"};  // Light cluster buffer after build stage
        const std::string LightClusterListBuffer{
            "Resource_Light_Cluster_List_Buffer"};  // Light cluster list filled with light indices after cluster assignment stage

    }  // namespace ResourceNames

    CombinedRenderer::CombinedRenderer() noexcept
    {
        m_MainCamera = MakeShared<Camera>(70.0f, static_cast<f32>(m_ViewportExtent.width) / static_cast<f32>(m_ViewportExtent.height),
                                          1000.0f, 0.0001f);
        m_Scene      = MakeUnique<Scene>("CombinedRendererTest");

        LOG_INFO("Light clusters subdivision Z slices: {}", Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS.z);
        for (u32 slice{}; slice < Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS.z; ++slice)
        {
            const auto sd = m_MainCamera->GetShaderData();
            const auto ZSliceNear =
                sd.zNearFar.x * glm::pow(sd.zNearFar.y / sd.zNearFar.x, (f32)slice / (f32)Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS.z);
            const auto ZSliceFar =
                sd.zNearFar.x * glm::pow(sd.zNearFar.y / sd.zNearFar.x, (f32)(slice + 1) / (f32)Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS.z);

            LOG_TRACE("Slice: {}, Froxel dimensions: [{:.4f}, {:.4f}].", slice, ZSliceNear, ZSliceFar);
        }

        {
            auto lightClustersBuildShader = MakeShared<GfxShader>(
                m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/light_clusters_build.slang"});
            const GfxPipelineDescription pipelineDesc = {
                .DebugName = "LightClustersBuild", .PipelineOptions = GfxComputePipelineOptions{}, .Shader = lightClustersBuildShader};
            m_LightClustersBuildPipeline =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            auto lightClustersAssignmentShader = MakeShared<GfxShader>(
                m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/light_clusters_assignment.slang"});
            const GfxPipelineDescription pipelineDesc = {.DebugName       = "LightClustersAssignment",
                                                         .PipelineOptions = GfxComputePipelineOptions{},
                                                         .Shader          = lightClustersAssignmentShader};
            m_LightClustersAssignmentPipeline =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            auto depthPrePassShader =
                MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/DepthPrePass.slang"});
            const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eD32Sfloat},
                                                         .DynamicStates{vk::DynamicState::eCullMode, vk::DynamicState::ePrimitiveTopology},
                                                         .CullMode{vk::CullModeFlagBits::eBack},
                                                         .FrontFace{vk::FrontFace::eCounterClockwise},
                                                         .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                         .PolygonMode{vk::PolygonMode::eFill},
                                                         .bDepthTest{true},
                                                         .bDepthWrite{true},
                                                         .DepthCompareOp{vk::CompareOp::eGreaterOrEqual}};
            const GfxPipelineDescription pipelineDesc = {.DebugName = "DepthPrePass", .PipelineOptions = gpo, .Shader = depthPrePassShader};
            m_DepthPrePassPipeline =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            // NOTE: To not create many pipelines for objects, I switch depth compare op based on AlphaMode of object.
            auto pbrShader = MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/PBR.slang"});
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
            m_PBRPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            auto finalPassShader =
                MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/final.slang"});
            const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eA2B10G10R10UnormPack32},
                                                         .CullMode{vk::CullModeFlagBits::eNone},
                                                         .FrontFace{vk::FrontFace::eCounterClockwise},
                                                         .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                         .PolygonMode{vk::PolygonMode::eFill}};
            const GfxPipelineDescription pipelineDesc = {.DebugName = "FinalPass", .PipelineOptions = gpo, .Shader = finalPassShader};
            m_FinalPassPipeline =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            auto sssShader = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                   GfxShaderDescription{.Path = "../Assets/Shaders/screen_space_shadows.slang"});

            const GfxPipelineDescription pipelineDesc = {
                .DebugName = "SSS", .PipelineOptions = GfxComputePipelineOptions{}, .Shader = sssShader};
            m_SSSPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            auto ssaoShader =
                MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/ssao.slang"});

            const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eR8Unorm},
                                                         .CullMode{vk::CullModeFlagBits::eNone},
                                                         .FrontFace{vk::FrontFace::eCounterClockwise},
                                                         .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                         .PolygonMode{vk::PolygonMode::eFill}};
            const GfxPipelineDescription pipelineDesc = {.DebugName = "SSAO", .PipelineOptions = gpo, .Shader = ssaoShader};
            m_SSAOPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            auto ssaoBoxBlurShader =
                MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/ssao_box_blur.slang"});

            const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eR8Unorm},
                                                         .CullMode{vk::CullModeFlagBits::eNone},
                                                         .FrontFace{vk::FrontFace::eCounterClockwise},
                                                         .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                         .PolygonMode{vk::PolygonMode::eFill}};
            const GfxPipelineDescription pipelineDesc = {.DebugName = "SSAOBoxBlur", .PipelineOptions = gpo, .Shader = ssaoBoxBlurShader};
            m_SSAOBoxBlurPipeline =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        // Default bloom
        {
            auto bloomDownsampleShader = MakeShared<GfxShader>(
                m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/pbr_bloom_downsample.slang"});
            const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eB10G11R11UfloatPack32},
                                                         .CullMode{vk::CullModeFlagBits::eNone},
                                                         .FrontFace{vk::FrontFace::eCounterClockwise},
                                                         .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                         .PolygonMode{vk::PolygonMode::eFill}};
            const GfxPipelineDescription pipelineDesc = {
                .DebugName = "BloomDownsample", .PipelineOptions = gpo, .Shader = bloomDownsampleShader};
            m_BloomDownsamplePipeline =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            auto bloomUpsampleBlurShader = MakeShared<GfxShader>(
                m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/pbr_bloom_upsample_blur.slang"});
            const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eB10G11R11UfloatPack32},
                                                         .CullMode{vk::CullModeFlagBits::eNone},
                                                         .FrontFace{vk::FrontFace::eCounterClockwise},
                                                         .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                         .PolygonMode{vk::PolygonMode::eFill},
                                                         .BlendMode{GfxGraphicsPipelineOptions::EBlendMode::BLEND_MODE_ADDITIVE}};
            const GfxPipelineDescription pipelineDesc = {
                .DebugName = "BloomUpsampleBlur", .PipelineOptions = gpo, .Shader = bloomUpsampleBlurShader};
            m_BloomUpsampleBlurPipeline =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        // Compute optimized bloom
        {
            auto bloomDownsampleShader = MakeShared<GfxShader>(
                m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/bloom_downsample_compute.slang"});
            const GfxPipelineDescription pipelineDesc = {
                .DebugName = "BloomDownsample", .PipelineOptions = GfxComputePipelineOptions{}, .Shader = bloomDownsampleShader};
            m_BloomDownsamplePipelineOptimized =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            auto bloomUpsampleBlurShader = MakeShared<GfxShader>(
                m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/bloom_upsample_blur_compute.slang"});
            const GfxPipelineDescription pipelineDesc = {
                .DebugName = "BloomUpsampleBlur", .PipelineOptions = GfxComputePipelineOptions{}, .Shader = bloomUpsampleBlurShader};
            m_BloomUpsampleBlurPipelineOptimized =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        m_LightData.Sun.Direction   = {0.0f, 0.8f, 0.5f};
        m_LightData.Sun.Intensity   = 1.0f;
        m_LightData.Sun.Color       = Shaders::PackUnorm4x8(glm::vec4(1.0f));
        m_LightData.PointLightCount = MAX_POINT_LIGHT_COUNT;
        constexpr f32 radius        = 2.5f;
        for (auto& pl : m_LightData.PointLights)
        {
            pl.sphere.Origin = glm::linearRand(s_MinPointLightPos, s_MaxPointLightPos);
            pl.sphere.Radius = radius;
            pl.Intensity     = 1.1f;
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
            m_LightClustersAssignmentPipeline->HotReload();
            // m_SSSPipeline->HotReload();
            m_SSAOPipeline->HotReload();
            m_SSAOBoxBlurPipeline->HotReload();

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

        static bool s_bUpdateLights{true};
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
                                                              vk::Format::eD32Sfloat, vk::ImageUsageFlagBits::eDepthStencilAttachment));
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
                        const Shaders::CameraData* CameraData{nullptr};
                        const VertexPosition* VtxPositions{nullptr};
                    } pc = {};

                    glm::quat q{1.0f, 0.0f, 0.0f, 0.0f};
                    glm::vec3 decomposePlaceholder0{1.0f};
                    glm::vec4 decomposePlaceholder1{1.0f};
                    glm::decompose(ro.TRS, pc.scale, q, pc.translation, decomposePlaceholder0, decomposePlaceholder1);
                    pc.scale /= 100.0f;
                    pc.orientation = glm::vec4(q.w, q.x, q.y, q.z);

                    pc.CameraData   = (const Shaders::CameraData*)cameraUBO->GetBDA();
                    pc.VtxPositions = (const VertexPosition*)ro.VertexPositionBuffer->GetBDA();

                    pipelineStateCache.Set(cmd, ro.CullMode);
                    pipelineStateCache.Set(cmd, ro.PrimitiveTopology);

                    cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, pc);
                    pipelineStateCache.Bind(cmd, ro.IndexBuffer.get());
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
                constexpr u64 lcbCapacity = sizeof(AABB) * Shaders::s_LIGHT_CLUSTER_COUNT;
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

                cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, pc);
                cmd.dispatch(glm::ceil(Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS.x / (f32)LIGHT_CLUSTERS_BUILD_WG_SIZE),
                             glm::ceil(Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS.y / (f32)LIGHT_CLUSTERS_BUILD_WG_SIZE),
                             glm::ceil(Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS.z / (f32)LIGHT_CLUSTERS_BUILD_WG_SIZE));
            });

        struct LightClustersAssignmentPassData
        {
            RGResourceID CameraBuffer;
            RGResourceID LightClusterBuffer;
            RGResourceID LightClusterListBuffer;
            RGResourceID LightBuffer;
        } lcaPassData = {};
        m_RenderGraph->AddPass(
            "LightClustersAssignmentPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_COMPUTE,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                constexpr u64 lcaCapacity = sizeof(Shaders::LightClusterList) * Shaders::s_LIGHT_CLUSTER_COUNT;
                scheduler.CreateBuffer(ResourceNames::LightClusterListBuffer,
                                       GfxBufferDescription(lcaCapacity, sizeof(Shaders::LightClusterList),
                                                            vk::BufferUsageFlagBits::eStorageBuffer,
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
                } pc = {};

                auto& cameraUBO              = scheduler.GetBuffer(lcaPassData.CameraBuffer);
                pc.CameraData                = (const Shaders::CameraData*)cameraUBO->GetBDA();
                auto& lightClusterSB         = scheduler.GetBuffer(lcaPassData.LightClusterBuffer);
                pc.Clusters                  = (const AABB*)lightClusterSB->GetBDA();
                auto& lightBuffer            = scheduler.GetBuffer(lcaPassData.LightBuffer);
                pc.LightData                 = (const Shaders::LightData*)lightBuffer->GetBDA();
                auto& lightClusterListBuffer = scheduler.GetBuffer(lcaPassData.LightClusterListBuffer);
                pc.LightClusterList          = (Shaders::LightClusterList*)lightClusterListBuffer->GetBDA();

                cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, pc);

                cmd.dispatch(glm::ceil(Shaders::s_LIGHT_CLUSTER_COUNT / (f32)LIGHT_CLUSTERS_ASSIGNMENT_WG_SIZE), 1, 1);
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

                cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, pc);
                cmd.dispatch(glm::ceil(sssTexture->GetDescription().Dimensions.x / 16.0f),
                             glm::ceil(sssTexture->GetDescription().Dimensions.y / 16.0f), 1);
            });
#endif

        static bool s_bComputeSSAO{true};

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

                    cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, pc);
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

                    cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, pc);
                    cmd.draw(3, 1, 0, 0);
                });
        }

        struct MainPassData
        {
            RGResourceID DepthTexture;
            RGResourceID CameraBuffer;
            RGResourceID LightBuffer;
            RGResourceID LightClusterListBuffer;
            RGResourceID DebugDataBuffer;
            RGResourceID SSSTexture;
            RGResourceID SSAOTexture;
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

                // auto& sssTexture  = scheduler.GetTexture(mainPassData.SSSTexture);
                auto& cameraUBO              = scheduler.GetBuffer(mainPassData.CameraBuffer);
                auto& lightUBO               = scheduler.GetBuffer(mainPassData.LightBuffer);
                auto& lightClusterListBuffer = scheduler.GetBuffer(mainPassData.LightClusterListBuffer);

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
                        u32 SSAOTextureID{0};
                        u32 SSSTextureID{0};
                        float2 ScaleBias{0.0f};
                    } pc = {};

                    if (s_bComputeSSAO)
                    {
                        pc.SSAOTextureID = scheduler.GetTexture(mainPassData.SSAOTexture)->GetBindlessTextureID();
                    }
                    //   pc.SSSTextureID  = sssTexture->GetBindlessTextureID();
                    pc.LightData        = (const Shaders::LightData*)lightUBO->GetBDA();
                    pc.LightClusterList = (const Shaders::LightClusterList*)lightClusterListBuffer->GetBDA();
                    pc.CameraData       = (const Shaders::CameraData*)cameraUBO->GetBDA();
                    const auto zFar     = m_MainCamera->GetZFar();
                    const auto zNear    = m_MainCamera->GetZNear();
                    pc.ScaleBias        = {static_cast<f32>(Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS.z) / glm::log2(zFar / zNear),
                                           -static_cast<f32>(Shaders::s_LIGHT_CLUSTER_SUBDIVISIONS.z) * glm::log2(zNear) /
                                               glm::log2(zFar / zNear)};

                    glm::quat q{1.0f, 0.0f, 0.0f, 0.0f};
                    glm::vec3 decomposePlaceholder0{1.0f};
                    glm::vec4 decomposePlaceholder1{1.0f};
                    glm::decompose(ro.TRS, pc.scale, q, pc.translation, decomposePlaceholder0, decomposePlaceholder1);
                    pc.orientation = glm::packHalf(glm::vec4(q.w, q.x, q.y, q.z) * 0.5f + 0.5f);
                    pc.scale /= 100.0f;
                    pc.orientation = glm::vec4(q.w, q.x, q.y, q.z);

                    pc.VtxPositions  = (const VertexPosition*)ro.VertexPositionBuffer->GetBDA();
                    pc.VtxAttributes = (const VertexAttribute*)ro.VertexAttributeBuffer->GetBDA();
                    pc.MaterialData  = (const Shaders::GLTFMaterial*)ro.MaterialBuffer->GetBDA();

                    const auto currentDepthCompareOp =
                        ro.AlphaMode == EAlphaMode::ALPHA_MODE_OPAQUE ? vk::CompareOp::eEqual : vk::CompareOp::eGreaterOrEqual;
                    pipelineStateCache.Set(cmd, currentDepthCompareOp);
                    pipelineStateCache.Set(cmd, ro.CullMode);
                    pipelineStateCache.Set(cmd, ro.PrimitiveTopology);

                    cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, pc);
                    pipelineStateCache.Bind(cmd, ro.IndexBuffer.get(), 0, vk::IndexType::eUint32);
                    cmd.drawIndexed(ro.IndexCount, 1, ro.FirstIndex, 0, 0);
                }
            });

        static bool s_bBloomComputeBased{false};
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
                                textureName,
                                GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                      vk::Format::eB10G11R11UfloatPack32, vk::ImageUsageFlagBits::eColorAttachment,
                                                      vk::SamplerCreateInfo()
                                                          .setMinFilter(vk::Filter::eLinear)
                                                          .setMagFilter(vk::Filter::eLinear)
                                                          .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                                          .setAddressModeV(vk::SamplerAddressMode::eClampToEdge),
                                                      1, vk::SampleCountFlagBits::e1, /* bExposeMips */ true));

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

                        cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0,
                                                             pc);
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
                                textureName,
                                GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                      vk::Format::eB10G11R11UfloatPack32,
                                                      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage,
                                                      vk::SamplerCreateInfo()
                                                          .setMinFilter(vk::Filter::eLinear)
                                                          .setMagFilter(vk::Filter::eLinear)
                                                          .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                                          .setAddressModeV(vk::SamplerAddressMode::eClampToEdge),
                                                      1, vk::SampleCountFlagBits::e1, /* bExposeMips */ true));

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

                        cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0,
                                                             pc);
                        cmd.dispatch(glm::ceil(currentViewportExtent.width / (float)BLOOM_DOWNSAMPLE_WG_SIZE_X),
                                     glm::ceil(currentViewportExtent.height / (float)BLOOM_DOWNSAMPLE_WG_SIZE_Y), 1);
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

                        cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0,
                                                             pc);
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

                        cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0,
                                                             pc);
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

                cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, pc);
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

                    ImGui::SeparatorText("Sun Parameters");
                    ImGui::DragFloat3("Direction", (f32*)&m_LightData.Sun.Direction, 0.05f, -1.0f, 1.0f);
                    ImGui::DragFloat("Intensity", &m_LightData.Sun.Intensity, 0.05f, 0.0f, 5.0f);

                    glm::vec3 sunColor{Shaders::UnpackUnorm4x8(m_LightData.Sun.Color)};
                    if (ImGui::DragFloat3("Radiance", (f32*)&sunColor, 0.05f, 0.0f, 1.0f))
                        m_LightData.Sun.Color = Shaders::PackUnorm4x8(glm::vec4(sunColor, 1.0f));
                }

                ImGui::Separator();
                ImGui::Checkbox("Bloom Use Compute", &s_bBloomComputeBased);
                ImGui::Checkbox("Compute SSAO", &s_bComputeSSAO);
                ImGui::Checkbox("Update Lights", &s_bUpdateLights);

                ImGui::End();
            });

        m_RenderGraph->Build();
        m_RenderGraph->Execute();

        m_RenderGraphStats = m_RenderGraph->GetStatistics();
    }

}  // namespace Radiant
