#include "Renderer.hpp"

#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

#include <GLFW/glfw3.h>

namespace Radiant
{
    Renderer::Renderer() noexcept
        : m_GfxContext(MakeUnique<GfxContext>()), m_RenderGraphResourcePool(MakeUnique<RenderGraphResourcePool>(m_GfxContext->GetDevice())),
          m_UIRenderer(MakeUnique<ImGuiRenderer>(m_GfxContext)), m_DebugRenderer(MakeUnique<DebugRenderer>(m_GfxContext))
    {
        Application::Get().GetMainWindow()->SubscribeToResizeEvents([=](const WindowResizeData& wrd)
                                                                    { m_MainCamera->OnResized(wrd.Dimensions); });

        m_ViewportExtent = m_GfxContext->GetSwapchainExtent();
    }

    Renderer::~Renderer() noexcept
    {
        m_GfxContext->GetDevice()->WaitIdle();
    }

    bool Renderer::BeginFrame() noexcept
    {
        m_RenderGraphResourcePool->Tick();
        m_RenderGraph = MakeUnique<RenderGraph>(m_GfxContext, s_ENGINE_NAME, m_RenderGraphResourcePool);

        const auto bImageAcquired = m_GfxContext->BeginFrame();
        m_ViewportExtent          = m_GfxContext->GetSwapchainExtent();  // Update extents after swapchain been recreated if needed.

        return bImageAcquired;
    }

    void Renderer::EndFrame() noexcept
    {
        m_GfxContext->EndFrame();
    }

    void Renderer::UpdateMainCamera(const f32 deltaTime) noexcept
    {
        auto& mainWindow = Application::Get().GetMainWindow();
        if (mainWindow->IsMouseButtonPressed(GLFW_MOUSE_BUTTON_2)) m_MainCamera->Rotate(deltaTime, mainWindow->GetCursorPos());

        RDNT_ASSERT(m_MainCamera, "MainCamera is invalid!");
        m_MainCamera->UpdateMousePos(mainWindow->GetCursorPos());

        glm::vec3 velocity{0.f};
        if (mainWindow->IsKeyPressed(GLFW_KEY_W)) velocity.z += -1.f;
        if (mainWindow->IsKeyPressed(GLFW_KEY_S)) velocity.z += 1.f;

        if (mainWindow->IsKeyPressed(GLFW_KEY_A)) velocity.x += -1.f;
        if (mainWindow->IsKeyPressed(GLFW_KEY_D)) velocity.x += 1.f;

        if (mainWindow->IsKeyPressed(GLFW_KEY_SPACE)) velocity.y += 1.f;
        if (mainWindow->IsKeyPressed(GLFW_KEY_LEFT_CONTROL)) velocity.y += -1.f;

        m_MainCamera->SetVelocity(velocity);
        m_MainCamera->Move(deltaTime);
        m_MainCamera->OnResized(glm::uvec2{m_ViewportExtent.width, m_ViewportExtent.height});
    }

    NODISCARD Shaders::CameraData Renderer::GetShaderMainCameraData() const noexcept

    {
        const auto& projectionMatrix               = m_MainCamera->GetProjectionMatrix();
        const auto& viewMatrix                     = m_MainCamera->GetViewMatrix();
        const auto viewProjectionMatrix            = m_MainCamera->GetViewProjectionMatrix();
        const auto& fullResolution                 = (const glm::vec2&)m_MainCamera->GetFullResolution();
        const Shaders::CameraData shaderCameraData = {.ProjectionMatrix        = projectionMatrix,
                                                      .ViewMatrix              = viewMatrix,
                                                      .ViewProjectionMatrix    = viewProjectionMatrix,
                                                      .InvProjectionMatrix     = glm::inverse(projectionMatrix),
                                                      .InvViewProjectionMatrix = glm::inverse(viewProjectionMatrix),
                                                      .FullResolution          = fullResolution,
                                                      .InvFullResolution       = 1.0f / fullResolution,
                                                      .Position                = m_MainCamera->GetPosition(),
                                                      .zNearFar                = {m_MainCamera->GetZNear(), m_MainCamera->GetZFar()},
                                                      .Zoom                    = m_MainCamera->GetZoom()};
        return shaderCameraData;
    }

