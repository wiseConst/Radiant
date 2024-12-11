#include "AlanWake2Renderer.hpp"
#include <aw2/aw2_defines.hpp>

namespace Radiant
{
    namespace ResourceNames
    {
        const std::string CameraBuffer{"Resource_CameraBuffer"};
        const std::string GBufferAlbedo{"Resource_LBuffer"};

        const std::string DepthBuffer{"Resource_DepthBuffer"};

    }  // namespace ResourceNames

    // gpu-driven renderer with lots of culling and tiled light shading as takahiro harada bequeathed

    // Resources used to build this shit:
    // https://www.youtube.com/watch?v=EtX7WnFhxtQ - GPU driven Rendering with Mesh Shaders in Alan Wake 2

    AlanWake2Renderer::AlanWake2Renderer() noexcept
    {
        m_MainCamera = MakeShared<Camera>(70.0f, static_cast<f32>(m_ViewportExtent.width) / static_cast<f32>(m_ViewportExtent.height),
                                          10000.0f, 0.0001f);

        m_MSTriPipeline = MakeUnique<GfxPipeline>(
            m_GfxContext->GetDevice(),
            GfxPipelineDescription{
                .DebugName = "triangle_mesh_shader",
                .PipelineOptions =
                    GfxGraphicsPipelineOptions{
                        .RenderingFormats{vk::Format::eR8G8B8A8Srgb, vk::Format::eD32Sfloat},
                        .CullMode{vk::CullModeFlagBits::eNone},
                        .FrontFace{vk::FrontFace::eCounterClockwise},
                        .PolygonMode{vk::PolygonMode::eFill},
                        .bDepthTest{true},
                        .bDepthWrite{true},
                        .DepthCompareOp{vk::CompareOp::eGreaterOrEqual},
                    },
                .Shader = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                GfxShaderDescription{.Path = "../Assets/Shaders/aw2/triangle_mesh_shader.slang"})});

        // m_ShadeMainPipeline

        m_World.LoadScene(m_GfxContext, "../Assets/Models/sponza/scene.gltf");
        //   Load(m_GfxContext, "../Assets/Models/sponza/scene.gltf");
        //    Load(m_GfxContext, "../Assets/Models/bistro_exterior/scene.gltf");
    }

    void AlanWake2Renderer::RenderFrame() noexcept
    {
#if 0
        struct MainPassData
        {
            RGResourceID CameraBuffer;
        } mainPassData = {};
        m_RenderGraph->AddPass(
            "MainPass", ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateTexture(
                    ResourceNames::GBufferAlbedo,
                    GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                          vk::Format::eR8G8B8A8Srgb,
                                          vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc, std::nullopt, 1,
                                          vk::SampleCountFlagBits::e1, EResourceCreateBits::RESOURCE_CREATE_DONT_TOUCH_SAMPLED_IMAGES_BIT));
                scheduler.WriteRenderTarget(ResourceNames::GBufferAlbedo, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                            vk::AttachmentStoreOp::eStore, vk::ClearColorValue().setFloat32({0.0f, 0.0f, 0.0f, 0.0f}));

                scheduler.CreateTexture(ResourceNames::DepthBuffer,
                                        GfxTextureDescription(vk::ImageType::e2D,
                                                              glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                              vk::Format::eD32Sfloat, vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                                              vk::SamplerCreateInfo()
                                                                  .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
                                                                  .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
                                                                  .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
                                                                  .setMinFilter(vk::Filter::eNearest)
                                                                  .setMagFilter(vk::Filter::eNearest)
                                                                  .setBorderColor(vk::BorderColor::eFloatOpaqueBlack)));
                scheduler.WriteDepthStencil(ResourceNames::DepthBuffer, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                            vk::AttachmentStoreOp::eStore, vk::ClearDepthStencilValue().setDepth(0.0f).setStencil(0));

                scheduler.CreateBuffer(ResourceNames::CameraBuffer,
                                       GfxBufferDescription(sizeof(Shaders::CameraData), sizeof(Shaders::CameraData),
                                                            vk::BufferUsageFlagBits::eUniformBuffer,
                                                            EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT));
                mainPassData.CameraBuffer =
                    scheduler.WriteBuffer(ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT);

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
                    vk::Rect2D().setExtent(m_ViewportExtent));
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_ShadeMainPipeline.get());

                auto& cameraUBO             = scheduler.GetBuffer(mainPassData.CameraBuffer);
                const auto cameraShaderData = GetShaderMainCameraData();
                cameraUBO->SetData(&cameraShaderData, sizeof(cameraShaderData));

                /*
                for (auto& smth : somthings)
                {

                struct PushConstantBlock
                {
                    const Shaders::CameraData* CameraData;
                } pc          = {};
                pc.CameraData = (const Shaders::CameraData*)cameraUBO->GetBDA();

                cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
                                                     0, pc);
                cmd.drawMeshTasksEXT(smth.meshletCount, 1, 1);
                }*/
            });
