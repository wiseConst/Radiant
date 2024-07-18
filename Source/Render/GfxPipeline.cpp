#include <pch.h>
#include "GfxPipeline.hpp"

#include <Render/GfxShader.hpp>
#include <Render/GfxTexture.hpp>

#include <vulkan/vulkan_format_traits.hpp>

// NOTE: Used only for base viewport construction.
#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

namespace Radiant
{
    void GfxPipeline::HotReload() noexcept
    {
        RDNT_ASSERT(m_Description.Shader, "Pipeline hasn't shader attached to it!");

        m_Device->PushObjectToDelete([oldPipeline = std::move(m_Handle)]() {});
        m_Description.Shader->HotReload();
        Invalidate();
    }

    void GfxPipeline::Invalidate() noexcept
    {
        RDNT_ASSERT(!std::holds_alternative<std::monostate>(m_Description.PipelineOptions), "PipelineOptions aren't setup!");
        RDNT_ASSERT(m_Description.Shader, "Pipeline hasn't shader attached to it!");

        if (const auto* gpo = std::get_if<GfxGraphicsPipelineOptions>(&m_Description.PipelineOptions); gpo)
        {
            RDNT_ASSERT(!gpo->RenderingFormats.empty(), "Graphics Pipeline requires rendering formats!");

            auto dynamicRenderingInfo = vk::PipelineRenderingCreateInfo();
            std::vector<vk::Format> colorAttachmentFormats;
            for (const auto renderingFormat : gpo->RenderingFormats)
            {
                // TODO: Stencil formats
                if (GfxTexture::IsDepthFormat(renderingFormat))
                {
                    RDNT_ASSERT(dynamicRenderingInfo.depthAttachmentFormat == vk::Format::eUndefined,
                                "Depth attachment already initialized?!");
                    dynamicRenderingInfo.setDepthAttachmentFormat(renderingFormat);
                }
                else
                    colorAttachmentFormats.emplace_back(renderingFormat);
            }
            dynamicRenderingInfo.setColorAttachmentFormats(colorAttachmentFormats);

            const auto inputAssemblyStateCI =
                vk::PipelineInputAssemblyStateCreateInfo().setTopology(gpo->PrimitiveTopology).setPrimitiveRestartEnable(vk::False);
            const auto vtxInputStateCI     = vk::PipelineVertexInputStateCreateInfo();
            const auto depthStencilStateCI = vk::PipelineDepthStencilStateCreateInfo()
                                                 .setBack(gpo->Back)
                                                 .setFront(gpo->Front)
                                                 .setStencilTestEnable(gpo->bStencilTest)
                                                 .setDepthBoundsTestEnable(vk::True)
                                                 .setDepthCompareOp(gpo->DepthCompareOp)
                                                 .setDepthTestEnable(gpo->bDepthTest)
                                                 .setDepthWriteEnable(gpo->bDepthWrite)
                                                 .setMaxDepthBounds(1.0f)
                                                 .setMinDepthBounds(0.f);
            const auto colorBlendAttachment =
                vk::PipelineColorBlendAttachmentState().setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                                                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
            const auto blendStateCI         = vk::PipelineColorBlendStateCreateInfo().setAttachments(colorBlendAttachment);
            const auto rasterizationStateCI = vk::PipelineRasterizationStateCreateInfo()
                                                  .setCullMode(gpo->CullMode)
                                                  .setFrontFace(gpo->FrontFace)
                                                  .setPolygonMode(gpo->PolygonMode)
                                                  .setRasterizerDiscardEnable(vk::False)
                                                  .setDepthClampEnable(gpo->bDepthClamp)
                                                  .setLineWidth(1.0f);

            // TODO:
            const auto msaaStateCI = vk::PipelineMultisampleStateCreateInfo().setRasterizationSamples(vk::SampleCountFlagBits::e1);

            const auto& windowExtent = Application::Get().GetMainWindow()->GetDescription().Extent;
            const auto scissor       = vk::Rect2D().setExtent(vk::Extent2D().setWidth(windowExtent.x).setHeight(windowExtent.y));
            const auto viewport      = vk::Viewport()
                                      .setMinDepth(0.0f)
                                      .setMaxDepth(1.0f)
                                      .setWidth(static_cast<float>(windowExtent.x))
                                      .setHeight(static_cast<float>(windowExtent.y));
            const auto viewportStateCI = vk::PipelineViewportStateCreateInfo().setScissors(scissor).setViewports(viewport);

            // NOTE: Unfortunately vulkan.hpp doesn't recognize ankerl's unordered set.
            std::vector<vk::DynamicState> dynamicStates;
            for (const auto& dynamicState : gpo->DynamicStates)
                dynamicStates.emplace_back(dynamicState);

            const auto shaderStages   = m_Description.Shader->GetShaderStages();
            const auto dynamicStateCI = vk::PipelineDynamicStateCreateInfo().setDynamicStates(dynamicStates);
            auto [result, pipeline]   = m_Device->GetLogicalDevice()->createGraphicsPipelineUnique(
                m_Device->GetPipelineCache(), vk::GraphicsPipelineCreateInfo()
                                                  .setLayout(*m_BindlessPipelineLayout)
                                                  .setStages(shaderStages)
                                                  .setPNext(&dynamicRenderingInfo)
                                                  .setPInputAssemblyState(gpo->bMeshShading ? nullptr : &inputAssemblyStateCI)
                                                  .setPVertexInputState(gpo->bMeshShading ? nullptr : &vtxInputStateCI)
                                                  .setPDepthStencilState(&depthStencilStateCI)
                                                  .setPViewportState(&viewportStateCI)
                                                  .setPColorBlendState(&blendStateCI)
                                                  .setPRasterizationState(&rasterizationStateCI)
                                                  .setPMultisampleState(&msaaStateCI)
                                                  .setPDynamicState(&dynamicStateCI));
            RDNT_ASSERT(result == vk::Result::eSuccess, "Failed to create GRAPHICS pipeline!");

            m_Handle = std::move(pipeline);
        }
        else if (const auto* cpo = std::get_if<GfxComputePipelineOptions>(&m_Description.PipelineOptions); cpo)
        {
            auto [result, pipeline] = m_Device->GetLogicalDevice()->createComputePipelineUnique(
                m_Device->GetPipelineCache(), vk::ComputePipelineCreateInfo()
                                                  .setLayout(*m_BindlessPipelineLayout)
                                                  .setStage(m_Description.Shader->GetShaderStages().back()));
            RDNT_ASSERT(result == vk::Result::eSuccess, "Failed to create COMPUTE pipeline!");

            m_Handle = std::move(pipeline);
        }
        else if (const auto* rtpo = std::get_if<GfxRayTracingPipelineOptions>(&m_Description.PipelineOptions); rtpo)
        {
        }
        else
            RDNT_ASSERT(false, "This shouldn't happen! {}", __FUNCTION__);
    }

}  // namespace Radiant
