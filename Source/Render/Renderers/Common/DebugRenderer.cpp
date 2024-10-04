#include <pch.h>
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
                                               const std::vector<std::string>& textureNames, const std::string& backBufferSrcName) noexcept
    {
        RDNT_ASSERT(!textureNames.empty(), "Texture name array is empty!");

        m_DebugTextureViewsPassData.resize(textureNames.size());
        renderGraph->AddPass(
            "DebugTextureViewPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.WriteRenderTarget(backBufferSrcName, MipSet::FirstMip(), vk::AttachmentLoadOp::eLoad,
                                            vk::AttachmentStoreOp::eStore, {}, ResourceNames::DebugAliasTexture);

                for (u32 i{}; i < m_DebugTextureViewsPassData.size(); ++i)
                {
                    m_DebugTextureViewsPassData[i] = scheduler.ReadTexture(textureNames[i], MipSet::FirstMip(),
                                                                           EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);
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

                for (auto& textureView : m_DebugTextureViewsPassData)
                {
                    struct PushConstantBlock
                    {
                        u32 TextureID;
                        glm::vec4 MinMax;
                    } pc         = {};
                    pc.TextureID = scheduler.GetTexture(textureView)->GetBindlessTextureID();
                    pc.MinMax    = glm::vec4(currMin.x, currMin.y, currMin.x + tileSize.x, currMin.y + tileSize.y);

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
