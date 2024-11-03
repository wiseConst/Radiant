#include "AlanWake2Renderer.hpp"
#include <aw2/aw2_defines.hpp>

namespace Radiant
{
    namespace ResourceNames
    {
        const std::string CameraBuffer{"Resource_CameraBuffer"};
        const std::string GBufferAlbedo{"Resource_LBuffer"};

        const std::string PrevFrameDepthBuffer{"Resource_DepthBufferLastFrame"};
        const std::string DepthBuffer{"Resource_DepthBuffer"};
        const std::string HiZBuffer{"Resource_HiZBuffer"};
    }  // namespace ResourceNames

    namespace AW2
    {

        AlanWake2Renderer::AlanWake2Renderer() noexcept
        {
            m_MainCamera = MakeShared<Camera>(70.0f, static_cast<f32>(m_ViewportExtent.width) / static_cast<f32>(m_ViewportExtent.height),
                                              1000.0f, 0.0001f);

            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName       = "Hierarchical Z Buffer Build",
                    .PipelineOptions = GfxComputePipelineOptions{},
                    .Shader =
                        MakeShared<GfxShader>(m_GfxContext->GetDevice(), GfxShaderDescription{.Path = "../Assets/Shaders/aw2/hzb.slang"})};
                m_HZBPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }

            {
                auto triMsShader                     = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                         GfxShaderDescription{.Path = "../Assets/Shaders/aw2/triangle_mesh_shader.slang"});
                const GfxGraphicsPipelineOptions gpo = {
                    .RenderingFormats{vk::Format::eR8G8B8A8Srgb, vk::Format::eD32Sfloat},
                    //   .DynamicStates{vk::DynamicState::eCullMode},
                    .FrontFace{vk::FrontFace::eCounterClockwise},
                    .PolygonMode{vk::PolygonMode::eFill},
                    .bDepthTest{true},
                    .bDepthWrite{true},
                    .DepthCompareOp{vk::CompareOp::eGreaterOrEqual},
                };
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName = "triangle_mesh_shader", .PipelineOptions = gpo, .Shader = triMsShader};
                m_MSTriPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }
        }

        void AlanWake2Renderer::RenderFrame() noexcept
        {

            struct MSTestPassData
            {
                RGResourceID CameraBuffer;
            } msTestPassData = {};
            m_RenderGraph->AddPass(
                "MeshShaderTestPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
                [&](RenderGraphResourceScheduler& scheduler)
                {
                    scheduler.CreateTexture(
                        ResourceNames::GBufferAlbedo,
                        GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                              vk::Format::eR8G8B8A8Srgb,
                                              vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc, std::nullopt,
                                              1, vk::SampleCountFlagBits::e1,
                                              EResourceCreateBits::RESOURCE_CREATE_DONT_TOUCH_SAMPLED_IMAGES_BIT));
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

                    scheduler.SetViewportScissors(vk::Viewport()
                                                      .setMinDepth(0.0f)
                                                      .setMaxDepth(1.0f)
                                                      .setWidth(m_ViewportExtent.width)
                                                      .setHeight(m_ViewportExtent.height),
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
                    cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                         vk::ShaderStageFlagBits::eAll, 0, pc);
                    cmd.drawMeshTasksEXT(1, 1, 1);
                });

