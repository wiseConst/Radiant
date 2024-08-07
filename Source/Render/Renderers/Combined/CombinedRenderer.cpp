#include <pch.h>
#include "CombinedRenderer.hpp"

#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

namespace Radiant
{
    namespace ResourceNames
    {
        const std::string CameraBuffer{"Resource_Camera_Buffer"};
        const std::string GBufferDepth{"Resource_GBuffer_Depth"};

        const std::string GBufferAlbedo{"Resource_GBuffer_Albedo"};

        const std::string SSSTexture{"Resource_ScreenSpaceShadows"};
        const std::string SSAOTexture{"Resource_SSAO"};
        const std::string SSAOTextureBlurred{"Resource_SSAO_Blurred"};

        const std::string LightClusterBuffer{"Resource_Light_Cluster_Buffer"};
        const std::string LightBuffer{"Resource_Light_Buffer"};

    }  // namespace ResourceNames

    CombinedRenderer::CombinedRenderer() noexcept
    {
        m_MainCamera = MakeShared<Camera>(70.0f, static_cast<f32>(m_ViewportExtent.width) / static_cast<f32>(m_ViewportExtent.height),
                                          1000.0f, 0.0001f);
        m_Scene      = MakeUnique<Scene>("CombinedRendererTest");

        for (uint32_t slice{}; slice < Shaders::s_LIGHT_CLUSTER_SUBDIVISONS.z; ++slice)
        {
            const auto sd = m_MainCamera->GetShaderData();
            const auto ZSlice =
                sd.zNearFar.x * glm::pow(sd.zNearFar.y / sd.zNearFar.x, (f32)slice / (f32)Shaders::s_LIGHT_CLUSTER_SUBDIVISONS.z);
            LOG_TRACE("Slice: {}, ZSlice: {:.4f}", slice, ZSlice);
        }

        {
            auto lightClustersBuildShader = MakeShared<GfxShader>(
                m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/light_clusters_build.slang"});
            GfxPipelineDescription pipelineDesc = {
                .DebugName = "LightClustersBuild", .PipelineOptions = GfxComputePipelineOptions{}, .Shader = lightClustersBuildShader};
            m_LightClustersBuildPipeline =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            auto lightClustersAssignmentShader = MakeShared<GfxShader>(
                m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/light_clusters_assignment.slang"});
            GfxPipelineDescription pipelineDesc = {.DebugName       = "LightClustersAssignment",
                                                   .PipelineOptions = GfxComputePipelineOptions{},
                                                   .Shader          = lightClustersAssignmentShader};
            m_LightClustersAssignmentPipeline =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            auto depthPrePassShader =
                MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/DepthPrePass.slang"});
            GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eD32Sfloat},
                                                   .DynamicStates{vk::DynamicState::eCullMode, vk::DynamicState::ePrimitiveTopology},
                                                   .CullMode{vk::CullModeFlagBits::eBack},
                                                   .FrontFace{vk::FrontFace::eCounterClockwise},
                                                   .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                   .PolygonMode{vk::PolygonMode::eFill},
                                                   .bDepthTest{true},
                                                   .bDepthWrite{true},
                                                   .DepthCompareOp{vk::CompareOp::eGreaterOrEqual}};
            GfxPipelineDescription pipelineDesc = {.DebugName = "DepthPrePass", .PipelineOptions = gpo, .Shader = depthPrePassShader};
            m_DepthPrePassPipeline =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            // NOTE: To not create many pipelines for objects, I switch depth compare op based on AlphaMode of object.
            auto pbrShader = MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/PBR.slang"});
            GfxGraphicsPipelineOptions gpo = {
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
            GfxPipelineDescription pipelineDesc = {.DebugName = "PBR", .PipelineOptions = gpo, .Shader = pbrShader};
            m_PBRPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            auto sssShader = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                   GfxShaderDescription{.Path = "../Assets/Shaders/screen_space_shadows.slang"});

            GfxPipelineDescription pipelineDesc = {.DebugName = "SSS", .PipelineOptions = GfxComputePipelineOptions{}, .Shader = sssShader};
            m_SSSPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            auto ssaoShader =
                MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/ssao.slang"});

            GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eR8Unorm},
                                                   .CullMode{vk::CullModeFlagBits::eNone},
                                                   .FrontFace{vk::FrontFace::eCounterClockwise},
                                                   .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                   .PolygonMode{vk::PolygonMode::eFill}};
            GfxPipelineDescription pipelineDesc = {.DebugName = "SSAO", .PipelineOptions = gpo, .Shader = ssaoShader};
            m_SSAOPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            auto ssaoBoxBlurShader =
                MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/ssao_box_blur.slang"});

            GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eR8Unorm},
                                                   .CullMode{vk::CullModeFlagBits::eNone},
                                                   .FrontFace{vk::FrontFace::eCounterClockwise},
                                                   .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                   .PolygonMode{vk::PolygonMode::eFill}};
            GfxPipelineDescription pipelineDesc = {.DebugName = "SSAOBoxBlur", .PipelineOptions = gpo, .Shader = ssaoBoxBlurShader};
            m_SSAOBoxBlurPipeline =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
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
            m_PBRPipeline->HotReload();
            m_LightClustersBuildPipeline->HotReload();
            m_LightClustersAssignmentPipeline->HotReload();
            m_DepthPrePassPipeline->HotReload();
            m_SSSPipeline->HotReload();
            m_SSAOPipeline->HotReload();
            m_SSAOBoxBlurPipeline->HotReload();
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

#if 0
        struct BuildLightClustersPassData
        {
            RGResourceID CameraBuffer;
            RGResourceID DepthTexture;
        } blcPassData = {};
        m_RenderGraph->AddPass(
            "BuildLightClustersPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_COMPUTE,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                blcPassData.DepthTexture =
                    scheduler.ReadTexture(ResourceNames::GBufferDepth, EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE);
                blcPassData.CameraBuffer =
                    scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceState::RESOURCE_STATE_UNIFORM_BUFFER |
                                                                          EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE);
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_LightClustersBuildPipeline.get());

                struct PushConstantBlock
                {
                    const Shaders::CameraData* CameraData{nullptr};
                    u32 DepthTextureID{0};
                } pc = {};

                auto& cameraUBO    = scheduler.GetBuffer(sssPassData.CameraBuffer);
                pc.CameraData      = (const Shaders::CameraData*)cameraUBO->GetBDA();
                auto& sssTexture   = scheduler.GetTexture(sssPassData.SSSTexture);
                pc.SSSTextureID    = sssTexture->GetBindlessImageID();
                auto& depthTexture = scheduler.GetTexture(sssPassData.DepthTexture);
                pc.DepthTextureID  = depthTexture->GetBindlessTextureID();

                cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, pc);
                cmd.dispatch(glm::ceil(sssTexture->GetDescription().Dimensions.x / 16.0f),
                             glm::ceil(sssTexture->GetDescription().Dimensions.y / 16.0f), 1);
            });