    NODISCARD std::pair<Unique<GfxTexture>, Unique<GfxTexture>> Renderer::GenerateIBLMaps(
        const std::string_view& equirectangularMapPath) noexcept
    {
        static constexpr u32 s_IrradianceCubeMapSize          = 64u;
        static constexpr u32 s_PrefilteredCubeMapSize         = s_IrradianceCubeMapSize * 2;
        static constexpr u32 s_FromEquirectangularCubeMapSize = 1024u;
        static constexpr u8 s_CubemapMipCount                 = 5;  // used globally across all cubemaps to mititgate bright dots

        // Prepare pipelines for:
        // 1) Transforming equirectangular to cubemap.
        // 2) Convolute cubemap into irradiance map KxK size. K <= 256.
        // 3) Convolute cubemap into prefiltered map used for specular indirect as a part of split-sum approximation.

        const auto& device                    = m_GfxContext->GetDevice();
        auto equirectangularToCubemapPipeline = MakeUnique<GfxPipeline>(
            device, GfxPipelineDescription{
                        .DebugName       = "equirectangular_to_cubemap",
                        .PipelineOptions = GfxGraphicsPipelineOptions{.RenderingFormats{vk::Format::eR32G32B32A32Sfloat},
                                                                      //              .CullMode{vk::CullModeFlagBits::eBack},
                                                                      .FrontFace{vk::FrontFace::eCounterClockwise},
                                                                      .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                                      .PolygonMode{vk::PolygonMode::eFill}},
                        .Shader          = MakeShared<GfxShader>(
                            device, GfxShaderDescription{.Path = "../Assets/Shaders/ibl_utils/equirectangular_to_cubemap.slang"})});

        auto irradianceCubemapPipeline = MakeUnique<GfxPipeline>(
            device, GfxPipelineDescription{
                        .DebugName       = "generate_irradiance_cube",
                        .PipelineOptions = GfxGraphicsPipelineOptions{.RenderingFormats{vk::Format::eB10G11R11UfloatPack32},
                                                                      //              .CullMode{vk::CullModeFlagBits::eBack},
                                                                      .FrontFace{vk::FrontFace::eCounterClockwise},
                                                                      .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                                      .PolygonMode{vk::PolygonMode::eFill}},
                        .Shader          = MakeShared<GfxShader>(
                            device, GfxShaderDescription{.Path = "../Assets/Shaders/ibl_utils/generate_irradiance_cube.slang"})});

        auto prefilteredCubemapPipeline = MakeUnique<GfxPipeline>(
            device, GfxPipelineDescription{
                        .DebugName       = "generate_prefiltered_cube",
                        .PipelineOptions = GfxGraphicsPipelineOptions{.RenderingFormats{vk::Format::eB10G11R11UfloatPack32},
                                                                      //              .CullMode{vk::CullModeFlagBits::eBack},
                                                                      .FrontFace{vk::FrontFace::eCounterClockwise},
                                                                      .PrimitiveTopology{vk::PrimitiveTopology::eTriangleList},
                                                                      .PolygonMode{vk::PolygonMode::eFill}},
                        .Shader          = MakeShared<GfxShader>(
                            device, GfxShaderDescription{.Path = "../Assets/Shaders/ibl_utils/generate_prefiltered_cube.slang"})});

        auto executionContext = m_GfxContext->CreateImmediateExecuteContext(ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL);
        executionContext.CommandBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        executionContext.CommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, device->GetBindlessPipelineLayout(), 0,
                                                          device->GetCurrentFrameBindlessResources().DescriptorSet, {});
#if RDNT_DEBUG
        executionContext.CommandBuffer.beginDebugUtilsLabelEXT(
            vk::DebugUtilsLabelEXT().setPLabelName("IBLMapsGen").setColor({1.0f, 1.0f, 1.0f, 1.0f}));
#endif

        // Load equirectangular HDR texture.
#if RDNT_DEBUG
        executionContext.CommandBuffer.beginDebugUtilsLabelEXT(
            vk::DebugUtilsLabelEXT().setPLabelName("Equirectangular Map Loading").setColor({1.0f, 1.0f, 1.0f, 1.0f}));
#endif

        i32 width{1}, height{1}, channels{4};
        void* hdrImageData = GfxTextureUtils::LoadImage(equirectangularMapPath, width, height, channels, 4, true);

