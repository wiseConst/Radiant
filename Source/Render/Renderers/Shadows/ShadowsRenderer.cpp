#include "ShadowsRenderer.hpp"

#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

namespace Radiant
{
    namespace ResourceNames
    {
        const std::string LightBuffer{"Resource_Light_Buffer"};
        const std::string CameraBuffer{"Resource_Camera_Buffer"};

        const std::string CSMDataBuffer{"Resource_CSMDataBuffer"};
        const std::string ShadowsDepthBoundsBuffer{"Resource_Shadows_Depth_Bounds_Buffer"};
        const std::string CSMShadowMapTexture{"Resource_CSM_TextureArray"};

        const std::string GBufferDepth{"Resource_DepthBuffer"};
        const std::string GBufferAlbedo{"Resource_LBuffer"};

        const std::string MainPassShaderDataBuffer{"Resource_MainPassShaderDataBuffer"};

        const std::string FinalPassTexture{"Resource_Final_Texture"};
    }  // namespace ResourceNames

    static glm::vec3 s_SunColor{1.0f};

    static f32 s_MeshScale = 0.01f;
    static glm::vec3 s_MeshTranslation{0.0f, 0.0f, 0.0f};
    static glm::vec3 s_MeshRotation{0.0f, 0.0f, 0.0f};

    static bool s_bComputeTightBounds{true};  // switches whole csm pipeline to GPU.(setup shadows, etc..)
    static bool s_bCascadeTexelSizedIncrements{true};
    static f32 s_CascadeSplitDelta{0.95f};
    static f32 s_CascadeMinDistance{0.01f};   // zNear
    static f32 s_CascadeMaxDistance{350.0f};  // zFar

    static u64 s_DrawCallCount{0};

    // for sdsm d16 could be enough
    static vk::Format s_CSMTextureFormat = vk::Format::eD16Unorm /*eD32Sfloat*/;

