#include <pch.h>
#include "ForwardRenderer.hpp"

#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

namespace Radiant
{
    namespace ResourceNames
    {
        const std::string GBufferAlbedo{"Resource_GBuffer_Albedo"};
        const std::string GBufferDepth{"Resource_GBuffer_Depth"};
        const std::string SSSTexture{"Resource_ScreenSpaceShadows"};
        const std::string CameraBuffer{"Resource_Camera_Buffer"};

    }  // namespace ResourceNames

    ForwardRenderer::ForwardRenderer() noexcept
    {
        // NOTE: Reversed-Z
        m_MainCamera = MakeShared<Camera>(70.0f, static_cast<float>(m_ViewportExtent.width) / static_cast<float>(m_ViewportExtent.height),
                                          1000.0f, 0.0001f);
        m_Scene      = MakeUnique<Scene>("ForwardRendererTest");

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
                .RenderingFormats{vk::Format::eR8G8B8A8Unorm, vk::Format::eD32Sfloat},
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
                MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/SSAO.slang"});

            GfxPipelineDescription pipelineDesc = {
                .DebugName = "SSAO", .PipelineOptions = GfxComputePipelineOptions{}, .Shader = ssaoShader};
            m_SSAOPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        m_Scene->LoadMesh(m_GfxContext, "../Assets/Models/sponza/scene.gltf");
        m_Scene->IterateObjects(m_DrawContext);
    }

    bool ForwardRenderer::BeginFrame() noexcept
    {
        m_RenderGraphResourcePool->Tick();
        m_RenderGraph = MakeUnique<RenderGraph>(m_GfxContext, s_ENGINE_NAME, m_RenderGraphResourcePool);

        const auto bImageAcquired = m_GfxContext->BeginFrame();
        m_ViewportExtent          = m_GfxContext->GetSwapchainExtent();  // Update extents after swapchain been recreated if needed.

        return bImageAcquired;
    }