        auto equirectangularEnvMap =
            MakeUnique<GfxTexture>(device, GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(width, height, 1),
                                                                 vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eTransferDst,
                                                                 vk::SamplerCreateInfo()
                                                                     .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                                                     .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                                                     .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
                                                                     .setMagFilter(vk::Filter::eLinear)
                                                                     .setMinFilter(vk::Filter::eLinear)));

        const auto imageSize = static_cast<u64>(width * height * channels * sizeof(f32));
        auto stagingBuffer =
            MakeUnique<GfxBuffer>(device, GfxBufferDescription(imageSize, /* placeholder */ 1, vk::BufferUsageFlagBits::eTransferSrc,
                                                               EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT));
        stagingBuffer->SetData(hdrImageData, imageSize);
        GfxTextureUtils::UnloadImage(hdrImageData);

        executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
            vk::ImageMemoryBarrier2()
                .setImage(*equirectangularEnvMap)
                .setSubresourceRange(
                    vk::ImageSubresourceRange().setBaseArrayLayer(0).setLayerCount(1).setBaseMipLevel(0).setLevelCount(1).setAspectMask(
                        vk::ImageAspectFlagBits::eColor))
                .setOldLayout(vk::ImageLayout::eUndefined)
                .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
                .setDstStageMask(vk::PipelineStageFlagBits2::eCopy)));

        executionContext.CommandBuffer.copyBufferToImage(
            *stagingBuffer, *equirectangularEnvMap, vk::ImageLayout::eTransferDstOptimal,
            vk::BufferImageCopy()
                .setImageSubresource(vk::ImageSubresourceLayers().setBaseArrayLayer(0).setMipLevel(0).setLayerCount(1).setAspectMask(
                    vk::ImageAspectFlagBits::eColor))
                .setImageExtent(vk::Extent3D(width, height, 1)));

        executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
            vk::ImageMemoryBarrier2()
                .setImage(*equirectangularEnvMap)
                .setSubresourceRange(
                    vk::ImageSubresourceRange().setBaseArrayLayer(0).setLayerCount(1).setBaseMipLevel(0).setLevelCount(1).setAspectMask(
                        vk::ImageAspectFlagBits::eColor))
                .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
                .setSrcStageMask(vk::PipelineStageFlagBits2::eCopy)
                .setDstAccessMask(vk::AccessFlagBits2::eShaderSampledRead)
                .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader)));

#if RDNT_DEBUG
        executionContext.CommandBuffer.endDebugUtilsLabelEXT();
#endif

        // Prepare vertex shader data.
        auto indexBufferReBAR = MakeUnique<GfxBuffer>(
            device, GfxBufferDescription(sizeof(Shaders::g_CubeIndices), sizeof(u8), vk::BufferUsageFlagBits::eIndexBuffer,
                                         EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT));
        indexBufferReBAR->SetData(&Shaders::g_CubeIndices, sizeof(Shaders::g_CubeIndices));

        struct EquirectangularToCubemapShaderData
        {
            glm::mat4 CaptureViewMatrices[6];
            glm::mat4 ProjectionMatrix;
        } etcsd                      = {};
        etcsd.ProjectionMatrix       = glm::perspective(glm::radians(90.0f), 1.0f, 0.001f, 10.0f);
        etcsd.CaptureViewMatrices[0] = glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                                                   glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));  // +x
        etcsd.CaptureViewMatrices[1] = glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
                                                   glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));                   // -x
        etcsd.CaptureViewMatrices[2] = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));  // +y
        etcsd.CaptureViewMatrices[3] = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));   // -y
        etcsd.CaptureViewMatrices[4] = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));  // +z
        etcsd.CaptureViewMatrices[5] = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));  // -z

        auto etcsDataBuffer =
            MakeUnique<GfxBuffer>(device, GfxBufferDescription(sizeof(etcsd), sizeof(etcsd), vk::BufferUsageFlagBits::eUniformBuffer,
                                                               EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT |
                                                                   EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_ADDRESSABLE_BIT));
        etcsDataBuffer->SetData(&etcsd, sizeof(etcsd));

        // To solve bright dots on highest mip levels, we generate mips for src env cube map.
        auto envCubeMap = MakeUnique<GfxTexture>(
            device,
            GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(s_FromEquirectangularCubeMapSize, s_FromEquirectangularCubeMapSize, 1),
                                  vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eColorAttachment,
                                  vk::SamplerCreateInfo()
                                      .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                      .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                      .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
                                      .setMagFilter(vk::Filter::eLinear)
                                      .setMinFilter(vk::Filter::eLinear)
                                      .setMipmapMode(vk::SamplerMipmapMode::eLinear)
                                      .setMinLod(0.0f)
                                      .setMaxLod(vk::LodClampNone),
                                  6, vk::SampleCountFlagBits::e1, EResourceCreateBits::RESOURCE_CREATE_CREATE_MIPS_BIT, s_CubemapMipCount));

        struct PushConstantBlock
        {
            const EquirectangularToCubemapShaderData* ETCSData{nullptr};
            u32 SrcTextureID{0};
            float Data0{0.0f};
            float Data1{1.0f};
        } pc            = {};
        pc.ETCSData     = (const EquirectangularToCubemapShaderData*)etcsDataBuffer->GetBDA();
        pc.SrcTextureID = equirectangularEnvMap->GetBindlessTextureID();

        // Transform equirectangular map to cubemap.