#endif

#if 1
        struct MSTestPassData
        {
            RGResourceID CameraBuffer;
        } msTestPassData = {};
        m_RenderGraph->AddPass(
            "MeshShaderTestPass", ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateTexture(
                    ResourceNames::GBufferAlbedo,
                    GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                          vk::Format::eR8G8B8A8Srgb,
                                          vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc, std::nullopt, 1,
                                          vk::SampleCountFlagBits::e1, EResourceCreateBits::RESOURCE_CREATE_DONT_TOUCH_SAMPLED_IMAGES_BIT));
                scheduler.WriteRenderTarget(ResourceNames::GBufferAlbedo, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                            vk::AttachmentStoreOp::eStore, vk::ClearColorValue().setFloat32({0.0f, 0.0f, 0.0f, 0.0f}));

                scheduler.CreateTexture(ResourceNames::DepthBuffer,
                                        GfxTextureDescription(vk::ImageType::e2D,
                                                              glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                              vk::Format::eD32Sfloat, vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                                              vk::SamplerCreateInfo()
                                                                  .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
                                                                  .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
                                                                  .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
                                                                  .setMagFilter(vk::Filter::eNearest)
                                                                  .setMinFilter(vk::Filter::eNearest)
                                                                  .setBorderColor(vk::BorderColor::eFloatOpaqueBlack)));
                scheduler.WriteDepthStencil(ResourceNames::DepthBuffer, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                            vk::AttachmentStoreOp::eStore, vk::ClearDepthStencilValue().setDepth(0.0f).setStencil(0));

                scheduler.CreateBuffer(ResourceNames::CameraBuffer,
                                       GfxBufferDescription(sizeof(Shaders::CameraData), sizeof(Shaders::CameraData),
                                                            vk::BufferUsageFlagBits::eUniformBuffer,
                                                            EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT));
                msTestPassData.CameraBuffer =
                    scheduler.WriteBuffer(ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT);

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
                    vk::Rect2D().setExtent(m_ViewportExtent));
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_MSTriPipeline.get());

                auto& cameraUBO             = scheduler.GetBuffer(msTestPassData.CameraBuffer);
                const auto cameraShaderData = GetShaderMainCameraData();
                cameraUBO->SetData(&cameraShaderData, sizeof(cameraShaderData));

                struct PushConstantBlock
                {
                    const Shaders::CameraData* CameraData;
                } pc          = {};
                pc.CameraData = (const Shaders::CameraData*)cameraUBO->GetBDA();
                cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
                                                     0, pc);
                cmd.drawMeshTasksEXT(1, 1, 1);
            });
#endif

        m_UIRenderer->RenderFrame(m_ViewportExtent, m_RenderGraph, ResourceNames::GBufferAlbedo,
                                  [&]()
                                  {
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