    void ForwardRenderer::RenderFrame() noexcept
    {
        auto& mainWindow = Application::Get().GetMainWindow();

        // Testing shaders hot reload and deferred deletion queue.
        if (mainWindow->IsKeyPressed(GLFW_KEY_V))
        {
            m_PBRPipeline->HotReload();
            m_DepthPrePassPipeline->HotReload();
            m_SSSPipeline->HotReload();
            m_SSAOPipeline->HotReload();
        }

        // Sort transparent objects back to front.
        std::sort(std::execution::par, m_DrawContext.RenderObjects.begin(), m_DrawContext.RenderObjects.end(),
                  [&](const RenderObject& lhs, const RenderObject& rhs)
                  {
                      //      const float lhsDistToCam = glm::length(m_MainCamera->GetShaderData().Position - glm::vec3(lhs.TRS[3]));
                      //     const float rhsDistToCam = glm::length(m_MainCamera->GetShaderData().Position - glm::vec3(rhs.TRS[3]));

                      //         if (lhs.AlphaMode == rhs.AlphaMode  && lhs.AlphaMode == EAlphaMode::ALPHA_MODE_MASK)

                      if (lhs.AlphaMode == rhs.AlphaMode) return lhs.IndexBuffer < rhs.IndexBuffer;

                      return lhs.AlphaMode < rhs.AlphaMode;  // ? lhsDistToCam < rhsDistToCam : lhsDistToCam > rhsDistToCam;
                  });

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
                                          .UsageFlags = vk::ImageUsageFlagBits::eDepthStencilAttachment});

                scheduler.WriteDepthStencil(ResourceNames::GBufferDepth, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                            vk::ClearDepthStencilValue().setDepth(0.0f));

                scheduler.CreateBuffer(ResourceNames::CameraBuffer,
                                       GfxBufferDescription{.Capacity{sizeof(CameraData)},
                                                            .ElementSize{sizeof(CameraData)},
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
            [&](RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_DepthPrePassPipeline.get());

                auto& cameraUBO = scheduler.GetBuffer(depthPrePassData.CameraBuffer);
                cameraUBO->SetData(&m_MainCamera->GetShaderData(), sizeof(CameraData));

                for (const auto& ro : m_DrawContext.RenderObjects)
                {
                    // DepthPrePass works only for opaque objects.
                    if (ro.AlphaMode != EAlphaMode::ALPHA_MODE_OPAQUE) continue;

                    struct PushConstantBlock
                    {
                        glm::mat4 ModelMatrix{1.f};
                        const CameraData* CameraData{nullptr};
                        const VertexPosition* VtxPositions{nullptr};
                    } pc = {};

                    pc.ModelMatrix = ro.TRS;
                    // cerberus:
                    // *glm::translate(glm::vec3(-ro.TRS[3])) * glm::rotate(glm::radians(-90.0f), glm::vec3(1, 0, 0)) *
                    // glm::scale(glm::vec3(0.001f)); damaged helmet:
                    // * glm::rotate(glm::radians(-90.0f), glm::vec3(1, 0, 0)) * glm::scale(glm::vec3(0.1f));
                    pc.CameraData   = (const CameraData*)cameraUBO->GetBDA();
                    pc.VtxPositions = (const VertexPosition*)ro.VertexPositionBuffer->GetBDA();

                    pipelineStateCache.Set(cmd, ro.CullMode);
                    pipelineStateCache.Set(cmd, ro.PrimitiveTopology);

                    cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, pc);
                    pipelineStateCache.Bind(cmd, ro.IndexBuffer.get());
                    cmd.drawIndexed(ro.IndexCount, 1, ro.FirstIndex, 0, 0);
                }
            });

        struct ScreenSpaceShadowsData
        {
            RGResourceID CameraBuffer;
            RGResourceID DepthTexture;
            RGResourceID SSSTexture;
        } sssData = {};
        m_RenderGraph->AddPass(
            "ScreenSpaceShadowsPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_COMPUTE,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateTexture(
                    ResourceNames::SSSTexture,
                    GfxTextureDescription{.Type       = vk::ImageType::e2D,
                                          .Dimensions = glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                          .Format{vk::Format::eR8Unorm},
                                          .UsageFlags = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eColorAttachment});
                sssData.SSSTexture =
                    scheduler.WriteTexture(ResourceNames::SSSTexture, EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE);
                sssData.DepthTexture =
                    scheduler.ReadTexture(ResourceNames::GBufferDepth, EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE);

                sssData.CameraBuffer =
                    scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceState::RESOURCE_STATE_UNIFORM_BUFFER |
                                                                          EResourceState::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE);
            },
            [&](RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_SSSPipeline.get());

                struct PushConstantBlock
                {
                    const CameraData* CameraData{nullptr};
                    std::uint32_t DepthTextureID{0};
                    std::uint32_t SSSTextureID{0};
                } pc = {};

                auto& cameraUBO    = scheduler.GetBuffer(depthPrePassData.CameraBuffer);
                pc.CameraData      = (const CameraData*)cameraUBO->GetBDA();
                auto& sssTexture   = scheduler.GetTexture(sssData.SSSTexture);
                pc.SSSTextureID    = sssTexture->GetBindlessImageID();
                auto& depthTexture = scheduler.GetTexture(sssData.DepthTexture);
                pc.DepthTextureID  = depthTexture->GetBindlessTextureID();

                cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, pc);
                cmd.dispatch(std::ceil<std::uint32_t>(sssTexture->GetDescription().Dimensions.x / 16),
                             std::ceil<std::uint32_t>(sssTexture->GetDescription().Dimensions.y / 16), 1);
            });

        struct MainPassData
        {
            RGResourceID DepthTexture;
            RGResourceID AlbedoTexture;
            RGResourceID CameraBuffer;
            RGResourceID SSSTexture;
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
                                          .Format{vk::Format::eR8G8B8A8Unorm},
                                          .UsageFlags = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc});
                mainPassData.AlbedoTexture =
                    scheduler.WriteRenderTarget(ResourceNames::GBufferAlbedo, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                                vk::ClearColorValue().setFloat32({1.0f, 0.5f, 0.0f, 1.0f}));
                mainPassData.CameraBuffer =
                    scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceState::RESOURCE_STATE_UNIFORM_BUFFER |
                                                                          EResourceState::RESOURCE_STATE_VERTEX_SHADER_RESOURCE |
                                                                          EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE);

                sssData.SSSTexture =
                    scheduler.ReadTexture(ResourceNames::SSSTexture, EResourceState::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE);

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
                    vk::Rect2D().setExtent(m_ViewportExtent));
            },
            [&](RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_PBRPipeline.get());

                auto& cameraUBO = scheduler.GetBuffer(mainPassData.CameraBuffer);
                for (const auto& ro : m_DrawContext.RenderObjects)
                {
                    struct PushConstantBlock
                    {
                        glm::mat4 ModelMatrix{1.f};
                        const CameraData* CameraData{nullptr};
                        const VertexPosition* VtxPositions{nullptr};
                        const VertexAttribute* VtxAttributes{nullptr};
                        const Shaders::GLTFMaterial* MaterialData{nullptr};
                    } pc = {};

                    pc.ModelMatrix = ro.TRS;
                    // cerberus:
                    // *glm::translate(glm::vec3(-ro.TRS[3])) * glm::rotate(glm::radians(-90.0f), glm::vec3(1, 0, 0)) *
                    // glm::scale(glm::vec3(0.001f)); damaged helmet:
                    // * glm::rotate(glm::radians(-90.0f), glm::vec3(1, 0, 0)) * glm::scale(glm::vec3(0.1f));
                    pc.CameraData    = (const CameraData*)cameraUBO->GetBDA();
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

        m_UIRenderer->RenderFrame(
            m_ViewportExtent, m_RenderGraph, ResourceNames::GBufferAlbedo,
            [&]()
            {
                ImGui::ShowDemoWindow();

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
                }
                ImGui::End();
            });

        m_RenderGraph->Build();
        m_RenderGraph->Execute();

        m_RenderGraphStats = m_RenderGraph->GetStatistics();
    }

    void ForwardRenderer::EndFrame() noexcept
    {
        m_GfxContext->EndFrame();
    }

}  // namespace Radiant