#if RDNT_DEBUG
        executionContext.CommandBuffer.beginDebugUtilsLabelEXT(
            vk::DebugUtilsLabelEXT().setPLabelName("Equirectangular Map To CubeMap").setColor({1.0f, 1.0f, 1.0f, 1.0f}));
#endif

        executionContext.CommandBuffer.pipelineBarrier2(
            vk::DependencyInfo().setImageMemoryBarriers(vk::ImageMemoryBarrier2()
                                                            .setImage(*envCubeMap)
                                                            .setSubresourceRange(vk::ImageSubresourceRange()
                                                                                     .setBaseArrayLayer(0)
                                                                                     .setLayerCount(6)
                                                                                     .setBaseMipLevel(0)
                                                                                     .setLevelCount(s_CubemapMipCount)
                                                                                     .setAspectMask(vk::ImageAspectFlagBits::eColor))
                                                            .setOldLayout(vk::ImageLayout::eUndefined)
                                                            .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                                            .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                                                            .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                                                            .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                                                            .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)));

        executionContext.CommandBuffer.beginRendering(
            vk::RenderingInfo()
                .setLayerCount(6)
                .setColorAttachments((vk::RenderingAttachmentInfo&)envCubeMap->GetRenderingAttachmentInfo(
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::ClearValue().setColor(vk::ClearColorValue().setFloat32({0.0f, 0.0f, 0.0f, 1.0f})), vk::AttachmentLoadOp::eClear,
                    vk::AttachmentStoreOp::eStore))
                .setRenderArea(vk::Rect2D().setExtent(
                    vk::Extent2D().setWidth(s_FromEquirectangularCubeMapSize).setHeight(s_FromEquirectangularCubeMapSize))));
        executionContext.CommandBuffer.setViewportWithCount(vk::Viewport()
                                                                .setMinDepth(0.0f)
                                                                .setMaxDepth(1.0f)
                                                                .setWidth(s_FromEquirectangularCubeMapSize)
                                                                .setHeight(s_FromEquirectangularCubeMapSize));
        executionContext.CommandBuffer.setScissorWithCount(
            vk::Rect2D().setExtent(vk::Extent2D().setWidth(s_FromEquirectangularCubeMapSize).setHeight(s_FromEquirectangularCubeMapSize)));
        executionContext.CommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *equirectangularToCubemapPipeline);

        executionContext.CommandBuffer.pushConstants<PushConstantBlock>(device->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
                                                                        0, pc);
        executionContext.CommandBuffer.bindIndexBuffer(*indexBufferReBAR, 0, vk::IndexType::eUint8EXT);
        executionContext.CommandBuffer.drawIndexed(indexBufferReBAR->GetElementCount(), 6, 0, 0, 0);
        executionContext.CommandBuffer.endRendering();

        executionContext.CommandBuffer.pipelineBarrier2(
            vk::DependencyInfo().setImageMemoryBarriers(vk::ImageMemoryBarrier2()
                                                            .setImage(*envCubeMap)
                                                            .setSubresourceRange(vk::ImageSubresourceRange()
                                                                                     .setBaseArrayLayer(0)
                                                                                     .setLayerCount(6)
                                                                                     .setBaseMipLevel(0)
                                                                                     .setLevelCount(s_CubemapMipCount)
                                                                                     .setAspectMask(vk::ImageAspectFlagBits::eColor))
                                                            .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                                            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                                                            .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                                                            .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                                                            .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
                                                            .setDstStageMask(vk::PipelineStageFlagBits2::eAllTransfer)));
        envCubeMap->GenerateMipMaps(executionContext.CommandBuffer);

