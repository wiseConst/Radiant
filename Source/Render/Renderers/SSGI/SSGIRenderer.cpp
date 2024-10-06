#include <pch.hpp>
#include "SSGIRenderer.hpp"

#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

namespace Radiant
{
    namespace ResourceNames
    {

        //  const std::string FinalPassTexture{"Resource_Final_Texture"};
        const std::string AlbedoTexture{"Resource_Albedo_Texture"};
        const std::string DepthTexture{"Resource_Depth_Texture"};
        const std::string Point2DBuffer{"Resource_Point2D_Buffer"};
        const std::string CameraBuffer{"Resource_Camera_Buffer"};

    }  // namespace ResourceNames

    SSGIRenderer::SSGIRenderer() noexcept
    {
        m_MainCamera = MakeShared<Camera>(70.0f, static_cast<f32>(m_ViewportExtent.width) / static_cast<f32>(m_ViewportExtent.height),
                                          0.0001f, 10000.0f);
        {
            auto point2dShader =
                MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/points2d.slang"});

            const GfxGraphicsPipelineOptions gpo      = {.RenderingFormats{vk::Format::eR16G16B16A16Sfloat, vk::Format::eD16Unorm},
                                                         .CullMode{vk::CullModeFlagBits::eNone /*eBack*/},
                                                         .FrontFace{vk::FrontFace::eCounterClockwise},
                                                         .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                         .PolygonMode{vk::PolygonMode::eFill},
                                                         .bDepthTest{true},
                                                         .bDepthWrite{true},
                                                         .DepthCompareOp{vk::CompareOp::eLessOrEqual}};
            const GfxPipelineDescription pipelineDesc = {.DebugName = "Point2DPipeline", .PipelineOptions = gpo, .Shader = point2dShader};
            m_Point2DPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(),  pipelineDesc);
        }

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
                MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(),  pipelineDesc);
        }
    }

    void SSGIRenderer::RenderFrame() noexcept
    {
        auto& mainWindow = Application::Get().GetMainWindow();
        static bool bParticleCreationQueued{false};
        if (bParticleCreationQueued && mainWindow->IsMouseButtonReleased(GLFW_MOUSE_BUTTON_2))
        {
            m_Points.emplace_back(mainWindow->GetCursorPos(), glm::vec3(1.0f), m_PointRadius);
        }
        bParticleCreationQueued = mainWindow->IsMouseButtonPressed(GLFW_MOUSE_BUTTON_2);

        static bool bHotReloadQueued{false};
        if (bHotReloadQueued && mainWindow->IsKeyReleased(GLFW_KEY_V))  // Check state frame before and current
        {
            m_Point2DPipeline->HotReload();
        }
        bHotReloadQueued = mainWindow->IsKeyPressed(GLFW_KEY_V);

        struct MainPassData
        {
            RGResourceID Point2DBuffer;
            RGResourceID CameraBuffer;
        } mainPassData = {};
        m_RenderGraph->AddPass(
            "MainPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateTexture(
                    ResourceNames::AlbedoTexture,
                    GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                          vk::Format::eR16G16B16A16Sfloat,
                                          vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc));
                scheduler.WriteRenderTarget(ResourceNames::AlbedoTexture, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                            vk::AttachmentStoreOp::eStore, vk::ClearColorValue().setFloat32({0.0f, 0.0f, 0.0f, 1.0f}));

                scheduler.CreateTexture(ResourceNames::DepthTexture,
                                        GfxTextureDescription(vk::ImageType::e2D,
                                                              glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                              vk::Format::eD16Unorm, vk::ImageUsageFlagBits::eDepthStencilAttachment));
                scheduler.WriteDepthStencil(ResourceNames::DepthTexture, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                            vk::AttachmentStoreOp::eStore, vk::ClearDepthStencilValue().setDepth(1.0f));

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
                    vk::Rect2D().setExtent(m_ViewportExtent));

                u64 pointBufferSize = sizeof(Point2D) * m_Points.size();
                if (pointBufferSize == 0) pointBufferSize = sizeof(Point2D);
                scheduler.CreateBuffer(ResourceNames::Point2DBuffer,
                                       GfxBufferDescription(pointBufferSize, sizeof(Point2D), vk::BufferUsageFlagBits::eUniformBuffer,
                                                            EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT));
                mainPassData.Point2DBuffer =
                    scheduler.WriteBuffer(ResourceNames::Point2DBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT);

                scheduler.CreateBuffer(ResourceNames::CameraBuffer,
                                       GfxBufferDescription(sizeof(Shaders::CameraData), sizeof(Shaders::CameraData),
                                                            vk::BufferUsageFlagBits::eUniformBuffer,
                                                            EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT));
                mainPassData.CameraBuffer =
                    scheduler.WriteBuffer(ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT);
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_Point2DPipeline.get());

                auto& cameraUBO = scheduler.GetBuffer(mainPassData.CameraBuffer);
                cameraUBO->SetData(&m_MainCamera->GetShaderData(), sizeof(Shaders::CameraData));

                auto& point2dUBO = scheduler.GetBuffer(mainPassData.Point2DBuffer);
                point2dUBO->SetData(m_Points.data(), sizeof(Point2D) * m_Points.size());

                struct PushConstantBlock
                {
                    const Shaders::CameraData* CameraData;
                    const Point2D* Points;
                    glm::uvec2 FullResolution;
                } pc              = {};
                pc.CameraData     = (const Shaders::CameraData*)cameraUBO->GetBDA();
                pc.Points         = (const Point2D*)point2dUBO->GetBDA();
                pc.FullResolution = m_MainCamera->GetShaderData().FullResolution;

                cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
                                                     0, pc);
                cmd.draw(3, m_Points.size(), 0, 0);
            });

#if 0
       struct FinalPassData
       
           RGResourceID MainPassTexture;
       } finalPassData = {};
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

                //    cmd.pushConstants<PushConstantBlock>(* vk::ShaderStageFlagBits::eAll, 0,
                //    pc);
                cmd.draw(3, 1, 0, 0);
            });
#endif

        m_ProfilerWindow.m_GPUGraph.LoadFrameData(m_GfxContext->GetLastFrameGPUProfilerData());
        m_ProfilerWindow.m_CPUGraph.LoadFrameData(m_GfxContext->GetLastFrameCPUProfilerData());

        m_UIRenderer->RenderFrame(m_ViewportExtent, m_RenderGraph, ResourceNames::AlbedoTexture,
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

                                          ImGui::Text("Points: %llu", m_Points.size());
                                          //        ImGui::DragFloat("Point Size", &m_PointSize, 1.0f, 0.1f, 100.0f);
                                          ImGui::DragFloat("Point Radius", &m_PointRadius, 1.0f, 0.1f, 100.0f);
                                      }
                                      ImGui::End();
                                  });

        m_RenderGraph->Build();
        m_RenderGraph->Execute();
    }

}  // namespace Radiant
