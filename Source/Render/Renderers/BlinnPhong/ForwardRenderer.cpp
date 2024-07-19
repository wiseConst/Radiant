#include <pch.h>
#include "ForwardRenderer.hpp"

#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

namespace Radiant
{
    ForwardBlinnPhongRenderer::ForwardBlinnPhongRenderer() noexcept
    {
        m_MainCamera = MakeShared<Camera>(70.0f, static_cast<float>(m_ViewportExtent.width) / static_cast<float>(m_ViewportExtent.height));

        m_Scene = MakeUnique<Scene>("ForwardRendererTest");
        m_Scene->LoadMesh(m_GfxContext, "../Assets/Models/standard/dragon/scene.gltf");

        {
            auto blinnPhongShader =
                MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/blinn_phong.slang"});
            GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eB8G8R8A8Unorm, vk::Format::eD16Unorm},
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

            GfxTextureDescription textureDesc = {.Type       = vk::ImageType::e2D,
                                                 .Dimensions = glm::vec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                 .Format{vk::Format::eD16Unorm},
                                                 .UsageFlags = vk::ImageUsageFlagBits::eDepthStencilAttachment};
            m_DepthTexture                    = MakeUnique<GfxTexture>(m_GfxContext->GetDevice(), textureDesc);
        }
    }

    bool ForwardBlinnPhongRenderer::BeginFrame() noexcept
    {
        m_RenderGraph = MakeUnique<RenderGraph>(s_ENGINE_NAME);

        const auto bImageAcquired = m_GfxContext->BeginFrame();
        m_ViewportExtent          = m_GfxContext->GetSwapchainExtent();  // Update extents after swapchain been recreated if needed.

        m_DepthTexture->Resize(glm::uvec3{m_ViewportExtent.width, m_ViewportExtent.height, 1});
        return bImageAcquired;
    }

    void ForwardBlinnPhongRenderer::RenderFrame() noexcept
    {
        auto& mainWindow = Application::Get().GetMainWindow();

        // Testing shaders hot reload and deferred deletion queue.
        if (mainWindow->IsKeyPressed(GLFW_KEY_V))
        {
            m_BlinnPhongPipeline->HotReload();
        }

        auto& frameData = m_GfxContext->GetCurrentFrameData();

        frameData.CommandBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

        const auto colorImageSubresourceRange = vk::ImageSubresourceRange()
                                                    .setBaseArrayLayer(0)
                                                    .setLayerCount(1)
                                                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                    .setBaseMipLevel(0)
                                                    .setLevelCount(1);

        std::vector<vk::ImageMemoryBarrier2> imageMemoryBarriers;
        imageMemoryBarriers.emplace_back(vk::ImageMemoryBarrier2()
                                             .setImage(m_GfxContext->GetCurrentSwapchainImage())
                                             .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                                             .setSrcStageMask(vk::PipelineStageFlagBits2::eBottomOfPipe)
                                             .setSubresourceRange(colorImageSubresourceRange)
                                             .setOldLayout(vk::ImageLayout::eUndefined)
                                             .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                             .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                                             .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput));

        const auto depthImageSubresourceRange = vk::ImageSubresourceRange()
                                                    .setBaseArrayLayer(0)
                                                    .setLayerCount(1)
                                                    .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                                                    .setBaseMipLevel(0)
                                                    .setLevelCount(1);

        imageMemoryBarriers.emplace_back(
            vk::ImageMemoryBarrier2()
                .setImage(*m_DepthTexture)
                .setSrcAccessMask(vk::AccessFlagBits2::eDepthStencilAttachmentWrite)
                .setSrcStageMask(vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests)
                .setSubresourceRange(depthImageSubresourceRange)
                .setOldLayout(vk::ImageLayout::eUndefined)
                .setNewLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
                .setDstAccessMask(vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eDepthStencilAttachmentRead)
                .setDstStageMask(vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests));

        frameData.CommandBuffer.pipelineBarrier2(
            vk::DependencyInfo().setDependencyFlags(vk::DependencyFlagBits::eByRegion).setImageMemoryBarriers(imageMemoryBarriers));

        const auto swapchainAttachmentInfo =
            vk::RenderingAttachmentInfo()
                .setLoadOp(vk::AttachmentLoadOp::eClear)
                .setStoreOp(vk::AttachmentStoreOp::eStore)
                .setImageView(*m_GfxContext->GetCurrentSwapchainImageView())
                .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setClearValue(vk::ClearValue().setColor(vk::ClearColorValue().setFloat32({0.1f, 0.1f, 0.1f, 1.0f})));

        const auto depthAttachmentInfo = m_DepthTexture->GetRenderingAttachmentInfo(
            vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ClearValue().setDepthStencil(vk::ClearDepthStencilValue().setDepth(1.0f)),
            vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore);
        frameData.CommandBuffer.beginRendering(vk::RenderingInfo()
                                                   .setColorAttachments(swapchainAttachmentInfo)
                                                   .setLayerCount(1)
                                                   .setPDepthAttachment(&depthAttachmentInfo)
                                                   .setRenderArea(vk::Rect2D().setExtent(m_ViewportExtent)));
        frameData.CommandBuffer.setViewport(
            0, vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height));
        frameData.CommandBuffer.setScissor(0, vk::Rect2D().setExtent(m_ViewportExtent));

        frameData.CommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_BlinnPhongPipeline);
        m_Scene->IterateObjects(
            [&](const Unique<GfxBuffer>& vertexPosBuffer, const Unique<GfxBuffer>& vertexAttribBuffer, const Unique<GfxBuffer>& indexBuffer)
            {
                struct PushConstantBlock
                {
                    glm::mat4 ViewProjectionMatrix;
                    //   const CameraData* Camera;
                    const VertexPosition* VtxPositions;
                    const VertexAttribute* VtxAttributes;
                } pc;

                //   pc.Camera        = &m_MainCamera->GetShaderData();
                pc.ViewProjectionMatrix = m_MainCamera->GetViewProjectionMatrix();
                pc.VtxPositions         = (const VertexPosition*)vertexPosBuffer->GetBDA();
                pc.VtxAttributes        = (const VertexAttribute*)vertexAttribBuffer->GetBDA();

                frameData.CommandBuffer.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(),
                                                                         vk::ShaderStageFlagBits::eAll, 0, pc);

                frameData.CommandBuffer.bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint32);
                frameData.CommandBuffer.drawIndexed(indexBuffer->GetElementCount(), 1, 0, 0, 0);
            });

        frameData.CommandBuffer.endRendering();

        imageMemoryBarriers.clear();
        imageMemoryBarriers.emplace_back(vk::ImageMemoryBarrier2()
                                             .setImage(m_GfxContext->GetCurrentSwapchainImage())
                                             .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                                             .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                                             .setSubresourceRange(colorImageSubresourceRange)
                                             .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                             .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
                                             .setDstAccessMask(vk::AccessFlagBits2::eNone)
                                             .setDstStageMask(vk::PipelineStageFlagBits2::eNone));

        frameData.CommandBuffer.pipelineBarrier2(
            vk::DependencyInfo().setDependencyFlags(vk::DependencyFlagBits::eByRegion).setImageMemoryBarriers(imageMemoryBarriers));
        frameData.CommandBuffer.end();
    }

    void ForwardBlinnPhongRenderer::EndFrame() noexcept
    {
        m_GfxContext->EndFrame();
    }

}  // namespace Radiant