#if RDNT_DEBUG
        executionContext.CommandBuffer.endDebugUtilsLabelEXT();
#endif

        // Some shitty sidenote(because of the way vulkan's resource views are):
        // In order to do textureLod/SampleLevel we have to have 1 image view created with N mips instead of N image views. (or keep
        // textureIDs per mip which is bad imho). So we create offscreen cubemap that we render into and then copy its results into dst cube
        // maps.

        // Offscreen cubemap that used for copy into dst cubemap.
        auto prefilteredOffscreenCubemap = MakeUnique<GfxTexture>(
            device, GfxTextureDescription(
                        vk::ImageType::e2D, glm::uvec3(s_PrefilteredCubeMapSize * 0.5f, s_PrefilteredCubeMapSize * 0.5f, 1),
                        vk::Format::eB10G11R11UfloatPack32, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
                        std::nullopt, 6, vk::SampleCountFlagBits::e1));

        // Final prefiltered environment map.
        auto prefilteredCubemap = MakeUnique<GfxTexture>(
            device,
            GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(s_PrefilteredCubeMapSize, s_PrefilteredCubeMapSize, 1),
                                  vk::Format::eB10G11R11UfloatPack32, vk::ImageUsageFlagBits::eTransferDst,
                                  vk::SamplerCreateInfo()
                                      .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                      .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                      .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
                                      .setMagFilter(vk::Filter::eLinear)
                                      .setMinFilter(vk::Filter::eLinear)
                                      .setMipmapMode(vk::SamplerMipmapMode::eLinear)
                                      .setMinLod(0.0f)
                                      .setMaxLod(vk::LodClampNone),
                                  6, vk::SampleCountFlagBits::e1, EResourceCreateBits::RESOURCE_CREATE_CREATE_MIPS_BIT, s_CubemapMipCount));

        // Convolute environment cubemap into prefiltered cubemap.
        pc.SrcTextureID = envCubeMap->GetBindlessTextureID();

#if RDNT_DEBUG
        executionContext.CommandBuffer.beginDebugUtilsLabelEXT(
            vk::DebugUtilsLabelEXT().setPLabelName("PrefilteredCubeMapGeneration").setColor({1.0f, 1.0f, 1.0f, 1.0f}));