#endif

        struct DepthPrePassData
        {
            RGResourceID CameraBuffer;
        } depthPrePassData = {};
        m_RenderGraph->AddPass(
            "DepthPrePass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateTexture(
                    ResourceNames::GBufferDepth,
                    GfxTextureDescription{.Type       = vk::ImageType::e2D,
                                          .Dimensions = glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                          .Format{vk::Format::eD32Sfloat},
                                          .UsageFlags        = vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                          /*.SamplerCreateInfo = vk::SamplerCreateInfo()
                                                                   .setUnnormalizedCoordinates(vk::False)
                                                                   .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
                                                                   .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
                                                                   .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
                                                                   .setMagFilter(vk::Filter::eLinear)
                                                                   .setMinFilter(vk::Filter::eLinear)
                                                                   .setMipmapMode(vk::SamplerMipmapMode::eLinear)
                                                                   .setMinLod(0.0f)
                                                                   .setMaxLod(1.0f)
                                                                   .setMaxLod(vk::LodClampNone)
                                                                   .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
                                                                   .setAnisotropyEnable(vk::False)*/});

                scheduler.WriteDepthStencil(ResourceNames::GBufferDepth, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                            vk::ClearDepthStencilValue().setDepth(0.0f));

                scheduler.CreateBuffer(ResourceNames::CameraBuffer,
                                       GfxBufferDescription{.Capacity{sizeof(Shaders::CameraData)},
                                                            .ElementSize{sizeof(Shaders::CameraData)},
                                                            .UsageFlags{vk::BufferUsageFlagBits::eUniformBuffer},
                                                            .ExtraFlags{EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED |
                                                                        EExtraBufferFlag::EXTRA_BUFFER_FLAG_ADDRESSABLE}});
                depthPrePassData.CameraBuffer =
                    scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceState::RESOURCE_STATE_UNIFORM_BUFFER |
                                                                          EResourceState::RESOURCE_STATE_VERTEX_SHADER_RESOURCE);

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
                    vk::Rect2D().setExtent(m_ViewportExtent));
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_DepthPrePassPipeline.get());

                auto& cameraUBO = scheduler.GetBuffer(depthPrePassData.CameraBuffer);
                cameraUBO->SetData(&m_MainCamera->GetShaderData(), sizeof(Shaders::CameraData));

                for (const auto& ro : m_DrawContext.RenderObjects)
                {
                    // DepthPrePass works only for opaque objects.
                    if (ro.AlphaMode != EAlphaMode::ALPHA_MODE_OPAQUE) continue;

                    struct PushConstantBlock
                    {
                        glm::mat4 ModelMatrix{1.f};
                        const Shaders::CameraData* CameraData{nullptr};
                        const VertexPosition* VtxPositions{nullptr};
                    } pc = {};

                    pc.ModelMatrix = ro.TRS;
                    // cerberus:
                    // *glm::translate(glm::vec3(-ro.TRS[3])) * glm::rotate(glm::radians(-90.0f), glm::vec3(1, 0, 0)) *
                    // glm::scale(glm::vec3(0.001f)); damaged helmet:
                    // * glm::rotate(glm::radians(-90.0f), glm::vec3(1, 0, 0)) * glm::scale(glm::vec3(0.1f));
                    pc.CameraData   = (const Shaders::CameraData*)cameraUBO->GetBDA();
                    pc.VtxPositions = (const VertexPosition*)ro.VertexPositionBuffer->GetBDA();

                    pipelineStateCache.Set(cmd, ro.CullMode);
                    pipelineStateCache.Set(cmd, ro.PrimitiveTopology);

                    cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, pc);
                    pipelineStateCache.Bind(cmd, ro.IndexBuffer.get());
                    cmd.drawIndexed(ro.IndexCount, 1, ro.FirstIndex, 0, 0);
                }
            });

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
                    scheduler.WriteTexture(ResourceNames::SSSTexture, EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE);
                sssPassData.DepthTexture =
                    scheduler.ReadTexture(ResourceNames::GBufferDepth, EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE);
                sssPassData.CameraBuffer =
                    scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceState::RESOURCE_STATE_UNIFORM_BUFFER |
                                                                          EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE);
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
                pc.SunDirection    = m_SunDirection;

                cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, pc);
                cmd.dispatch(glm::ceil(sssTexture->GetDescription().Dimensions.x / 16.0f),
                             glm::ceil(sssTexture->GetDescription().Dimensions.y / 16.0f), 1);
            });

        struct SSAOPassData
        {
            RGResourceID CameraBuffer;
            RGResourceID DepthTexture;
            RGResourceID SSAOTexture;
        } ssaoPassData = {};
        m_RenderGraph->AddPass(
            "SSAOPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateTexture(
                    ResourceNames::SSAOTexture,
                    GfxTextureDescription{.Type       = vk::ImageType::e2D,
                                          .Dimensions = glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                          .Format{vk::Format::eR8Unorm},
                                          .UsageFlags = vk::ImageUsageFlagBits::eColorAttachment});
                ssaoPassData.SSAOTexture =
                    scheduler.WriteRenderTarget(ResourceNames::SSAOTexture, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                                vk::ClearColorValue().setFloat32({1.0f, 1.0f, 1.0f, 1.0f}));
                ssaoPassData.DepthTexture =
                    scheduler.ReadTexture(ResourceNames::GBufferDepth, EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE);
                ssaoPassData.CameraBuffer =
                    scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceState::RESOURCE_STATE_UNIFORM_BUFFER |
                                                                          EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE);

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
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

        struct SSAOBoxBlurPassData
        {
            RGResourceID SSAOTexture;
        } ssaoBoxBlurPassData = {};
        m_RenderGraph->AddPass(
            "SSAOBoxBlurPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateTexture(
                    ResourceNames::SSAOTextureBlurred,
                    GfxTextureDescription{.Type       = vk::ImageType::e2D,
                                          .Dimensions = glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                          .Format{vk::Format::eR8Unorm},
                                          .UsageFlags = vk::ImageUsageFlagBits::eColorAttachment});

                scheduler.WriteRenderTarget(ResourceNames::SSAOTextureBlurred, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                            vk::ClearColorValue().setFloat32({1.0f, 1.0f, 1.0f, 1.0f}));

                ssaoBoxBlurPassData.SSAOTexture =
                    scheduler.ReadTexture(ResourceNames::SSAOTexture, EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE);

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
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

        struct MainPassData
        {
            RGResourceID DepthTexture;
            RGResourceID AlbedoTexture;
            RGResourceID CameraBuffer;
            RGResourceID DebugDataBuffer;
            RGResourceID SSSTexture;
            RGResourceID SSAOTexture;
        } mainPassData = {};
        m_RenderGraph->AddPass(
            "MainPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                // NOTE: This stage also handles texture resizes since you can specify the dimensions.
                mainPassData.DepthTexture = scheduler.ReadTexture(ResourceNames::GBufferDepth, EResourceState::RESOURCE_STATE_DEPTH_READ);

                scheduler.CreateTexture(
                    ResourceNames::GBufferAlbedo,
                    GfxTextureDescription{.Type       = vk::ImageType::e2D,
                                          .Dimensions = glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                          .Format{vk::Format::eR16G16B16A16Sfloat},
                                          .UsageFlags = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc});
                mainPassData.AlbedoTexture =
                    scheduler.WriteRenderTarget(ResourceNames::GBufferAlbedo, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                                vk::ClearColorValue().setFloat32({1.0f, 0.5f, 0.0f, 1.0f}));
                mainPassData.CameraBuffer =
                    scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceState::RESOURCE_STATE_UNIFORM_BUFFER |
                                                                          EResourceState::RESOURCE_STATE_VERTEX_SHADER_RESOURCE |
                                                                          EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE);

                mainPassData.SSSTexture =
                    scheduler.ReadTexture(ResourceNames::SSSTexture, EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE);

                mainPassData.SSAOTexture =
                    scheduler.ReadTexture(ResourceNames::SSAOTextureBlurred, EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE);

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
                    vk::Rect2D().setExtent(m_ViewportExtent));
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_PBRPipeline.get());

                auto& sssTexture  = scheduler.GetTexture(mainPassData.SSSTexture);
                auto& ssaoTexture = scheduler.GetTexture(mainPassData.SSAOTexture);
                auto& cameraUBO   = scheduler.GetBuffer(mainPassData.CameraBuffer);
                for (const auto& ro : m_DrawContext.RenderObjects)
                {
                    struct PushConstantBlock
                    {
                        glm::mat4 ModelMatrix{1.f};
                        const Shaders::CameraData* CameraData{nullptr};
                        const VertexPosition* VtxPositions{nullptr};
                        const VertexAttribute* VtxAttributes{nullptr};
                        const Shaders::GLTFMaterial* MaterialData{nullptr};
                        u32 SSAOTextureID;
                        u32 SSSTextureID;
                        glm::vec3 SunDirection;
                    } pc = {};

                    pc.ModelMatrix = ro.TRS;
                    // cerberus:
                    // *glm::translate(glm::vec3(-ro.TRS[3])) * glm::rotate(glm::radians(-90.0f), glm::vec3(1, 0, 0)) *
                    // glm::scale(glm::vec3(0.001f)); damaged helmet:
                    // * glm::rotate(glm::radians(-90.0f), glm::vec3(1, 0, 0)) * glm::scale(glm::vec3(0.1f));
                    pc.CameraData    = (const Shaders::CameraData*)cameraUBO->GetBDA();
                    pc.VtxPositions  = (const VertexPosition*)ro.VertexPositionBuffer->GetBDA();
                    pc.VtxAttributes = (const VertexAttribute*)ro.VertexAttributeBuffer->GetBDA();
                    pc.MaterialData  = (const Shaders::GLTFMaterial*)ro.MaterialBuffer->GetBDA();
                    pc.SSAOTextureID = ssaoTexture->GetBindlessTextureID();
                    pc.SSSTextureID  = sssTexture->GetBindlessTextureID();
                    pc.SunDirection  = m_SunDirection;

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

        m_UIRenderer->RenderFrame(
            m_ViewportExtent, m_RenderGraph, ResourceNames::GBufferAlbedo,
            [&]()
            {
                static bool bShowDemoWindow = true;
                if (bShowDemoWindow) ImGui::ShowDemoWindow(&bShowDemoWindow);

                if (ImGui::Begin("Application Info"))
                {
                    const auto& io = ImGui::GetIO();
                    ImGui::Text("Application average [%.3f] ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

                    if (ImGui::TreeNodeEx("RenderGraph Statistics", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Text("Build Time: [%.3f] ms", m_RenderGraphStats.BuildTime);
                        ImGui::Text("Barrier Batch Count: %u", m_RenderGraphStats.BarrierBatchCount);
                        ImGui::Text("Barrier Count: %u", m_RenderGraphStats.BarrierCount);

                        ImGui::TreePop();
                    }

                    ImGui::SliderFloat3("Sun Direction", (f32*)&m_SunDirection, -1.0f, 1.0f);
                }
                ImGui::End();
            });

        m_RenderGraph->Build();
        m_RenderGraph->Execute();

        m_RenderGraphStats = m_RenderGraph->GetStatistics();
    }

}  // namespace Radiant
