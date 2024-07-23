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

    }  // namespace ResourceNames

    ForwardBlinnPhongRenderer::ForwardBlinnPhongRenderer() noexcept
    {
        m_MainCamera = MakeShared<Camera>(70.0f, static_cast<float>(m_ViewportExtent.width) / static_cast<float>(m_ViewportExtent.height));

        m_Scene = MakeUnique<Scene>("ForwardRendererTest");
        m_Scene->LoadMesh(m_GfxContext, "../Assets/Models/standard/dragon/scene.gltf");

        {
            auto blinnPhongShader =
                MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/blinn_phong.slang"});
            GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eR8G8B8A8Unorm, vk::Format::eD16Unorm},
                                                   .CullMode{vk::CullModeFlagBits::eBack},
                                                   .FrontFace{vk::FrontFace::eCounterClockwise},
                                                   .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                   .PolygonMode{vk::PolygonMode::eFill},
                                                   .bDepthTest{true},
                                                   .bDepthWrite{true},
                                                   .DepthCompareOp{vk::CompareOp::eLessOrEqual}};
            GfxPipelineDescription pipelineDesc = {.DebugName = "BlinnPhongPipeline", .PipelineOptions = gpo, .Shader = blinnPhongShader};
            m_BlinnPhongPipeline =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        {
            auto fullScreenQuadShader =
                MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/FullScreenQuad.slang"});
            GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eR8G8B8A8Unorm, vk::Format::eD16Unorm},
                                                   .CullMode{vk::CullModeFlagBits::eFront},
                                                   .FrontFace{vk::FrontFace::eCounterClockwise},
                                                   .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                   .PolygonMode{vk::PolygonMode::eFill},
                                                   .bDepthTest{true},
                                                   .bDepthWrite{true},
                                                   .DepthCompareOp{vk::CompareOp::eLessOrEqual}};
            GfxPipelineDescription pipelineDesc = {.DebugName = "FullScreenQuad", .PipelineOptions = gpo, .Shader = fullScreenQuadShader};
            m_FullScreenQuadPipeline =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }
    }

    bool ForwardBlinnPhongRenderer::BeginFrame() noexcept
    {
        m_RenderGraphResourcePool->Tick();
        m_RenderGraph = MakeUnique<RenderGraph>(m_GfxContext, s_ENGINE_NAME, m_RenderGraphResourcePool);

        const auto bImageAcquired = m_GfxContext->BeginFrame();
        m_ViewportExtent          = m_GfxContext->GetSwapchainExtent();  // Update extents after swapchain been recreated if needed.

        return bImageAcquired;
    }

    void ForwardBlinnPhongRenderer::RenderFrame() noexcept
    {
        auto& mainWindow = Application::Get().GetMainWindow();

        // Testing shaders hot reload and deferred deletion queue.
        if (mainWindow->IsKeyPressed(GLFW_KEY_V))
        {
            m_BlinnPhongPipeline->HotReload();
            m_FullScreenQuadPipeline->HotReload();
        }

        struct MainPassData
        {
            RGResourceID DepthTexture;
            RGResourceID AlbedoTexture;
            RGResourceID CameraBuffer;
        } mainPassData = {};
        m_RenderGraph->AddPass(
            "MainPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                // NOTE: This stage also handles texture resizes since you can specify the dimensions.

                scheduler.CreateTexture(
                    ResourceNames::GBufferDepth,
                    GfxTextureDescription{.Type       = vk::ImageType::e2D,
                                          .Dimensions = glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                          .Format{vk::Format::eD16Unorm},
                                          .UsageFlags = vk::ImageUsageFlagBits::eDepthStencilAttachment});
                mainPassData.DepthTexture =
                    scheduler.WriteDepthStencil(ResourceNames::GBufferDepth, vk::ClearDepthStencilValue().setDepth(1.0f));

                scheduler.CreateTexture(
                    ResourceNames::GBufferAlbedo,
                    GfxTextureDescription{.Type       = vk::ImageType::e2D,
                                          .Dimensions = glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                          .Format{vk::Format::eR8G8B8A8Unorm},
                                          .UsageFlags = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc});
                mainPassData.AlbedoTexture =
                    scheduler.WriteRenderTarget(ResourceNames::GBufferAlbedo, vk::ClearColorValue().setFloat32({1.0f, 0.5f, 0.0f, 1.0f}));

                // mainPassData.CameraBuffer =
                //     scheduler.NewBuffer("CameraData", GfxBufferDescription{.Capacity    = sizeof(CameraData),
                //                                                            .ElementSize = sizeof(CameraData),
                //                                                            .UsageFlags  = vk::BufferUsageFlagBits::eStorageBuffer,
                //                                                            .ExtraFlags  = EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED
                //                                                            |
                //                                                                          EExtraBufferFlag::EXTRA_BUFFER_FLAG_ADDRESSABLE});

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
                    vk::Rect2D().setExtent(m_ViewportExtent));
            },
            [&, mainPassData](RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_BlinnPhongPipeline);
                m_Scene->IterateObjects(
                    [&](const Unique<GfxBuffer>& vertexPosBuffer, const Unique<GfxBuffer>& vertexAttribBuffer,
                        const Unique<GfxBuffer>& indexBuffer)
                    {
                        struct PushConstantBlock
                        {
                            glm::mat4 ViewProjectionMatrix;
                            //   const CameraData* Camera;
                            const VertexPosition* VtxPositions;
                            const VertexAttribute* VtxAttributes;
                        } pc;

                        //   pc.Camera        =
                        //   &m_MainCamera->GetShaderData();
                        pc.ViewProjectionMatrix = m_MainCamera->GetViewProjectionMatrix();
                        pc.VtxPositions         = (const VertexPosition*)vertexPosBuffer->GetBDA();
                        pc.VtxAttributes        = (const VertexAttribute*)vertexAttribBuffer->GetBDA();

                        cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0,
                                                             pc);

                        cmd.bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint32);
                        cmd.drawIndexed(indexBuffer->GetElementCount(), 1, 0, 0, 0);
                    });

                //                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_FullScreenQuadPipeline);
                //              cmd.draw(3, 1, 0, 0);
            });

        m_UIRenderer->RenderFrame(m_ViewportExtent, m_RenderGraph, ResourceNames::GBufferAlbedo);

        m_RenderGraph->Execute();
    }

    void ForwardBlinnPhongRenderer::EndFrame() noexcept
    {
        m_GfxContext->EndFrame();
    }

}  // namespace Radiant