#endif

        executionContext.CommandBuffer.pipelineBarrier2(
            vk::DependencyInfo().setImageMemoryBarriers({vk::ImageMemoryBarrier2()
                                                             .setImage(*prefilteredCubemap)
                                                             .setSubresourceRange(vk::ImageSubresourceRange()
                                                                                      .setBaseArrayLayer(0)
                                                                                      .setLayerCount(6)
                                                                                      .setBaseMipLevel(0)
                                                                                      .setLevelCount(s_CubemapMipCount)
                                                                                      .setAspectMask(vk::ImageAspectFlagBits::eColor))
                                                             .setOldLayout(vk::ImageLayout::eUndefined)
                                                             .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                                                             .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                                                             .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                                                             .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
                                                             .setDstStageMask(vk::PipelineStageFlagBits2::eTransfer)}));

        // Footnote from moving frostbite to pbr.(mip 0 is actually mirror(roughness=0) so don't waste compute power and simply blit)
        {
            executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                vk::ImageMemoryBarrier2()
                    .setImage(*envCubeMap)
                    .setSubresourceRange(
                        vk::ImageSubresourceRange().setBaseArrayLayer(0).setLayerCount(6).setBaseMipLevel(0).setLevelCount(1).setAspectMask(
                            vk::ImageAspectFlagBits::eColor))
                    .setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                    .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                    .setSrcAccessMask(vk::AccessFlagBits2::eShaderSampledRead)
                    .setSrcStageMask(vk::PipelineStageFlagBits2::eFragmentShader)
                    .setDstAccessMask(vk::AccessFlagBits2::eTransferRead)
                    .setDstStageMask(vk::PipelineStageFlagBits2::eBlit)));

            executionContext.CommandBuffer.blitImage2(
                vk::BlitImageInfo2()
                    .setFilter(vk::Filter::eLinear)
                    .setSrcImage(*envCubeMap)
                    .setSrcImageLayout(vk::ImageLayout::eTransferSrcOptimal)
                    .setDstImage(*prefilteredCubemap)
                    .setDstImageLayout(vk::ImageLayout::eTransferDstOptimal)
                    .setRegions(vk::ImageBlit2()
                                    .setSrcSubresource(vk::ImageSubresourceLayers()
                                                           .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                           .setBaseArrayLayer(0)
                                                           .setLayerCount(6)
                                                           .setMipLevel(0))
                                    .setSrcOffsets({vk::Offset3D(), vk::Offset3D(static_cast<i32>(s_FromEquirectangularCubeMapSize),
                                                                                 static_cast<i32>(s_FromEquirectangularCubeMapSize), 1)})
                                    .setDstSubresource(vk::ImageSubresourceLayers()
                                                           .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                           .setBaseArrayLayer(0)
                                                           .setLayerCount(6)
                                                           .setMipLevel(0))
                                    .setDstOffsets({vk::Offset3D(), vk::Offset3D(static_cast<i32>(s_PrefilteredCubeMapSize),
                                                                                 static_cast<i32>(s_PrefilteredCubeMapSize), 1)})));

            executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                vk::ImageMemoryBarrier2()
                    .setImage(*envCubeMap)
                    .setSubresourceRange(
                        vk::ImageSubresourceRange().setBaseArrayLayer(0).setLayerCount(6).setBaseMipLevel(0).setLevelCount(1).setAspectMask(
                            vk::ImageAspectFlagBits::eColor))
                    .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
                    .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                    .setSrcAccessMask(vk::AccessFlagBits2::eTransferRead)
                    .setSrcStageMask(vk::PipelineStageFlagBits2::eBlit)
                    .setDstAccessMask(vk::AccessFlagBits2::eShaderSampledRead)
                    .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader)));
        }

        executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
            vk::ImageMemoryBarrier2()
                .setImage(*prefilteredOffscreenCubemap)
                .setSubresourceRange(
                    vk::ImageSubresourceRange().setBaseArrayLayer(0).setLayerCount(6).setBaseMipLevel(0).setLevelCount(1).setAspectMask(
                        vk::ImageAspectFlagBits::eColor))
                .setOldLayout(vk::ImageLayout::eUndefined)
                .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)));

        executionContext.CommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *prefilteredCubemapPipeline);
        executionContext.CommandBuffer.setScissorWithCount(
            vk::Rect2D().setExtent(vk::Extent2D().setWidth(s_PrefilteredCubeMapSize * 0.5f).setHeight(s_PrefilteredCubeMapSize * 0.5f)));
        pc.Data1 = 1.0f / static_cast<f32>(s_FromEquirectangularCubeMapSize);
        for (u32 mipLevel{1}; mipLevel < s_CubemapMipCount; ++mipLevel)
        {
            pc.Data0                 = static_cast<f32>(mipLevel) / static_cast<f32>(s_CubemapMipCount - 1);  // roughness
            const u32 mipCubemapSize = s_PrefilteredCubeMapSize * std::pow(0.5f, mipLevel);

            executionContext.CommandBuffer.beginRendering(
                vk::RenderingInfo()
                    .setLayerCount(6)
                    .setColorAttachments((vk::RenderingAttachmentInfo&)prefilteredOffscreenCubemap->GetRenderingAttachmentInfo(
                        vk::ImageLayout::eColorAttachmentOptimal,
                        vk::ClearValue().setColor(vk::ClearColorValue().setFloat32({0.0f, 0.0f, 0.0f, 1.0f})), vk::AttachmentLoadOp::eClear,
                        vk::AttachmentStoreOp::eStore))
                    .setRenderArea(vk::Rect2D().setExtent(vk::Extent2D().setWidth(mipCubemapSize).setHeight(mipCubemapSize))));
            executionContext.CommandBuffer.setViewportWithCount(
                vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(mipCubemapSize).setHeight(mipCubemapSize));
            executionContext.CommandBuffer.pushConstants<PushConstantBlock>(device->GetBindlessPipelineLayout(),
                                                                            vk::ShaderStageFlagBits::eAll, 0, pc);
            executionContext.CommandBuffer.bindIndexBuffer(*indexBufferReBAR, 0, vk::IndexType::eUint8EXT);
            executionContext.CommandBuffer.drawIndexed(indexBufferReBAR->GetElementCount(), 6, 0, 0, 0);
            executionContext.CommandBuffer.endRendering();

            executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                vk::ImageMemoryBarrier2()
                    .setImage(*prefilteredOffscreenCubemap)
                    .setSubresourceRange(
                        vk::ImageSubresourceRange().setBaseArrayLayer(0).setLayerCount(6).setBaseMipLevel(0).setLevelCount(1).setAspectMask(
                            vk::ImageAspectFlagBits::eColor))
                    .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                    .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                    .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                    .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                    .setDstAccessMask(vk::AccessFlagBits2::eTransferRead)
                    .setDstStageMask(vk::PipelineStageFlagBits2::eCopy)));

            executionContext.CommandBuffer.copyImage2(
                vk::CopyImageInfo2()
                    .setSrcImage(*prefilteredOffscreenCubemap)
                    .setSrcImageLayout(vk::ImageLayout::eTransferSrcOptimal)
                    .setDstImage(*prefilteredCubemap)
                    .setDstImageLayout(vk::ImageLayout::eTransferDstOptimal)
                    .setRegions(vk::ImageCopy2()
                                    .setSrcSubresource(vk::ImageSubresourceLayers()
                                                           .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                           .setBaseArrayLayer(0)
                                                           .setLayerCount(6)
                                                           .setMipLevel(0))
                                    .setDstSubresource(vk::ImageSubresourceLayers()
                                                           .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                           .setBaseArrayLayer(0)
                                                           .setLayerCount(6)
                                                           .setMipLevel(mipLevel))
                                    .setExtent(vk::Extent3D().setWidth(mipCubemapSize).setHeight(mipCubemapSize).setDepth(1))));

            executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                vk::ImageMemoryBarrier2()
                    .setImage(*prefilteredOffscreenCubemap)
                    .setSubresourceRange(
                        vk::ImageSubresourceRange().setBaseArrayLayer(0).setLayerCount(6).setBaseMipLevel(0).setLevelCount(1).setAspectMask(
                            vk::ImageAspectFlagBits::eColor))
                    .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
                    .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                    .setSrcAccessMask(vk::AccessFlagBits2::eTransferRead)
                    .setSrcStageMask(vk::PipelineStageFlagBits2::eCopy)
                    .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                    .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)));
        }

        executionContext.CommandBuffer.pipelineBarrier2(
            vk::DependencyInfo().setImageMemoryBarriers(vk::ImageMemoryBarrier2()
                                                            .setImage(*prefilteredCubemap)
                                                            .setSubresourceRange(vk::ImageSubresourceRange()
                                                                                     .setBaseArrayLayer(0)
                                                                                     .setLayerCount(6)
                                                                                     .setBaseMipLevel(0)
                                                                                     .setLevelCount(s_CubemapMipCount)
                                                                                     .setAspectMask(vk::ImageAspectFlagBits::eColor))
                                                            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                                                            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                                                            .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
                                                            .setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
                                                            .setDstAccessMask(vk::AccessFlagBits2::eShaderSampledRead)
                                                            .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader)));

