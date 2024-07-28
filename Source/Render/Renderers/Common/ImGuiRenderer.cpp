#include <pch.h>
#include "ImGuiRenderer.hpp"

#include <Render/GfxDevice.hpp>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

namespace Radiant
{

    void ImGuiRenderer::Init() noexcept
    {
        const auto& gfxDevice     = m_GfxContext->GetDevice();
        const auto& logicalDevice = gfxDevice->GetLogicalDevice();

        constexpr std::array<vk::DescriptorPoolSize, 11> poolSizes = {
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eSampler).setDescriptorCount(1000),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eCombinedImageSampler).setDescriptorCount(1000),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eSampledImage).setDescriptorCount(1000),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eStorageImage).setDescriptorCount(1000),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eUniformTexelBuffer).setDescriptorCount(1000),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eStorageTexelBuffer).setDescriptorCount(1000),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eUniformBuffer).setDescriptorCount(1000),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1000),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eUniformBufferDynamic).setDescriptorCount(1000),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eStorageBufferDynamic).setDescriptorCount(1000),
            vk::DescriptorPoolSize().setType(vk::DescriptorType::eInputAttachment).setDescriptorCount(1000)};

        m_ImGuiPool = logicalDevice->createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo()
                                                                    .setMaxSets(Shaders::s_MAX_BINDLESS_SAMPLERS)
                                                                    .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
                                                                    .setPoolSizes(poolSizes));

        ImGui::CreateContext();

        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_HasGamepad;
        io.WantCaptureMouse    = true;
        io.WantCaptureKeyboard = false;
        io.WantTextInput       = false;

        RDNT_ASSERT(ImGui_ImplVulkan_LoadFunctions(
                        [](const char* functionName, void* vulkanInstance)
                        {
                            return GfxContext::Get().GetInstance().getDispatch().vkGetInstanceProcAddr(
                                *(reinterpret_cast<VkInstance*>(vulkanInstance)), functionName);
                        },
                        (void*)&(*m_GfxContext->GetInstance())),
                    "Failed to load functions into ImGui!");

        const auto& mainWindow = Application::Get().GetMainWindow();
        ImGui_ImplGlfw_InitForVulkan(mainWindow->Get(), true);

        const auto imageFormat             = m_GfxContext->GetSwapchainImageFormat();
        ImGui_ImplVulkan_InitInfo initInfo = {
            .Instance                    = *m_GfxContext->GetInstance(),
            .PhysicalDevice              = gfxDevice->GetPhysicalDevice(),
            .Device                      = *logicalDevice,
            .QueueFamily                 = gfxDevice->GetGeneralQueue().QueueFamilyIndex.value(),
            .Queue                       = gfxDevice->GetGeneralQueue().Handle,
            .DescriptorPool              = *m_ImGuiPool,
            .MinImageCount               = static_cast<std::uint32_t>(m_GfxContext->GetSwapchainImageCount()),
            .ImageCount                  = static_cast<std::uint32_t>(m_GfxContext->GetSwapchainImageCount()),
            .MSAASamples                 = VK_SAMPLE_COUNT_1_BIT,
            .PipelineCache               = gfxDevice->GetPipelineCache(),
            .UseDynamicRendering         = true,
            .PipelineRenderingCreateInfo = {.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                            .colorAttachmentCount    = 1,
                                            .pColorAttachmentFormats = (VkFormat*)&imageFormat},
            .CheckVkResultFn             = [](VkResult err) { RDNT_ASSERT(err == VK_SUCCESS, "ImGui issues!"); },
        };
        ImGui_ImplVulkan_Init(&initInfo);

        // Execute a GPU command to upload ImGui font textures.
        RDNT_ASSERT(ImGui_ImplVulkan_CreateFontsTexture(), "Failed to create fonts texture for ImGui!");
    }

    ImGuiRenderer::~ImGuiRenderer() noexcept
    {
        m_GfxContext->GetDevice()->WaitIdle();

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiRenderer::RenderFrame(const vk::Extent2D& viewportExtent, Unique<RenderGraph>& renderGraph, const std::string& backbufferName,
                                    std::function<void()>&& uiFunc) noexcept
    {
        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        renderGraph->AddPass(
            "ImGuiPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                m_ImGuiPassData.BackbufferTexture = scheduler.ReadTexture(backbufferName, EResourceState::RESOURCE_STATE_COPY_SOURCE);
                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(viewportExtent.width).setHeight(viewportExtent.height),
                    vk::Rect2D().setExtent(viewportExtent));
            },
            [&, uiFunc](RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& backBufferSrcTexture = scheduler.GetTexture(m_ImGuiPassData.BackbufferTexture);

                const auto backbufferImageSubresourceRange = vk::ImageSubresourceRange()
                                                                 .setBaseArrayLayer(0)
                                                                 .setLayerCount(1)
                                                                 .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                                 .setBaseMipLevel(0)
                                                                 .setLevelCount(1);
                cmd.pipelineBarrier2(
                    vk::DependencyInfo().setImageMemoryBarriers(vk::ImageMemoryBarrier2()
                                                                    .setImage(m_GfxContext->GetCurrentSwapchainImage())
                                                                    .setSubresourceRange(backbufferImageSubresourceRange)
                                                                    .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                                                                    .setSrcStageMask(vk::PipelineStageFlagBits2::eBottomOfPipe)
                                                                    .setOldLayout(vk::ImageLayout::eUndefined)
                                                                    .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                                                                    .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
                                                                    .setDstStageMask(vk::PipelineStageFlagBits2::eAllTransfer)));

                const auto backbufferImageSubresourceLayers = vk::ImageSubresourceLayers()
                                                                  .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                                  .setBaseArrayLayer(0)
                                                                  .setLayerCount(1)
                                                                  .setMipLevel(0)
                                                                  .setLayerCount(1);
                cmd.blitImage(
                    *backBufferSrcTexture, vk::ImageLayout::eTransferSrcOptimal, m_GfxContext->GetCurrentSwapchainImage(),
                    vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageBlit()
                        .setSrcSubresource(backbufferImageSubresourceLayers)
                        .setDstSubresource(backbufferImageSubresourceLayers)
                        .setSrcOffsets(
                            {vk::Offset3D(), vk::Offset3D(static_cast<int32_t>(backBufferSrcTexture->GetDescription().Dimensions.x),
                                                          static_cast<int32_t>(backBufferSrcTexture->GetDescription().Dimensions.y), 1)})
                        .setDstOffsets({vk::Offset3D(), vk::Offset3D(static_cast<int32_t>(viewportExtent.width),
                                                                     static_cast<int32_t>(viewportExtent.height), 1)}),
                    vk::Filter::eLinear);

                cmd.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                    vk::ImageMemoryBarrier2()
                        .setImage(m_GfxContext->GetCurrentSwapchainImage())
                        .setSubresourceRange(backbufferImageSubresourceRange)
                        .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
                        .setSrcStageMask(vk::PipelineStageFlagBits2::eAllTransfer)
                        .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                        .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eColorAttachmentRead)
                        .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)));

                const auto swapchainAttachmentInfo = vk::RenderingAttachmentInfo()
                                                         .setLoadOp(vk::AttachmentLoadOp::eLoad)
                                                         .setStoreOp(vk::AttachmentStoreOp::eStore)
                                                         .setImageView(*m_GfxContext->GetCurrentSwapchainImageView())
                                                         .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal);
                cmd.beginRendering(vk::RenderingInfo()
                                       .setColorAttachments(swapchainAttachmentInfo)
                                       .setLayerCount(1)
                                       .setRenderArea(vk::Rect2D().setExtent(viewportExtent)));

                uiFunc();

                // Rendering
                ImGui::EndFrame();
                ImGui::Render();
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

                cmd.endRendering();
                cmd.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                    vk::ImageMemoryBarrier2()
                        .setImage(m_GfxContext->GetCurrentSwapchainImage())
                        .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eColorAttachmentRead)
                        .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                        .setSubresourceRange(backbufferImageSubresourceRange)
                        .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                        .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
                        .setDstAccessMask(vk::AccessFlagBits2::eNone)
                        .setDstStageMask(vk::PipelineStageFlagBits2::eBottomOfPipe)));
            });
    }

}  // namespace Radiant
