#include <pch.h>
#include "GfxPipeline.hpp"

#include <Core/Application.hpp>
#include <Render/GfxDevice.hpp>
#include <Render/GfxShader.hpp>
#include <Render/GfxTexture.hpp>

namespace Radiant
{
    void GfxPipeline::HotReload() noexcept
    {
        if (m_bIsHotReloadGoing)
        {
            LOG_INFO("Pipeline [{}] already hot-reloading, wait until it's done before you can hot-reload again!", m_Description.DebugName);
            return;
        }

        RDNT_ASSERT(m_Description.Shader, "Pipeline hasn't shader attached to it!");
        m_bIsHotReloadGoing.store(true);
        m_bCanSwitchHotReloadedDummy.store(false);

        auto brightFuture = Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const auto hotReloadBeginTime = Timer::Now();
                m_Description.Shader->HotReload();
                Invalidate();

                m_bCanSwitchHotReloadedDummy.store(true);
                m_bIsHotReloadGoing.store(false);

                const auto hotReloadTimeDiff =
                    std::chrono::duration<f32, std::chrono::milliseconds::period>(Timer::Now() - hotReloadBeginTime).count();
                std::stringstream ss;
                ss << "Worker[" << std::this_thread::get_id() << "] hot-reloaded pipeline ";
                LOG_INFO("{} [{}] in {:.4f} ms.", ss.str(), m_Description.DebugName, hotReloadTimeDiff);
            });
        (void)brightFuture;
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
            for (u32 i{}; i < gpo->RenderingFormats.size(); ++i)
            {
                const auto format = gpo->RenderingFormats[i];
                // TODO: Stencil formats
                if (GfxTexture::IsDepthFormat(format))
                {
                    RDNT_ASSERT(dynamicRenderingInfo.depthAttachmentFormat == vk::Format::eUndefined,
                                "Depth attachment already initialized?!");
                    dynamicRenderingInfo.setDepthAttachmentFormat(format);
                }
                else
                {
                    colorAttachmentFormats.emplace_back(format);
                }
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

            std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
            for (u32 i{}; i < colorAttachmentFormats.size(); ++i)
            {
                auto& colorBlendAttachment = colorBlendAttachments.emplace_back(vk::PipelineColorBlendAttachmentState().setColorWriteMask(
                    vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
                    vk::ColorComponentFlagBits::eA));
                const auto blendMode =
                    gpo->BlendModes.empty() ? GfxGraphicsPipelineOptions::EBlendMode::BLEND_MODE_NONE : gpo->BlendModes[i];
                switch (blendMode)
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
            const auto blendStateCI = vk::PipelineColorBlendStateCreateInfo().setAttachments(colorBlendAttachments);

            const auto inputAssemblyStateCI =
                vk::PipelineInputAssemblyStateCreateInfo().setTopology(gpo->PrimitiveTopology).setPrimitiveRestartEnable(vk::False);
            const auto vtxInputStateCI = vk::PipelineVertexInputStateCreateInfo();

            const auto rasterizationStateCI = vk::PipelineRasterizationStateCreateInfo()
                                                  .setCullMode(gpo->CullMode)
                                                  .setFrontFace(gpo->FrontFace)
                                                  .setPolygonMode(gpo->PolygonMode)
                                                  .setRasterizerDiscardEnable(vk::False)
                                                  .setDepthClampEnable(gpo->bDepthClamp)
                                                  .setLineWidth(1.0f);

            const auto msaaStateCI =
                vk::PipelineMultisampleStateCreateInfo()
                    .setRasterizationSamples(gpo->bMultisample ? m_Device->GetMSAASamples() : vk::SampleCountFlagBits::e1)
                    .setMinSampleShading(1.0f);

            const auto dynamicStateCI = vk::PipelineDynamicStateCreateInfo().setDynamicStates(gpo->DynamicStates);

            const auto shaderStages = m_Description.Shader->GetShaderStages();
            auto [result, pipeline] = m_Device->GetLogicalDevice()->createGraphicsPipelineUnique(
                m_Device->GetPipelineCache(), vk::GraphicsPipelineCreateInfo()
                                                  .setPNext(&dynamicRenderingInfo)
                                                  .setLayout(m_Device->GetBindlessPipelineLayout())
                                                  .setStages(shaderStages)
                                                  .setPDepthStencilState(&depthStencilStateCI)
                                                  .setPInputAssemblyState(gpo->bMeshShading ? nullptr : &inputAssemblyStateCI)
                                                  .setPVertexInputState(gpo->bMeshShading ? nullptr : &vtxInputStateCI)
                                                  .setPColorBlendState(&blendStateCI)
                                                  .setPRasterizationState(&rasterizationStateCI)
                                                  .setPMultisampleState(&msaaStateCI)
                                                  .setPDynamicState(&dynamicStateCI));
            RDNT_ASSERT(result == vk::Result::eSuccess, "Failed to create GRAPHICS pipeline!");

            m_Dummy = std::move(pipeline);
        }
        else if (const auto* cpo = std::get_if<GfxComputePipelineOptions>(&m_Description.PipelineOptions); cpo)
        {
            auto [result, pipeline] = m_Device->GetLogicalDevice()->createComputePipelineUnique(
                m_Device->GetPipelineCache(), vk::ComputePipelineCreateInfo()
                                                  .setLayout(m_Device->GetBindlessPipelineLayout())
                                                  .setStage(m_Description.Shader->GetShaderStages().back()));
            RDNT_ASSERT(result == vk::Result::eSuccess, "Failed to create COMPUTE pipeline!");

            m_Dummy = std::move(pipeline);
        }
        else if (const auto* rtpo = std::get_if<GfxRayTracingPipelineOptions>(&m_Description.PipelineOptions); rtpo)
        {
        }
        else
            RDNT_ASSERT(false, "This shouldn't happen! {}", __FUNCTION__);

        m_Device->SetDebugName(m_Description.DebugName, *m_Dummy);
        m_Description.Shader->Clear();
    }

    void GfxPipeline::Destroy() noexcept
    {
        m_Device->PushObjectToDelete(std::move(m_Handle));
    }

    GfxPipeline::operator const vk::Pipeline&() const noexcept
    {
        if (m_bCanSwitchHotReloadedDummy)
        {
            if (m_Handle) m_Device->PushObjectToDelete(std::move(m_Handle));
            m_Handle = std::move(m_Dummy);
            m_bCanSwitchHotReloadedDummy.store(false);
        }

        RDNT_ASSERT(m_Handle, "Pipeline handle is invalid!");
        return *m_Handle;
    }

}  // namespace Radiant