#if RDNT_DEBUG
        executionContext.CommandBuffer.endDebugUtilsLabelEXT();
#endif

        // Final convoluted environment map.
        auto irradianceCubemap = MakeUnique<GfxTexture>(
            device, GfxTextureDescription(vk::ImageType::e2D, glm::uvec3(s_IrradianceCubeMapSize, s_IrradianceCubeMapSize, 1),
                                          vk::Format::eB10G11R11UfloatPack32, vk::ImageUsageFlagBits::eColorAttachment,
                                          vk::SamplerCreateInfo()
                                              .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                                              .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                                              .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
                                              .setMagFilter(vk::Filter::eLinear)
                                              .setMinFilter(vk::Filter::eLinear),
                                          6, vk::SampleCountFlagBits::e1));

        // Convolute environment cubemap into irradiance cubemap.
        pc.SrcTextureID = envCubeMap->GetBindlessTextureID();
        pc.Data1        = 1.0f / static_cast<f32>(s_FromEquirectangularCubeMapSize);

#if RDNT_DEBUG
        executionContext.CommandBuffer.beginDebugUtilsLabelEXT(
            vk::DebugUtilsLabelEXT().setPLabelName("IrradianceCubeMapGeneration").setColor({1.0f, 1.0f, 1.0f, 1.0f}));
