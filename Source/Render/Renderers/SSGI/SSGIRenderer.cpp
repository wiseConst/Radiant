#include <pch.h>
#include "SSGIRenderer.hpp"

#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

namespace Radiant
{
    namespace ResourceNames
    {

        const std::string FinalPassTexture{"Resource_Final_Texture"};

    }  // namespace ResourceNames

    SSGIRenderer::SSGIRenderer() noexcept
    {
        m_MainCamera = MakeShared<Camera>(70.0f, static_cast<f32>(m_ViewportExtent.width) / static_cast<f32>(m_ViewportExtent.height),
                                          0.0001f, 10000.0f);

        {
            auto testShader = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                    GfxShaderDescription{.Path = "../Assets/Shaders/FullScreenClearPass.slang"});

            GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eA2B10G10R10UnormPack32},
                                                   .CullMode{vk::CullModeFlagBits::eNone},
                                                   .FrontFace{vk::FrontFace::eCounterClockwise},
                                                   .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                   .PolygonMode{vk::PolygonMode::eFill}};
            GfxPipelineDescription pipelineDesc = {.DebugName = "FullScreenClearPass", .PipelineOptions = gpo, .Shader = testShader};
            m_FullScreenClearPassPipeline =
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), m_GfxContext->GetBindlessPipelineLayout(), pipelineDesc);
        }
    }

    void SSGIRenderer::RenderFrame() noexcept
    {
        // struct FinalPassData
        //{
        //     RGResourceID MainPassTexture;
        // } finalPassData = {};
        m_RenderGraph->AddPass(
            "FinalPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateTexture(
                    ResourceNames::FinalPassTexture,
                    GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                          vk::Format::eA2B10G10R10UnormPack32,
                                          vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc));
                scheduler.WriteRenderTarget(ResourceNames::FinalPassTexture, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                            vk::AttachmentStoreOp::eStore, vk::ClearColorValue().setFloat32({1.0f, 0.5f, 0.25f, 1.0f}));

                // finalPassData.BloomTexture    = scheduler.ReadTexture("BloomUpsampleBlurTexture0", MipSet::FirstMip(),
                //                                                       EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);
                // finalPassData.MainPassTexture = scheduler.ReadTexture(ResourceNames::GBufferAlbedo, MipSet::FirstMip(),
                //                                                       EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
                    vk::Rect2D().setExtent(m_ViewportExtent));
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_FullScreenClearPassPipeline.get());

                /*struct PushConstantBlock
                {
                    u32 MainPassTextureID;
                    u32 BloomTextureID;
                } pc                 = {};
                pc.MainPassTextureID = scheduler.GetTexture(finalPassData.MainPassTexture)->GetBindlessTextureID();
                pc.BloomTextureID    = scheduler.GetTexture(finalPassData.BloomTexture)->GetBindlessTextureID();*/

                //    cmd.pushConstants<PushConstantBlock>(*m_GfxContext->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll, 0,
                //    pc);
                cmd.draw(3, 1, 0, 0);
            });

        m_ProfilerWindow.m_GPUGraph.LoadFrameData(m_GfxContext->GetLastFrameGPUProfilerData());
        m_ProfilerWindow.m_CPUGraph.LoadFrameData(m_GfxContext->GetLastFrameCPUProfilerData());

        m_UIRenderer->RenderFrame(m_ViewportExtent, m_RenderGraph, ResourceNames::FinalPassTexture,
                                  [&]()
                                  {
                                      static bool bShowDemoWindow = true;
                                      if (bShowDemoWindow) ImGui::ShowDemoWindow(&bShowDemoWindow);

                                      m_ProfilerWindow.Render();

                                      if (ImGui::Begin("Application Info"))
                                      {
                                          const auto& io = ImGui::GetIO();
                                          ImGui::Text("Application average [%.3f] ms/frame (%.1f FPS)", 1000.0f / io.Framerate,
                                                      io.Framerate);

                                          ImGui::Separator();
                                          ImGui::Text("Renderer: %s", m_GfxContext->GetDevice()->GetGPUProperties().deviceName);
                                          ImGui::Separator();
                                      }
                                      ImGui::End();
                                  });

        m_RenderGraph->Build();
        m_RenderGraph->Execute();
    }

}  // namespace Radiant
