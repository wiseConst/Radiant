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

    ForwardRenderer::ForwardRenderer() noexcept
    {
        m_MainCamera = MakeShared<Camera>(70.0f, static_cast<float>(m_ViewportExtent.width) / static_cast<float>(m_ViewportExtent.height));

        m_CameraSSBO = MakeUnique<GfxBuffer>(m_GfxContext->GetDevice(),
                                             GfxBufferDescription{.Capacity{sizeof(CameraData)},
                                                                  .ElementSize{sizeof(CameraData)},
                                                                  .UsageFlags{vk::BufferUsageFlagBits::eUniformBuffer},
                                                                  .ExtraFlags{EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED |
                                                                              EExtraBufferFlag::EXTRA_BUFFER_FLAG_ADDRESSABLE}});

        m_Scene = MakeUnique<Scene>("ForwardRendererTest");

        {
            auto pbrShader = MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/PBR.slang"});
            GfxGraphicsPipelineOptions gpo = {
                .RenderingFormats{vk::Format::eR8G8B8A8Unorm, vk::Format::eD16Unorm},
                .DynamicStates{vk::DynamicState::eCullMode, vk::DynamicState::ePrimitiveTopology /*, vk::DynamicState::eDepthWriteEnable*/},
                .CullMode{vk::CullModeFlagBits::eBack},
                .FrontFace{vk::FrontFace::eCounterClockwise},
                .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                .PolygonMode{vk::PolygonMode::eFill},
                .bDepthTest{true},
                .bDepthWrite{true},
                .DepthCompareOp{vk::CompareOp::eLessOrEqual}};
            GfxPipelineDescription pipelineDesc = {.DebugName = "PBR", .PipelineOptions = gpo, .Shader = pbrShader};
            m_PBRPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }

        //  m_Scene->LoadMesh(m_GfxContext, "../Assets/Models/sponza/scene.gltf");
        m_Scene->LoadMesh(m_GfxContext, "../Assets/Models/damaged_helmet/DamagedHelmet.gltf");
        m_Scene->IterateObjects(m_DrawContext);
    }

    bool ForwardRenderer::BeginFrame() noexcept
    {
        m_RenderGraphResourcePool->Tick();
        m_RenderGraph = MakeUnique<RenderGraph>(m_GfxContext, s_ENGINE_NAME, m_RenderGraphResourcePool);
        //  m_DrawContext = {};

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
        }

        m_CameraSSBO->SetData(&m_MainCamera->GetShaderData(), sizeof(CameraData));

        std::sort(std::execution::par, m_DrawContext.RenderObjects.begin(), m_DrawContext.RenderObjects.end(),
                  [&](const RenderObject& lhs, const RenderObject& rhs)
                  {
                      const float lhsDistToCam = glm::length(m_MainCamera->GetShaderData().Position - glm::vec3(lhs.TRS[3]));
                      const float rhsDistToCam = glm::length(m_MainCamera->GetShaderData().Position - glm::vec3(rhs.TRS[3]));

                      return lhs.AlphaMode < rhs.AlphaMode;  // ? lhsDistToCam<rhsDistToCam : lhsDistToCam> rhsDistToCam;
                  });

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
                    scheduler.WriteDepthStencil(ResourceNames::GBufferDepth, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                                vk::ClearDepthStencilValue().setDepth(1.0f));

                scheduler.CreateTexture(
                    ResourceNames::GBufferAlbedo,
                    GfxTextureDescription{.Type       = vk::ImageType::e2D,
                                          .Dimensions = glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                          .Format{vk::Format::eR8G8B8A8Unorm},
                                          .UsageFlags = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc});
                mainPassData.AlbedoTexture =
                    scheduler.WriteRenderTarget(ResourceNames::GBufferAlbedo, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                                                vk::ClearColorValue().setFloat32({1.0f, 0.5f, 0.0f, 1.0f}));

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
                    vk::Rect2D().setExtent(m_ViewportExtent));
            },
            [&](RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_PBRPipeline);
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

                    pc.ModelMatrix   = ro.TRS * glm::rotate(glm::radians(-90.0f), glm::vec3(1, 0, 0)) * glm::scale(glm::vec3(0.1f));
                    pc.CameraData    = (const CameraData*)m_CameraSSBO->GetBDA();
                    pc.VtxPositions  = (const VertexPosition*)ro.VertexPositionBuffer->GetBDA();
                    pc.VtxAttributes = (const VertexAttribute*)ro.VertexAttributeBuffer->GetBDA();
                    pc.MaterialData  = (const Shaders::GLTFMaterial*)ro.MaterialBuffer->GetBDA();

                    // cmd.setDepthWriteEnable(ro.AlphaMode == EAlphaMode::ALPHA_MODE_OPAQUE);
                    cmd.setCullMode(ro.CullMode);
                    cmd.setPrimitiveTopology(ro.PrimitiveTopology);
                    cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0, pc);

                    cmd.bindIndexBuffer(*ro.IndexBuffer, 0, vk::IndexType::eUint32);
                    cmd.drawIndexed(ro.IndexCount, 1, ro.FirstIndex, 0, 0);
                }
            });

        m_UIRenderer->RenderFrame(m_ViewportExtent, m_RenderGraph, ResourceNames::GBufferAlbedo, []() { ImGui::ShowDemoWindow(); });

        m_RenderGraph->Execute();
    }

    void ForwardRenderer::EndFrame() noexcept
    {
        m_GfxContext->EndFrame();
    }

}  // namespace Radiant
