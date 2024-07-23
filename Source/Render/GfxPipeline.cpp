#include <pch.h>
#include "GfxPipeline.hpp"

#include <Render/GfxShader.hpp>
#include <Render/GfxTexture.hpp>

namespace Radiant
{
    void GfxPipeline::HotReload() noexcept
    {
        RDNT_ASSERT(m_Description.Shader, "Pipeline hasn't shader attached to it!");

        m_Device->PushObjectToDelete(std::move(m_Handle));
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

            const auto depthStencilStateCI = vk::PipelineDepthStencilStateCreateInfo()
                                                 .setBack(gpo->Back)
                                                 .setFront(gpo->Front)
                                                 .setStencilTestEnable(gpo->bStencilTest)
                                                 .setDepthBoundsTestEnable(gpo->DepthBounds != glm::vec2{0.f})
                                                 .setDepthCompareOp(gpo->DepthCompareOp)
                                                 .setDepthTestEnable(gpo->bDepthTest)
                                                 .setDepthWriteEnable(gpo->bDepthWrite)
                                                 .setMinDepthBounds(gpo->DepthBounds.x)
                                                 .setMaxDepthBounds(gpo->DepthBounds.y);

            const auto inputAssemblyStateCI =
                vk::PipelineInputAssemblyStateCreateInfo().setTopology(gpo->PrimitiveTopology).setPrimitiveRestartEnable(vk::False);
            const auto vtxInputStateCI = vk::PipelineVertexInputStateCreateInfo();

            std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
            for (const auto format : colorAttachmentFormats)
            {
                auto& colorBlendAttachment = colorBlendAttachments.emplace_back(vk::PipelineColorBlendAttachmentState().setColorWriteMask(
                    vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
                    vk::ColorComponentFlagBits::eA));

                switch (gpo->BlendMode)
                {
                    case GfxGraphicsPipelineOptions::EBlendMode::BLEND_MODE_NONE: break;
                    case GfxGraphicsPipelineOptions::EBlendMode::BLEND_MODE_ALPHA:
                    {
                        colorBlendAttachment.setBlendEnable(vk::True)
                            .setColorBlendOp(vk::BlendOp::eAdd)
                            .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
                            .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
                            .setAlphaBlendOp(vk::BlendOp::eAdd)
                            .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
                            .setDstAlphaBlendFactor(vk::BlendFactor::eZero);
                        break;
                    }
                    case GfxGraphicsPipelineOptions::EBlendMode::BLEND_MODE_ADDITIVE:
                    {
                        colorBlendAttachment.setBlendEnable(vk::True)
                            .setColorBlendOp(vk::BlendOp::eAdd)
                            .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
                            .setDstColorBlendFactor(vk::BlendFactor::eOne)
                            .setAlphaBlendOp(vk::BlendOp::eAdd)
                            .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
                            .setDstAlphaBlendFactor(vk::BlendFactor::eZero);
                        break;
                    }
                }
            }

            const auto blendStateCI         = vk::PipelineColorBlendStateCreateInfo().setAttachments(colorBlendAttachments);
            const auto rasterizationStateCI = vk::PipelineRasterizationStateCreateInfo()
                                                  .setCullMode(gpo->CullMode)
                                                  .setFrontFace(gpo->FrontFace)
                                                  .setPolygonMode(gpo->PolygonMode)
                                                  .setRasterizerDiscardEnable(vk::False)
                                                  .setDepthClampEnable(gpo->bDepthClamp)
                                                  .setLineWidth(1.0f);

            // TODO:
            const auto msaaStateCI = vk::PipelineMultisampleStateCreateInfo().setRasterizationSamples(vk::SampleCountFlagBits::e1);

            // NOTE: Unfortunately vulkan.hpp doesn't recognize ankerl's unordered set.
            std::vector<vk::DynamicState> dynamicStates;
            for (const auto& dynamicState : gpo->DynamicStates)
                dynamicStates.emplace_back(dynamicState);
            const auto dynamicStateCI = vk::PipelineDynamicStateCreateInfo().setDynamicStates(dynamicStates);

            const auto shaderStages = m_Description.Shader->GetShaderStages();
            auto [result, pipeline] = m_Device->GetLogicalDevice()->createGraphicsPipelineUnique(
                m_Device->GetPipelineCache(), vk::GraphicsPipelineCreateInfo()
                                                  .setLayout(*m_BindlessPipelineLayout)
                                                  .setStages(shaderStages)
                                                  .setPDepthStencilState(&depthStencilStateCI)
                                                  .setPNext(&dynamicRenderingInfo)
                                                  .setPInputAssemblyState(gpo->bMeshShading ? nullptr : &inputAssemblyStateCI)
                                                  .setPVertexInputState(gpo->bMeshShading ? nullptr : &vtxInputStateCI)
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

    void GfxPipeline::Destroy() noexcept
    {
        m_Device->PushObjectToDelete(std::move(m_Handle));
    }

}  // namespace Radiant