#if 1
            struct HZBPassData
            {
                RGResourceID DepthTexture;
                RGResourceID HZBTexture;
            };
            std::array<HZBPassData, HZB_MIP_COUNT> hzbPassDatas{};
            const auto realHzbMipCount = GfxTextureUtils::GetMipLevelCount(m_ViewportExtent.width, m_ViewportExtent.height);
            RDNT_ASSERT(realHzbMipCount <= HZB_MIP_COUNT, "Reached HZB mip count limit, extend it!");

            for (u32 mipLevel{}; mipLevel < realHzbMipCount; ++mipLevel)
            {
                const auto passName = "HZBPass" + std::to_string(mipLevel);

                m_RenderGraph->AddPass(
                    passName, ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_COMPUTE,
                    [&](RenderGraphResourceScheduler& scheduler)
                    {
                        if (mipLevel == 0)
                        {
                            scheduler.CreateTexture(
                                ResourceNames::HiZBuffer,
                                GfxTextureDescription(
                                    vk::ImageType::e2D, glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                    vk::Format::eR32Sfloat, vk::ImageUsageFlagBits::eStorage,
                                    vk::SamplerCreateInfo()
                                        .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
                                        .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
                                        .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
                                        .setMagFilter(vk::Filter::eNearest)
                                        .setMinFilter(vk::Filter::eNearest)
                                        .setBorderColor(vk::BorderColor::eFloatOpaqueBlack),
                                    1, vk::SampleCountFlagBits::e1,
                                    EResourceCreateBits::RESOURCE_CREATE_EXPOSE_MIPS_BIT |
                                        EResourceCreateBits::RESOURCE_CREATE_CREATE_MIPS_BIT |
                                        EResourceCreateBits::RESOURCE_CREATE_FORCE_NO_RESOURCE_MEMORY_ALIASING_BIT,  // TODO: remove no rma
                                                                                                                     // when you finally add
                                                                                                                     // pass that uses hzb!
                                    realHzbMipCount));

                            hzbPassDatas[mipLevel].DepthTexture =
                                scheduler.ReadTexture(ResourceNames::DepthBuffer, MipSet::FirstMip(),
                                                      EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                        }

                        if (mipLevel > 0)
                        {
                            scheduler.ReadTexture(ResourceNames::HiZBuffer, MipSet::Explicit(mipLevel - 1),
                                                  EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                        }
                        hzbPassDatas[mipLevel].HZBTexture =
                            scheduler.WriteTexture(ResourceNames::HiZBuffer, MipSet::Explicit(mipLevel),
                                                   EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                    },
                    [&, mipLevel](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
                    {
                        auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                        pipelineStateCache.Bind(cmd, m_HZBPipeline.get());

                        struct PushConstantBlock
                        {
                            u32 SrcTextureID{};
                            u32 DstTextureID{};
                            u32 SamplerID{};
                            glm::vec2 SrcTextureSizeRcp{1.0f};
                        } pc = {};

                        constexpr auto samplerReductionMode =
                            vk::SamplerReductionModeCreateInfo().setReductionMode(vk::SamplerReductionMode::eMax);
                        pc.SamplerID = m_GfxContext->GetDevice()
                                           ->GetSampler(vk::SamplerCreateInfo()
                                                            .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                                            .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                                            .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
                                                            .setMagFilter(vk::Filter::eLinear)
                                                            .setMinFilter(vk::Filter::eLinear)
                                                            .setPNext(&samplerReductionMode))
                                           .second;

                        auto& hzbTexture = scheduler.GetTexture(hzbPassDatas[mipLevel].HZBTexture);

                        const glm::uvec2 dimensions =
                            glm::max(glm::vec2(hzbTexture->GetDescription().Dimensions.x, hzbTexture->GetDescription().Dimensions.y) *
                                         (f32)glm::pow(0.5f, mipLevel),
                                     glm::vec2(1.0f));

                        if (mipLevel == 0)
                        {
                            auto& depthTexture = scheduler.GetTexture(hzbPassDatas[mipLevel].DepthTexture);
                            pc.SrcTextureID    = depthTexture->GetBindlessSampledImageID();
                            pc.SrcTextureSizeRcp =
                                1.0f / glm::vec2(depthTexture->GetDescription().Dimensions.x, depthTexture->GetDescription().Dimensions.y);
                        }
                        else
                        {
                            pc.SrcTextureID      = hzbTexture->GetBindlessSampledImageID(mipLevel - 1);
                            pc.SrcTextureSizeRcp = 1.0f / (glm::vec2&)(2u * dimensions);
                        }

                        pc.DstTextureID = hzbTexture->GetBindlessRWImageID(mipLevel);
                        cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                             vk::ShaderStageFlagBits::eAll, 0, pc);
                        cmd.dispatch(glm::ceil(dimensions.x / (f32)HZB_WG_SIZE), glm::ceil(dimensions.y / (f32)HZB_WG_SIZE), 1);
                    });
            }
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

    }  // namespace AW2

}  // namespace Radiant