    ShadowsRenderer::ShadowsRenderer() noexcept
    {
        m_MainCamera = MakeShared<Camera>(70.0f, static_cast<f32>(m_ViewportExtent.width) / static_cast<f32>(m_ViewportExtent.height),
                                          1000.0f, 0.001f);
        m_Scene      = MakeUnique<Scene>("ShadowsRendererTest");  // forward renderer with shadows

        std::vector<std::future<void>> thingsToPrepare{};

        // DepthPrePass
        thingsToPrepare.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName = "DepthPrePass",
                    .PipelineOptions =
                        GfxGraphicsPipelineOptions{.RenderingFormats{vk::Format::eD32Sfloat},
                                                   .DynamicStates{vk::DynamicState::eCullMode, vk::DynamicState::ePrimitiveTopology},
                                                   .FrontFace{vk::FrontFace::eCounterClockwise},
                                                   .PolygonMode{vk::PolygonMode::eFill},
                                                   .bDepthTest{true},
                                                   .bDepthWrite{true},
                                                   .DepthCompareOp{vk::CompareOp::eGreaterOrEqual}},
                    .Shader = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                    GfxShaderDescription{.Path = "../Assets/Shaders/depth_pre_pass.slang"})};
                m_DepthPrePassPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        // CSMPass
        thingsToPrepare.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName       = "CSMPass",
                    .PipelineOptions = GfxGraphicsPipelineOptions{.RenderingFormats{s_CSMTextureFormat},
                                                                  .DynamicStates{vk::DynamicState::ePrimitiveTopology},
                                                                  .CullMode{vk::CullModeFlagBits::eFront},  // fuck peter pan
                                                                  .FrontFace{vk::FrontFace::eCounterClockwise},
                                                                  .PolygonMode{vk::PolygonMode::eFill},
                                                                  .bDepthClamp{true},
                                                                  .bDepthTest{true},
                                                                  .bDepthWrite{true},
                                                                  .DepthCompareOp{vk::CompareOp::eGreaterOrEqual}},
                    .Shader          = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                    GfxShaderDescription{.Path = "../Assets/Shaders/shadows/csm_pass.slang"})};
                m_CSMPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        // SDSM Tight Bounds Compute GPU.
        thingsToPrepare.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName       = "DepthBoundsCompute",
                    .PipelineOptions = GfxComputePipelineOptions{},
                    .Shader          = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                    GfxShaderDescription{.Path = "../Assets/Shaders/shadows/depth_reduction.slang"})};
                m_DepthBoundsComputePipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        // SetupShadows GPU.
        thingsToPrepare.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName       = "SetupShadows",
                    .PipelineOptions = GfxComputePipelineOptions{},
                    .Shader          = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                    GfxShaderDescription{.Path = "../Assets/Shaders/shadows/setup_csm.slang"})};
                m_ShadowsSetupPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        // MainPassPBR
        thingsToPrepare.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                // NOTE: To not create many pipelines for objects, I switch depth compare op based on AlphaMode of object.
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName = "MainPassPBR",
                    .PipelineOptions =
                        GfxGraphicsPipelineOptions{.RenderingFormats{vk::Format::eR16G16B16A16Sfloat, vk::Format::eD32Sfloat},
                                                   .DynamicStates{vk::DynamicState::eCullMode, vk::DynamicState::ePrimitiveTopology,
                                                                  vk::DynamicState::eDepthCompareOp},
                                                   .FrontFace{vk::FrontFace::eCounterClockwise},
                                                   .PolygonMode{vk::PolygonMode::eFill},
                                                   .bDepthTest{true},
                                                   .DepthCompareOp{vk::CompareOp::eEqual},
                                                   .BlendModes{GfxGraphicsPipelineOptions::EBlendMode::BLEND_MODE_ALPHA}},
                    .Shader =
                        MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                              GfxShaderDescription{.Path = "../Assets/Shaders/shadows/shading_pbr_bc_compressed.slang"})};
                m_MainLightingPassPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        // Final composition pass.
        thingsToPrepare.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                const GfxPipelineDescription pipelineDesc = {
                    .DebugName       = "FinalPass",
                    .PipelineOptions = GfxGraphicsPipelineOptions{.RenderingFormats{vk::Format::eA2B10G10R10UnormPack32},
                                                                  .FrontFace{vk::FrontFace::eCounterClockwise},
                                                                  .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                                  .PolygonMode{vk::PolygonMode::eFill}},
                    .Shader          = MakeShared<GfxShader>(m_GfxContext->GetDevice(),
                                                    GfxShaderDescription{.Path = "../Assets/Shaders/shadows/final.slang"})};
                m_FinalPassPipeline = MakeUnique<GfxPipeline>(m_GfxContext->GetDevice(), pipelineDesc);
            }));

        thingsToPrepare.emplace_back(Application::Get().GetThreadPool()->Submit(
            [&]() noexcept
            {
                m_LightData->Sun.bCastShadows      = true;
                m_LightData->Sun.Direction         = {-0.5f, 0.8f, 0.08f};
                m_LightData->Sun.Intensity         = 1.0f;
                m_LightData->Sun.Size              = 8.5f;
                m_LightData->Sun.ShadowFade        = 25.0f;
                m_LightData->Sun.MaxShadowDistance = 400.0f;
                m_LightData->Sun.Color             = Shaders::PackUnorm4x8(glm::vec4(s_SunColor, 1.0f));
                m_LightData->PointLightCount       = 0;

                m_Scene->LoadMesh(m_GfxContext, "../Assets/Models/bistro_exterior/scene.gltf");
                m_Scene->IterateObjects(m_DrawContext);
            }));

        const auto rendererPrepareBeginTime = Timer::Now();
        for (auto& thing : thingsToPrepare)
            thing.get();

        LOG_INFO("Time taken prepare the renderer: {} seconds.", Timer::GetElapsedSecondsFromNow(rendererPrepareBeginTime));
    }

    void ShadowsRenderer::RenderFrame() noexcept
    {
        auto& mainWindow = Application::Get().GetMainWindow();

        s_DrawCallCount = 0;

        static bool bHotReloadQueued{false};
        if (bHotReloadQueued && mainWindow->IsKeyReleased(GLFW_KEY_V))  // Check state frame before and current
        {
            //   m_DepthPrePassPipeline->HotReload();
            m_MainLightingPassPipeline->HotReload();
            m_FinalPassPipeline->HotReload();

            m_DepthBoundsComputePipeline->HotReload();
            m_ShadowsSetupPipeline->HotReload();
            // m_CSMPipeline->HotReload();
        }
        bHotReloadQueued = mainWindow->IsKeyPressed(GLFW_KEY_V);

        // Sort transparent objects back to front.
        std::sort(std::execution::par, m_DrawContext.RenderObjects.begin(), m_DrawContext.RenderObjects.end(),
                  [&](const RenderObject& lhs, const RenderObject& rhs)
                  {
                      if (lhs.AlphaMode == rhs.AlphaMode && lhs.AlphaMode != EAlphaMode::ALPHA_MODE_OPAQUE)
                      {
                          const f32 lhsDistToCam = glm::length(m_MainCamera->GetPosition() - glm::vec3(lhs.TRS[3]));
                          const f32 rhsDistToCam = glm::length(m_MainCamera->GetPosition() - glm::vec3(rhs.TRS[3]));
                          return lhsDistToCam > rhsDistToCam;
                      }

                      if (lhs.AlphaMode == rhs.AlphaMode) return lhs.IndexBuffer < rhs.IndexBuffer;

                      return lhs.AlphaMode < rhs.AlphaMode;
                  });

        struct FramePreparePassData
        {
            RGResourceID CameraBuffer;
            RGResourceID LightBuffer;
        } fpPassData = {};
        m_RenderGraph->AddPass(
            "FramePreparePass", ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateBuffer(ResourceNames::CameraBuffer,
                                       GfxBufferDescription(sizeof(Shaders::CameraData), sizeof(Shaders::CameraData),
                                                            vk::BufferUsageFlagBits::eUniformBuffer,
                                                            EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT));
                fpPassData.CameraBuffer =
                    scheduler.WriteBuffer(ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT);

                scheduler.CreateBuffer(ResourceNames::LightBuffer,
                                       GfxBufferDescription(sizeof(Shaders::LightData), sizeof(Shaders::LightData),
                                                            vk::BufferUsageFlagBits::eUniformBuffer,
                                                            EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT));
                fpPassData.LightBuffer =
                    scheduler.WriteBuffer(ResourceNames::LightBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT);
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& cameraUBO             = scheduler.GetBuffer(fpPassData.CameraBuffer);
                const auto cameraShaderData = GetShaderMainCameraData();
                cameraUBO->SetData(&cameraShaderData, sizeof(cameraShaderData));

                auto& lightUBO = scheduler.GetBuffer(fpPassData.LightBuffer);
                lightUBO->SetData(m_LightData.get(), sizeof(Shaders::LightData));
            });

        struct DepthPrePassData
        {
            RGResourceID CameraBuffer;
        } depthPrePassData = {};
        m_RenderGraph->AddPass(
            "DepthPrePass", ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateTexture(ResourceNames::GBufferDepth,
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
                scheduler.WriteDepthStencil(ResourceNames::GBufferDepth, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                            vk::AttachmentStoreOp::eStore, vk::ClearDepthStencilValue().setDepth(0.0f).setStencil(0));

                depthPrePassData.CameraBuffer =
                    scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_VERTEX_SHADER_RESOURCE_BIT);

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
                    vk::Rect2D().setExtent(m_ViewportExtent));
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_DepthPrePassPipeline.get());

                auto& cameraUBO = scheduler.GetBuffer(depthPrePassData.CameraBuffer);
                for (const auto& ro : m_DrawContext.RenderObjects)
                {
                    if (ro.AlphaMode != EAlphaMode::ALPHA_MODE_OPAQUE) continue;

                    struct PushConstantBlock
                    {
                        glm::vec3 scale{1.f};
                        glm::vec3 translation{0.0f};
                        float4 orientation{1};
                        glm::mat4 ViewProjectionMatrix{1.f};
                        const VertexPosition* VtxPositions{nullptr};
                    } pc = {};

                    glm::quat q{1.0f, 0.0f, 0.0f, 0.0f};
                    glm::vec3 decomposePlaceholder0{1.0f};
                    glm::vec4 decomposePlaceholder1{1.0f};
                    glm::decompose(ro.TRS * glm::rotate(glm::radians(s_MeshRotation.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
                                       glm::rotate(glm::radians(s_MeshRotation.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
                                       glm::rotate(glm::radians(s_MeshRotation.z), glm::vec3(0.0f, 0.0f, 1.0f)),
                                   pc.scale, q, pc.translation, decomposePlaceholder0, decomposePlaceholder1);
                    pc.scale *= s_MeshScale;
                    pc.translation += s_MeshTranslation;
                    pc.orientation = glm::vec4(q.w, q.x, q.y, q.z);

                    pc.VtxPositions         = (const VertexPosition*)ro.VertexPositionBuffer->GetBDA();
                    pc.ViewProjectionMatrix = m_MainCamera->GetViewProjectionMatrix();

                    pipelineStateCache.Set(cmd, ro.CullMode);
                    pipelineStateCache.Set(cmd, ro.PrimitiveTopology);

                    cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                         vk::ShaderStageFlagBits::eAll, 0, pc);
                    pipelineStateCache.Bind(cmd, ro.IndexBuffer.get(), 0, ro.IndexType);
                    cmd.drawIndexed(ro.IndexCount, 1, ro.FirstIndex, 0, 0);
                }
            });

        struct ShadowsDepthReductionPassData
        {
            RGResourceID DepthTexture;
            RGResourceID CameraBuffer;
            RGResourceID DepthBoundsBuffer;
        } sdrPassData = {};
        struct ShadowsSetupPassData
        {
            RGResourceID CameraBuffer;
            RGResourceID DepthBoundsBuffer;
            RGResourceID CSMDataBuffer;
        } ssPassData = {};
        if (s_bComputeTightBounds)
        {
            m_RenderGraph->AddPass(
                "ShadowsDepthReductionPass", ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL,
                [&](RenderGraphResourceScheduler& scheduler)
                {
                    scheduler.CreateBuffer(ResourceNames::ShadowsDepthBoundsBuffer,
                                           GfxBufferDescription(sizeof(Shaders::DepthBounds), sizeof(Shaders::DepthBounds),
                                                                vk::BufferUsageFlagBits::eStorageBuffer,
                                                                EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT));
                    sdrPassData.DepthBoundsBuffer = scheduler.WriteBuffer(
                        ResourceNames::ShadowsDepthBoundsBuffer, EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT |
                                                                     EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                    scheduler.ClearOnExecute(ResourceNames::ShadowsDepthBoundsBuffer, std::numeric_limits<u32>::max(), sizeof(u32));
                    scheduler.ClearOnExecute(ResourceNames::ShadowsDepthBoundsBuffer, std::numeric_limits<u32>::min(), sizeof(u32),
                                             sizeof(u32));

                    sdrPassData.CameraBuffer =
                        scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);

                    sdrPassData.DepthTexture = scheduler.ReadTexture(ResourceNames::GBufferDepth, MipSet::FirstMip(),
                                                                     EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                },
                [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
                {
                    auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                    pipelineStateCache.Bind(cmd, m_DepthBoundsComputePipeline.get());

                    struct PushConstantBlock
                    {
                        u32 DepthTextureID;
                        glm::vec2 SrcTexelSize;
                        const Shaders::CameraData* CameraData;
                        Shaders::DepthBounds* DepthBounds;
                    } pc = {};

                    auto& depthTexture = scheduler.GetTexture(sdrPassData.DepthTexture);
                    pc.DepthTextureID  = depthTexture->GetBindlessTextureID();

                    const auto& dimensions = depthTexture->GetDescription().Dimensions;
                    pc.SrcTexelSize        = 1.0f / glm::vec2(dimensions.x, dimensions.y);
                    pc.CameraData          = (const Shaders::CameraData*)scheduler.GetBuffer(sdrPassData.CameraBuffer)->GetBDA();
                    pc.DepthBounds         = (Shaders::DepthBounds*)scheduler.GetBuffer(sdrPassData.DepthBoundsBuffer)->GetBDA();

                    cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                         vk::ShaderStageFlagBits::eAll, 0, pc);
                    cmd.dispatch(glm::ceil(dimensions.x / (f32)DEPTH_REDUCTION_WG_SIZE_X),
                                 glm::ceil(dimensions.y / (f32)DEPTH_REDUCTION_WG_SIZE_Y), 1);
                });

            m_RenderGraph->AddPass(
                "ShadowsSetupPass", ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL,
                [&](RenderGraphResourceScheduler& scheduler)
                {
                    scheduler.CreateBuffer(ResourceNames::CSMDataBuffer,
                                           GfxBufferDescription(sizeof(Shaders::CascadedShadowMapsData),
                                                                sizeof(Shaders::CascadedShadowMapsData),
                                                                vk::BufferUsageFlagBits::eStorageBuffer,
                                                                EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT));
                    ssPassData.CSMDataBuffer =
                        scheduler.WriteBuffer(ResourceNames::CSMDataBuffer, EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT |
                                                                                EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT);

                    ssPassData.DepthBoundsBuffer = scheduler.ReadBuffer(ResourceNames::ShadowsDepthBoundsBuffer,
                                                                        EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT |
                                                                            EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);

                    ssPassData.CameraBuffer =
                        scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_COMPUTE_SHADER_RESOURCE_BIT);
                },
                [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
                {
                    auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                    pipelineStateCache.Bind(cmd, m_ShadowsSetupPipeline.get());

                    struct PushConstantBlock
                    {
                        const Shaders::CameraData* CameraData;
                        const Shaders::DepthBounds* DepthBounds;
                        Shaders::CascadedShadowMapsData* CSMData;
                        glm::vec3 SunDirection;  // NOTE: defines "sun position".
                        f32 CascadeSplitLambda;
                    } pc = {};

                    pc.CameraData         = (const Shaders::CameraData*)scheduler.GetBuffer(ssPassData.CameraBuffer)->GetBDA();
                    pc.DepthBounds        = (const Shaders::DepthBounds*)scheduler.GetBuffer(ssPassData.DepthBoundsBuffer)->GetBDA();
                    pc.CSMData            = (Shaders::CascadedShadowMapsData*)scheduler.GetBuffer(ssPassData.CSMDataBuffer)->GetBDA();
                    pc.SunDirection       = m_LightData->Sun.Direction;
                    pc.CascadeSplitLambda = s_CascadeSplitDelta;

                    cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                         vk::ShaderStageFlagBits::eAll, 0, pc);
                    cmd.dispatch(1, 1, 1);
                });
        }

        // NOTE: Auto cascade split delta computation unfortunately sucks if s_CascadeMinDistance < 1, cuz f32 precision below gives 1.
        // s_CascadeSplitDelta = (s_CascadeMaxDistance - s_CascadeMinDistance) / s_CascadeMaxDistance;

        struct CascadedShadowMapsPassData
        {
            RGResourceID CSMDataBuffer;
        };
        std::array<CascadedShadowMapsPassData, SHADOW_MAP_CASCADE_COUNT> cmsPassDatas{};

        m_RenderGraph->AddPass(
            "CSMPass", ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                for (u32 cascadeIndex{}; cascadeIndex < SHADOW_MAP_CASCADE_COUNT; ++cascadeIndex)
                {
                    if (cascadeIndex == 0 && !s_bComputeTightBounds)
                    {
                        scheduler.CreateBuffer(ResourceNames::CSMDataBuffer,
                                               GfxBufferDescription(sizeof(Shaders::CascadedShadowMapsData),
                                                                    sizeof(Shaders::CascadedShadowMapsData),
                                                                    vk::BufferUsageFlagBits::eUniformBuffer,
                                                                    EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT));
                        cmsPassDatas[cascadeIndex].CSMDataBuffer =
                            scheduler.WriteBuffer(ResourceNames::CSMDataBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT);
                    }
                    else
                    {
                        cmsPassDatas[cascadeIndex].CSMDataBuffer =
                            scheduler.ReadBuffer(ResourceNames::CSMDataBuffer,
                                                 EResourceStateBits::RESOURCE_STATE_VERTEX_SHADER_RESOURCE_BIT |
                                                     (s_bComputeTightBounds ? EResourceStateBits::RESOURCE_STATE_STORAGE_BUFFER_BIT
                                                                            : EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT));
                    }

                    if (cascadeIndex == 0)
                    {
                        scheduler.CreateTexture(ResourceNames::CSMShadowMapTexture,
                                                GfxTextureDescription(vk::ImageType::e2D,
                                                                      glm::uvec3(SHADOW_MAP_CASCADE_SIZE, SHADOW_MAP_CASCADE_SIZE, 1),
                                                                      s_CSMTextureFormat, vk::ImageUsageFlagBits::eDepthStencilAttachment,
                                                                      vk::SamplerCreateInfo()
                                                                          .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
                                                                          .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
                                                                          .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
                                                                          .setMagFilter(vk::Filter::eNearest)
                                                                          .setMinFilter(vk::Filter::eNearest)
                                                                          .setBorderColor(vk::BorderColor::eFloatOpaqueBlack),
                                                                      SHADOW_MAP_CASCADE_COUNT));
                    }

                    scheduler.WriteDepthStencil(ResourceNames::CSMShadowMapTexture, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                                vk::AttachmentStoreOp::eStore, vk::ClearDepthStencilValue().setDepth(0.0f).setStencil(0),
                                                vk::AttachmentLoadOp::eNoneKHR, vk::AttachmentStoreOp::eNone, cascadeIndex);
                }

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(SHADOW_MAP_CASCADE_SIZE).setHeight(SHADOW_MAP_CASCADE_SIZE),
                    vk::Rect2D().setExtent(vk::Extent2D().setWidth(SHADOW_MAP_CASCADE_SIZE).setHeight(SHADOW_MAP_CASCADE_SIZE)));
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                if (!m_LightData->Sun.bCastShadows) return;

                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_CSMPipeline.get());

                auto& csmDataBuffer = scheduler.GetBuffer(cmsPassDatas[0].CSMDataBuffer);

                if (!s_bComputeTightBounds)
                {
                    const auto csmShaderData =
                        UpdateCSMData(glm::radians(m_MainCamera->GetZoom()), m_MainCamera->GetAspectRatio(), s_CascadeMinDistance,
                                      s_CascadeMaxDistance, m_MainCamera->GetViewMatrix(), glm::normalize(m_LightData->Sun.Direction));

                    csmDataBuffer->SetData(&csmShaderData, sizeof(csmShaderData));  // NOTE: will be used further in main pass
                }

                for (const auto& ro : m_DrawContext.RenderObjects)
                {
                    if (ro.AlphaMode != EAlphaMode::ALPHA_MODE_OPAQUE) continue;

                    struct PushConstantBlock
                    {
                        glm::vec3 scale{1.0f};
                        glm::vec3 translation{0.0f};
                        float4 orientation{1.0f};
                        const Shaders::CascadedShadowMapsData* CSMData{nullptr};
                        const VertexPosition* VtxPositions{nullptr};
                    } pc = {};
                    glm::quat q{1.0f, 0.0f, 0.0f, 0.0f};
                    glm::vec3 decomposePlaceholder0{1.0f};
                    glm::vec4 decomposePlaceholder1{1.0f};
                    glm::decompose(ro.TRS * glm::rotate(glm::radians(s_MeshRotation.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
                                       glm::rotate(glm::radians(s_MeshRotation.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
                                       glm::rotate(glm::radians(s_MeshRotation.z), glm::vec3(0.0f, 0.0f, 1.0f)),
                                   pc.scale, q, pc.translation, decomposePlaceholder0, decomposePlaceholder1);
                    pc.scale *= s_MeshScale;
                    pc.translation += s_MeshTranslation;
                    pc.orientation = glm::vec4(q.w, q.x, q.y, q.z);

                    pc.CSMData      = (const Shaders::CascadedShadowMapsData*)csmDataBuffer->GetBDA();
                    pc.VtxPositions = (const VertexPosition*)ro.VertexPositionBuffer->GetBDA();

                    pipelineStateCache.Set(cmd, ro.PrimitiveTopology);
                    cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                         vk::ShaderStageFlagBits::eAll, 0, pc);
                    pipelineStateCache.Bind(cmd, ro.IndexBuffer.get(), 0, ro.IndexType);
                    cmd.drawIndexed(ro.IndexCount, 1, ro.FirstIndex, 0, 0);
                }
            });

        struct MainPassShaderData
        {
            const Shaders::CascadedShadowMapsData* CSMData{nullptr};
            u32 ShadowMapTextureArrayID{0};
        };

        struct MainPassData
        {
            RGResourceID DepthTexture;
            RGResourceID CameraBuffer;
            RGResourceID LightBuffer;

            RGResourceID CSMShadowMapTextureArray;
            RGResourceID CSMDataBuffer;
            RGResourceID MainPassShaderDataBuffer;
        } mainPassData = {};
        m_RenderGraph->AddPass(
            "MainPass", ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateTexture(ResourceNames::GBufferAlbedo,
                                        GfxTextureDescription(vk::ImageType::e2D,
                                                              glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                                              vk::Format::eR16G16B16A16Sfloat, vk::ImageUsageFlagBits::eColorAttachment));
                scheduler.WriteRenderTarget(ResourceNames::GBufferAlbedo, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                            vk::AttachmentStoreOp::eStore,
                                            vk::ClearColorValue().setFloat32({s_SunColor.x, s_SunColor.y, s_SunColor.z, 1.0f}));

                mainPassData.DepthTexture = scheduler.ReadTexture(ResourceNames::GBufferDepth, MipSet::FirstMip(),
                                                                  EResourceStateBits::RESOURCE_STATE_DEPTH_READ_BIT);

                mainPassData.CameraBuffer =
                    scheduler.ReadBuffer(ResourceNames::CameraBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT |
                                                                          EResourceStateBits::RESOURCE_STATE_VERTEX_SHADER_RESOURCE_BIT |
                                                                          EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);

                mainPassData.LightBuffer =
                    scheduler.ReadBuffer(ResourceNames::LightBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT |
                                                                         EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);

                scheduler.CreateBuffer(ResourceNames::MainPassShaderDataBuffer,
                                       GfxBufferDescription(sizeof(MainPassShaderData), sizeof(MainPassShaderData),
                                                            vk::BufferUsageFlagBits::eUniformBuffer,
                                                            EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT));
                mainPassData.MainPassShaderDataBuffer =
                    scheduler.WriteBuffer(ResourceNames::MainPassShaderDataBuffer, EResourceStateBits::RESOURCE_STATE_UNIFORM_BUFFER_BIT);

                mainPassData.CSMDataBuffer =
                    scheduler.ReadBuffer(ResourceNames::CSMDataBuffer, EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);
                for (u32 cascadeIndex{}; cascadeIndex < SHADOW_MAP_CASCADE_COUNT; ++cascadeIndex)
                    mainPassData.CSMShadowMapTextureArray =
                        scheduler.ReadTexture(ResourceNames::CSMShadowMapTexture, MipSet::FirstMip(),
                                              EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT, cascadeIndex);

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
                    vk::Rect2D().setExtent(m_ViewportExtent));
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_MainLightingPassPipeline.get());

                auto& cameraUBO = scheduler.GetBuffer(mainPassData.CameraBuffer);
                auto& lightUBO  = scheduler.GetBuffer(mainPassData.LightBuffer);

                auto& mainPassShaderDataBuffer = scheduler.GetBuffer(mainPassData.MainPassShaderDataBuffer);

                MainPassShaderData mpsData      = {};
                mpsData.ShadowMapTextureArrayID = scheduler.GetTexture(mainPassData.CSMShadowMapTextureArray)->GetBindlessTextureID();
                mpsData.CSMData = (const Shaders::CascadedShadowMapsData*)scheduler.GetBuffer(mainPassData.CSMDataBuffer)->GetBDA();

                mainPassShaderDataBuffer->SetData(&mpsData, sizeof(mpsData));
                for (const auto& ro : m_DrawContext.RenderObjects)
                {
                    ++s_DrawCallCount;

                    struct PushConstantBlock
                    {
                        glm::vec3 scale{1.f};
                        glm::vec3 translation{0.0f};
                        float4 orientation{1};
                        const Shaders::CameraData* CameraData{nullptr};
                        const VertexPosition* VtxPositions{nullptr};
                        const VertexAttribute* VtxAttributes{nullptr};
                        const Shaders::GLTFMaterial* MaterialData{nullptr};
                        const Shaders::LightData* LightData{nullptr};
                        const MainPassShaderData* MPSData{nullptr};
                    } pc = {};

                    pc.MPSData    = (const MainPassShaderData*)mainPassShaderDataBuffer->GetBDA();
                    pc.LightData  = (const Shaders::LightData*)lightUBO->GetBDA();
                    pc.CameraData = (const Shaders::CameraData*)cameraUBO->GetBDA();

                    glm::quat q{1.0f, 0.0f, 0.0f, 0.0f};
                    glm::vec3 decomposePlaceholder0{1.0f};
                    glm::vec4 decomposePlaceholder1{1.0f};
                    glm::decompose(ro.TRS * glm::rotate(glm::radians(s_MeshRotation.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
                                       glm::rotate(glm::radians(s_MeshRotation.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
                                       glm::rotate(glm::radians(s_MeshRotation.z), glm::vec3(0.0f, 0.0f, 1.0f)),
                                   pc.scale, q, pc.translation, decomposePlaceholder0, decomposePlaceholder1);
                    pc.scale *= s_MeshScale;
                    pc.translation += s_MeshTranslation;
                    pc.orientation = glm::vec4(q.w, q.x, q.y, q.z);

                    pc.VtxPositions  = (const VertexPosition*)ro.VertexPositionBuffer->GetBDA();
                    pc.VtxAttributes = (const VertexAttribute*)ro.VertexAttributeBuffer->GetBDA();
                    pc.MaterialData  = (const Shaders::GLTFMaterial*)ro.MaterialBuffer->GetBDA();

                    const auto currentDepthCompareOp =
                        ro.AlphaMode == EAlphaMode::ALPHA_MODE_OPAQUE ? vk::CompareOp::eEqual : vk::CompareOp::eGreaterOrEqual;
                    pipelineStateCache.Set(cmd, currentDepthCompareOp);
                    pipelineStateCache.Set(cmd, ro.CullMode);
                    pipelineStateCache.Set(cmd, ro.PrimitiveTopology);

                    cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(),
                                                         vk::ShaderStageFlagBits::eAll, 0, pc);
                    pipelineStateCache.Bind(cmd, ro.IndexBuffer.get(), 0, ro.IndexType);
                    cmd.drawIndexed(ro.IndexCount, 1, ro.FirstIndex, 0, 0);
                }
            });

        struct FinalPassData
        {
            RGResourceID MainPassTexture;
        } finalPassData = {};
        m_RenderGraph->AddPass(
            "FinalPass", ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                scheduler.CreateTexture(
                    ResourceNames::FinalPassTexture,
                    GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(m_ViewportExtent.width, m_ViewportExtent.height, 1.0f),
                                          vk::Format::eA2B10G10R10UnormPack32,
                                          vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc));
                scheduler.WriteRenderTarget(ResourceNames::FinalPassTexture, MipSet::FirstMip(), vk::AttachmentLoadOp::eClear,
                                            vk::AttachmentStoreOp::eStore, vk::ClearColorValue().setFloat32({0.0f, 0.0f, 0.0f, 0.0f}));

                finalPassData.MainPassTexture = scheduler.ReadTexture(ResourceNames::GBufferAlbedo, MipSet::FirstMip(),
                                                                      EResourceStateBits::RESOURCE_STATE_FRAGMENT_SHADER_RESOURCE_BIT);

                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(m_ViewportExtent.width).setHeight(m_ViewportExtent.height),
                    vk::Rect2D().setExtent(m_ViewportExtent));
            },
            [&](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& pipelineStateCache = m_GfxContext->GetPipelineStateCache();
                pipelineStateCache.Bind(cmd, m_FinalPassPipeline.get());

                struct PushConstantBlock
                {
                    u32 MainPassTextureID;
                } pc                 = {};
                pc.MainPassTextureID = scheduler.GetTexture(finalPassData.MainPassTexture)->GetBindlessTextureID();

                cmd.pushConstants<PushConstantBlock>(m_GfxContext->GetDevice()->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
                                                     0, pc);
                cmd.draw(3, 1, 0, 0);
            });

        std::string finalPassAfterDebugTextureView = ResourceNames::FinalPassTexture;
        std::vector<DebugRenderer::TextureViewDescription> textureViewDescriptionss;
        {
            for (u32 cascadeIndex{}; cascadeIndex < SHADOW_MAP_CASCADE_COUNT; ++cascadeIndex)
            {
                textureViewDescriptionss.emplace_back(ResourceNames::CSMShadowMapTexture, 0, cascadeIndex);
            }
            finalPassAfterDebugTextureView =
                m_DebugRenderer->DrawTextureView(m_ViewportExtent, m_RenderGraph, textureViewDescriptionss, finalPassAfterDebugTextureView);
        }

        m_ProfilerWindow.m_GPUGraph.LoadFrameData(m_GfxContext->GetLastFrameGPUProfilerData());
        m_ProfilerWindow.m_CPUGraph.LoadFrameData(m_GfxContext->GetLastFrameCPUProfilerData());

        m_UIRenderer->RenderFrame(
            m_ViewportExtent, m_RenderGraph, finalPassAfterDebugTextureView,
            [&]()
            {
                m_ProfilerWindow.Render();

                if (ImGui::Begin("Application Info"))
                {
                    const auto& io = ImGui::GetIO();
                    ImGui::Text("Application average [%.3f] ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

                    ImGui::Separator();
                    ImGui::Text("Renderer: %s", m_GfxContext->GetDevice()->GetGPUProperties().deviceName);

                    ImGui::Separator();
                    ImGui::Text("DrawCalls: %zu", s_DrawCallCount);

                    ImGui::Text("Swapchain Present Mode: ");
                    ImGui::SameLine();

                    const auto currentPresentModeStr = vk::to_string(m_GfxContext->GetPresentMode());
                    const auto& presentModes         = m_GfxContext->GetSupportedPresentModesList();
                    std::vector<std::string> presentModeStrs{};
                    for (auto& presentMode : presentModes)
                        presentModeStrs.emplace_back(vk::to_string(presentMode));

                    if (ImGui::BeginCombo("##Swapchain_Present_Mode", currentPresentModeStr.data(),
                                          ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_WidthFitPreview))
                    {
                        for (u32 i{}; i < presentModeStrs.size(); ++i)
                        {
                            const auto& presentModeStr = presentModeStrs[i];

                            const auto bIsSelected = currentPresentModeStr == presentModeStr;
                            if (ImGui::Selectable(presentModeStr.data(), bIsSelected)) m_GfxContext->SetPresentMode(presentModes[i]);
                            if (bIsSelected) ImGui::SetItemDefaultFocus();
                        }

                        ImGui::EndCombo();
                    }

                    if (ImGui::TreeNodeEx("Bindless Resources Statistics", ImGuiTreeNodeFlags_Framed))
                    {
                        ImGui::Text("Storage Images, Combined Image Samplers, Sampled Images can overlap.");
                        const auto bindlessStatistics = m_GfxContext->GetDevice()->GetBindlessStatistics();
                        ImGui::Text("Storage Images Used: %zu", bindlessStatistics.StorageImagesUsed);
                        ImGui::Text("Combined Image Samplers Used: %zu", bindlessStatistics.CombinedImageSamplersUsed);
                        ImGui::Text("Sampled Images Used: %zu", bindlessStatistics.SampledImagesUsed);
                        ImGui::Text("Samplers Used: %zu", bindlessStatistics.SamplersUsed);

                        ImGui::TreePop();
                    }

                    ImGui::Separator();
                    if (ImGui::TreeNodeEx("RenderGraph Statistics", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Text("Build Time: [%.3f] ms", m_RenderGraphStats.BuildTime);
                        ImGui::Text("Barrier Batch Count: %u", m_RenderGraphStats.BarrierBatchCount);
                        ImGui::Text("Barrier Count: %u", m_RenderGraphStats.BarrierCount);

                        m_RenderGraphResourcePool->UI_ShowResourceUsage();

                        ImGui::TreePop();
                    }

                    ImGui::Separator();
                    ImGui::Text("Camera Position: %s", glm::to_string(m_MainCamera->GetPosition()).data());

                    if (ImGui::TreeNodeEx("Sun Parameters", ImGuiTreeNodeFlags_Framed))
                    {
                        ImGui::DragFloat3("Direction", (f32*)&m_LightData->Sun.Direction, 0.01f, -1.0f, 1.0f);
                        ImGui::DragFloat("Intensity", &m_LightData->Sun.Intensity, 0.01f, 0.0f, 500.0f);
                        ImGui::DragFloat("Size", &m_LightData->Sun.Size, 0.1f, 0.0f, 50.0f);
                        ImGui::DragFloat("Shadow Fade", &m_LightData->Sun.ShadowFade, 1.0f, 0.0f);
                        ImGui::DragFloat("Max Shadow Distance", &m_LightData->Sun.MaxShadowDistance, 1.0f, 0.0f);
                        ImGui::Checkbox("Cast Shadows", &m_LightData->Sun.bCastShadows);

                        if (ImGui::DragFloat3("Radiance", (f32*)&s_SunColor, 0.01f, 0.0f, 1.0f))
                            m_LightData->Sun.Color = Shaders::PackUnorm4x8(glm::vec4(s_SunColor, 1.0f));

                        ImGui::TreePop();
                    }
                }

                ImGui::Separator();
                if (ImGui::TreeNodeEx("Mesh Transform", ImGuiTreeNodeFlags_Framed))
                {
                    ImGui::DragFloat3("Translation", (float*)&s_MeshTranslation, 0.5f);
                    ImGui::DragFloat3("Rotation", (float*)&s_MeshRotation, 1.f, -360.0f, 360.0f);
                    ImGui::DragFloat("Scale", &s_MeshScale, 0.01f, 0.0f);

                    ImGui::TreePop();
                }

                ImGui::Separator();
                if (ImGui::TreeNodeEx("Cascaded Shadow Maps", ImGuiTreeNodeFlags_Framed))
                {
                    ImGui::Checkbox("Compute Tight Bounds (SDSM)", &s_bComputeTightBounds);
                    ImGui::Checkbox("Cascade Texel-Sized Incrementing", &s_bCascadeTexelSizedIncrements);
                    ImGui::DragFloat("Cascade Split Delta", &s_CascadeSplitDelta, 0.001f, 0.001f, 0.999f);
                    ImGui::DragFloat("Cascade Min Distance(zNear start)", &s_CascadeMinDistance, 0.001f, 0.0f);
                    ImGui::DragFloat("Cascade Max Distance(zFar end)", &s_CascadeMaxDistance, 1.0f, 10.0f);
                    ImGui::TreePop();
                }

                ImGui::End();
            });

        m_RenderGraph->Build();
        m_RenderGraph->Execute();

        m_RenderGraphStats = m_RenderGraph->GetStatistics();
    }

    /*
        Calculate frustum split depths and matrices for the shadow map cascades
        Based on https://johanmedestrom.wordpress.com/2016/03/18/opengl-cascaded-shadow-maps/
    */
    Shaders::CascadedShadowMapsData ShadowsRenderer::UpdateCSMData(const f32 cameraFovY, const f32 cameraAR, const f32 zNear,
                                                                   const f32 zFar, const glm::mat4& cameraView, const glm::vec3& L) noexcept
    {
        Shaders::CascadedShadowMapsData csmData = {.MinMaxCascadeDistance = glm::vec2{zNear, zFar}};

        // Calculate split depths based on view camera frustum
        // Based on method presented in
        // https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus
        const f32 range = zFar - zNear;
        const f32 ratio = zFar / zNear;
        for (u32 i{}; i < SHADOW_MAP_CASCADE_COUNT; ++i)
        {
            const f32 p              = (i + 1) / static_cast<f32>(SHADOW_MAP_CASCADE_COUNT);
            const f32 logPart        = zNear * glm::pow(ratio, p);
            const f32 uniformPart    = zNear + range * p;
            const f32 d              = uniformPart + s_CascadeSplitDelta * (logPart - uniformPart);
            csmData.CascadeSplits[i] = (d - zNear) / range;
        }

        const auto shadowCameraProj      = glm::perspective(cameraFovY, cameraAR, zNear, zFar) * glm::scale(glm::vec3(1.0f, -1.0f, 1.0f));
        const auto NDCToWorldSpaceMatrix = glm::inverse(shadowCameraProj * cameraView);
        f32 lastSplitDist                = 0.0f;
        for (u32 i{}; i < SHADOW_MAP_CASCADE_COUNT; ++i)
        {
            const f32 splitDist = csmData.CascadeSplits[i];

            // Starting with vulkan NDC coords, ending with frustum world space.
            std::array<glm::vec3, 8> frustumCornersWS = {
                glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f),    //
                glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(-1.0f, -1.0f, 0.0f),  //
                glm::vec3(-1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f),    //
                glm::vec3(1.0f, -1.0f, 1.0f), glm::vec3(-1.0f, -1.0f, 1.0f)   //
            };

            // Project frustum corners into world space.
            for (u32 j{}; j < frustumCornersWS.size(); ++j)
            {
                const auto cornerWS = NDCToWorldSpaceMatrix * glm::vec4(frustumCornersWS[j], 1.0f);
                frustumCornersWS[j] = cornerWS / cornerWS.w;
            }

            // Adjust frustum to current subfrustum.
            for (u32 j{}; j < frustumCornersWS.size() / 2; ++j)
            {
                const auto cornerRay    = frustumCornersWS[j + 4] - frustumCornersWS[j];
                frustumCornersWS[j + 4] = frustumCornersWS[j] + cornerRay * splitDist;
                frustumCornersWS[j] += cornerRay * lastSplitDist;
            }

            // Get frustum center.
            glm::vec3 frustumCenterWS{0.0f};
            for (const auto& frustumCornerWS : frustumCornersWS)
                frustumCenterWS += frustumCornerWS;

            frustumCenterWS /= frustumCornersWS.size();

            // Find the longest radius of the frustum.
            f32 radius{std::numeric_limits<f32>::lowest()};
            for (const auto& frustumCornerWS : frustumCornersWS)
            {
                const f32 diff = glm::length(frustumCornerWS - frustumCenterWS);
                radius         = glm::max(radius, diff);
            }
            radius = glm::ceil(radius * 16.0f) / 16.0f;

            const auto maxExtents = glm::vec3(radius);
            const auto minExtents = -maxExtents;

            const auto lightView = glm::lookAt(frustumCenterWS + L + glm::vec3(Shaders::s_KINDA_SMALL_NUMBER, 0.0f, 0.0f), frustumCenterWS,
                                               glm::vec3(0.0f, 1.0f, 0.0f));
            auto lightOrthoProj  = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, maxExtents.z, minExtents.z) *
                                  glm::scale(glm::vec3(1.0f, -1.0f, 1.0f));

            // https://www.gamedev.net/forums/topic/591684-xna-40---shimmering-shadow-maps/
            if (s_bCascadeTexelSizedIncrements)
            {
                // Shimmering fix: move in texel-sized increments.
                // (finding out how much we need to move the orthographic matrix so it matches up with shadow map)
                const auto shadowMatrix = lightOrthoProj * lightView;
                glm::vec4 shadowOrigin  = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                shadowOrigin *= (f32)SHADOW_MAP_CASCADE_SIZE * 0.5f;

                const auto roundedOrigin = glm::round(shadowOrigin);
                glm::vec4 roundOffset    = roundedOrigin - shadowOrigin;
                roundOffset              = roundOffset * 2.0f / (f32)SHADOW_MAP_CASCADE_SIZE;
                roundOffset.z = roundOffset.w = 0.0f;

                lightOrthoProj[3] += roundOffset;
            }

            lastSplitDist                   = splitDist;
            csmData.ViewProjectionMatrix[i] = lightOrthoProj * lightView;
            csmData.CascadeSplits[i]        = zNear + splitDist * range;
        }

        return csmData;
    }

}  // namespace Radiant