#endif

        executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
            vk::ImageMemoryBarrier2()
                .setImage(*irradianceCubemap)
                .setSubresourceRange(
                    vk::ImageSubresourceRange().setBaseArrayLayer(0).setLayerCount(6).setBaseMipLevel(0).setLevelCount(1).setAspectMask(
                        vk::ImageAspectFlagBits::eColor))
                .setOldLayout(vk::ImageLayout::eUndefined)
                .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)));

        executionContext.CommandBuffer.beginRendering(
            vk::RenderingInfo()
                .setLayerCount(6)
                .setColorAttachments((vk::RenderingAttachmentInfo&)irradianceCubemap->GetRenderingAttachmentInfo(
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::ClearValue().setColor(vk::ClearColorValue().setFloat32({0.0f, 0.0f, 0.0f, 1.0f})), vk::AttachmentLoadOp::eClear,
                    vk::AttachmentStoreOp::eStore))
                .setRenderArea(
                    vk::Rect2D().setExtent(vk::Extent2D().setWidth(s_IrradianceCubeMapSize).setHeight(s_IrradianceCubeMapSize))));
        executionContext.CommandBuffer.setScissorWithCount(
            vk::Rect2D().setExtent(vk::Extent2D().setWidth(s_IrradianceCubeMapSize).setHeight(s_IrradianceCubeMapSize)));
        executionContext.CommandBuffer.setViewportWithCount(
            vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(s_IrradianceCubeMapSize).setHeight(s_IrradianceCubeMapSize));
        executionContext.CommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *irradianceCubemapPipeline);
        executionContext.CommandBuffer.pushConstants<PushConstantBlock>(device->GetBindlessPipelineLayout(), vk::ShaderStageFlagBits::eAll,
                                                                        0, pc);
        executionContext.CommandBuffer.bindIndexBuffer(*indexBufferReBAR, 0, vk::IndexType::eUint8EXT);
        executionContext.CommandBuffer.drawIndexed(indexBufferReBAR->GetElementCount(), 6, 0, 0, 0);
        executionContext.CommandBuffer.endRendering();

        executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
            vk::ImageMemoryBarrier2()
                .setImage(*irradianceCubemap)
                .setSubresourceRange(
                    vk::ImageSubresourceRange().setBaseArrayLayer(0).setLayerCount(6).setBaseMipLevel(0).setLevelCount(1).setAspectMask(
                        vk::ImageAspectFlagBits::eColor))
                .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                .setDstAccessMask(vk::AccessFlagBits2::eShaderSampledRead)
                .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader)));
#if RDNT_DEBUG
        executionContext.CommandBuffer.endDebugUtilsLabelEXT();
#endif

#if RDNT_DEBUG
        executionContext.CommandBuffer.endDebugUtilsLabelEXT();
#endif
        executionContext.CommandBuffer.end();
        m_GfxContext->SubmitImmediateExecuteContext(executionContext);

        const auto ExtractBaseFilenameFunc = [](const std::string& path) -> std::string
        {
            const auto lastSlashPositionIndex = path.find_last_of('/');
            const auto startPositionIndex     = (lastSlashPositionIndex != std::string::npos) ? lastSlashPositionIndex + 1 : 0;

            const auto dotPositionIndex = path.find_last_of('.');
            if (dotPositionIndex == std::string::npos) return path.substr(startPositionIndex);
            return path.substr(startPositionIndex, dotPositionIndex - startPositionIndex);  // Returns actual file name without extension.
        };
        const auto environmentMapName = ExtractBaseFilenameFunc(std::string(equirectangularMapPath));

        const auto irradianceCubemapName = environmentMapName + "_Irradiance";
        device->SetDebugName(irradianceCubemapName.data(), (const vk::Image&)*irradianceCubemap);

        const auto prefilteredCubemapName = environmentMapName + "_Prefiltered";
        device->SetDebugName(prefilteredCubemapName.data(), (const vk::Image&)*prefilteredCubemap);

        return {std::move(irradianceCubemap), std::move(prefilteredCubemap)};
    }

}  // namespace Radiant
