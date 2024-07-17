#include <pch.h>
#include "ForwardRenderer.hpp"

namespace Radiant
{
    void ForwardBlinnPhongRenderer::Init() noexcept
    {
        m_Scene = MakeUnique<Scene>("ForwardRendererTest");

        m_ViewportExtent = m_GfxContext->GetSwapchainExtent();

        GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eB8G8R8A8Unorm}};
        GfxPipelineDescription pipelineDesc = {.DebugName = "TestTriangle", .PipelineOptions = gpo};
        m_TriPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
    }

    void ForwardBlinnPhongRenderer::Shutdown() noexcept
    {
        m_GfxContext->GetDevice()->WaitIdle();
    }

    bool ForwardBlinnPhongRenderer::BeginFrame() noexcept
    {
        m_RenderGraph = MakeUnique<RenderGraph>(s_ENGINE_NAME);

        const auto bImageAcquired = m_GfxContext->BeginFrame();
        m_ViewportExtent          = m_GfxContext->GetSwapchainExtent();  // Update extents after swapchain been recreated if needed.

        return bImageAcquired;
    }

    void ForwardBlinnPhongRenderer::RenderFrame() noexcept
    {
        auto& frameData = m_GfxContext->GetCurrentFrameData();

        frameData.CommandBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

        const auto imageSubresourceRange = vk::ImageSubresourceRange()
                                               .setBaseArrayLayer(0)
                                               .setLayerCount(1)
                                               .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                               .setBaseMipLevel(0)
                                               .setLevelCount(1);

        frameData.CommandBuffer.pipelineBarrier2(
            vk::DependencyInfo()
                .setDependencyFlags(vk::DependencyFlagBits::eByRegion)
                .setImageMemoryBarriers(vk::ImageMemoryBarrier2()
                                            .setImage(m_GfxContext->GetCurrentSwapchainImage())
                                            .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                                            .setSrcStageMask(vk::PipelineStageFlagBits2::eBottomOfPipe)
                                            .setSubresourceRange(imageSubresourceRange)
                                            .setOldLayout(vk::ImageLayout::eUndefined)
                                            .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                            .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                                            .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)));

        const auto renderingInfo =
            vk::RenderingAttachmentInfo()
                .setLoadOp(vk::AttachmentLoadOp::eClear)
                .setStoreOp(vk::AttachmentStoreOp::eStore)
                .setImageView(*m_GfxContext->GetCurrentSwapchainImageView())
                .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setClearValue(vk::ClearValue().setColor(vk::ClearColorValue().setFloat32({0.1f, 0.1f, 0.1f, 1.0f})));
        frameData.CommandBuffer.beginRendering(vk::RenderingInfo()
                                                   .setColorAttachments(renderingInfo)
                                                   .setLayerCount(1)
                                                   .setRenderArea(vk::Rect2D().setExtent(m_ViewportExtent)));
        frameData.CommandBuffer.setViewport(
            0, vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height));
        frameData.CommandBuffer.setScissor(0, vk::Rect2D().setExtent(m_ViewportExtent));
        frameData.CommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_TriPipeline);

        struct PushConstantBlock
        {
            glm::mat4 ProjectionMatrix;
            glm::mat4 ViewMatrix;
        };
        PushConstantBlock pc{.ProjectionMatrix =
                                 glm::perspective(glm::radians(80.0f), m_ViewportExtent.width / static_cast<float>(m_ViewportExtent.height),
                                                  0.1f, 1000.0f) /**
glm::scale(glm::vec3(1.0f, -1.0f, -1.0f))*/
                             ,
                             .ViewMatrix = glm::translate(glm::vec3(0.f, 0.0f, -1.0f)) * glm::scale(glm::vec3(10.f))};
        frameData.CommandBuffer.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
                                                                 0, pc);

        frameData.CommandBuffer.draw(3, 1, 0, 0);

        frameData.CommandBuffer.endRendering();

        frameData.CommandBuffer.pipelineBarrier2(
            vk::DependencyInfo()
                .setDependencyFlags(vk::DependencyFlagBits::eByRegion)
                .setImageMemoryBarriers(vk::ImageMemoryBarrier2()
                                            .setImage(m_GfxContext->GetCurrentSwapchainImage())
                                            .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                                            .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                                            .setSubresourceRange(imageSubresourceRange)
                                            .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                            .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
                                            .setDstAccessMask(vk::AccessFlagBits2::eNone)
                                            .setDstStageMask(vk::PipelineStageFlagBits2::eNone)));
        frameData.CommandBuffer.end();
    }

    void ForwardBlinnPhongRenderer::EndFrame() noexcept
    {
        m_GfxContext->EndFrame();
    }

}  // namespace Radiant
