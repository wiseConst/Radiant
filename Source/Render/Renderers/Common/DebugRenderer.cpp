#include "DebugRenderer.hpp"

#include <Render/GfxContext.hpp>
#include <Render/GfxDevice.hpp>
#include <Render/GfxShader.hpp>
#include <Render/RenderGraph.hpp>

namespace Radiant
{
    namespace ResourceNames
    {
        static const std::string DebugAliasTexture{"Resource_Debug_View_Texture_0"};
    }

    void DebugRenderer::Init() noexcept
    {
        auto textureViewShader =
            MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/debug/texture_view.slang"});
        const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eA2B10G10R10UnormPack32},
                                                     //     .CullMode{vk::CullModeFlagBits::eBack},
                                                     .FrontFace{vk::FrontFace::eCounterClockwise},
                                                     .PrimitiveTopology{vk::PrimitiveTopology::eTriangleFan},
                                                     .PolygonMode{vk::PolygonMode::eFill}};
        const GfxPipelineDescription pipelineDesc = {
            .DebugName = "debug_texture_view", .PipelineOptions = gpo, .Shader = textureViewShader};
        m_DebugTextureViewPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
    }

    DebugRenderer::~DebugRenderer() noexcept
    {
        m_GfxContext->GetDevice()->WaitIdle();
    }

    std::string DebugRenderer::DrawTextureView(const vk::Extent2D& viewportExtent, Unique<RenderGraph>& renderGraph,
                                               const std::vector<TextureViewDescription>& textureViewDescriptions,
                                               const std::string& backBufferSrcName) noexcept
    {
        RDNT_ASSERT(!textureViewDescriptions.empty(), "Texture name array is empty!");

        m_DebugTextureViewsPassData.resize(textureViewDescriptions.size());
        renderGraph->AddPass(
            "DebugTextureViewPass", ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.WriteRenderTarget(backBufferSrcName, MipSet::FirstMip(), vk::AttachmentLoadOp::eLoad,
                                            vk::AttachmentStoreOp::eStore, {}, 0, ResourceNames::DebugAliasTexture);

                for (u32 i{}; i < m_DebugTextureViewsPassData.size(); ++i)
                {
                    const auto& textureViewDescription = textureViewDescriptions[i];
                    m_DebugTextureViewsPassData[i]     = scheduler.ReadTexture(
                        textureViewDescription.name, MipSet::Explicit(textureViewDescription.mipIndex),
                        EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT, textureViewDescription.layerIndex);
                }

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(viewportExtent.width).setHeight(viewportExtent.height),
                    vk::Rect2D().setExtent(viewportExtent));
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_DebugTextureViewPipeline.get());

                glm::vec2 tileSize(0.1f, 0.1f);
                glm::vec2 tilePadding(0.015f, 0.015f);

                glm::vec2 currMin = tilePadding;

                for (u32 i{}; i < m_DebugTextureViewsPassData.size(); ++i)
                {
                    const auto& textureView            = m_DebugTextureViewsPassData[i];
                    const auto& textureViewDescription = textureViewDescriptions[i];
                    struct PushConstantBlock
                    {
                        u32 TextureID;
                        u32 LayerIndex;
                        u32 MipIndex;
                        glm::vec4 MinMax;
                    } pc          = {};
                    pc.LayerIndex = textureViewDescription.layerIndex;
                    pc.MipIndex   = textureViewDescription.mipIndex;
                    pc.TextureID  = scheduler.GetTexture(textureView)->GetBindlessTextureID();
                    pc.MinMax     = glm::vec4(currMin.x, currMin.y, currMin.x + tileSize.x, currMin.y + tileSize.y);

                    cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                         vk::ShaderStageFlagBits::eAll, 0, pc);
                    cmd.draw(4, 1, 0, 0);

                    currMin.x += tileSize.x + tilePadding.x;
                    if (currMin.x + tileSize.x > 1.0f)
                    {
                        currMin.x = tilePadding.x;
                        currMin.y += tileSize.y + tilePadding.y;
                    }
                }
            });

        return ResourceNames::DebugAliasTexture;
    }

}  // namespace Radiant
