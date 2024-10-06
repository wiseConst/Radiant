#include <pch.hpp>
#include "ImGuiRenderer.hpp"

#include <Render/GfxContext.hpp>
#include <Render/GfxDevice.hpp>
#include <Render/GfxPipeline.hpp>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <Core/Application.hpp>

namespace Radiant
{

    struct ImGuiPassData
    {
        RGResourceID BackbufferTexture;
    } static s_ImGuiPassData = {};

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

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_HasGamepad;
        io.WantCaptureMouse    = true;
        io.WantCaptureKeyboard = false;
        io.WantTextInput       = false;

        auto& imGuiStyle         = ImGui::GetStyle();
        imGuiStyle.ChildRounding = imGuiStyle.GrabRounding = imGuiStyle.ScrollbarRounding = imGuiStyle.TabRounding =
            imGuiStyle.PopupRounding = imGuiStyle.WindowRounding = imGuiStyle.FrameRounding = 8.0f;

        ImGui::StyleColorsDark();
        RDNT_ASSERT(ImGui_ImplVulkan_LoadFunctions(
                        [](const char* functionName, void* vulkanInstance)
                        {
                            return GfxContext::Get().GetInstance().getDispatch().vkGetInstanceProcAddr(
                                *(reinterpret_cast<vk::Instance*>(vulkanInstance)), functionName);
                        },
                        (void*)&(*m_GfxContext->GetInstance())),
                    "Failed to load functions into ImGui!");
        ImGui_ImplGlfw_InitForVulkan(Application::Get().GetMainWindow()->Get(), true);

        const auto imageFormat             = m_GfxContext->GetSwapchainImageFormat();
        ImGui_ImplVulkan_InitInfo initInfo = {
            .Instance                    = *m_GfxContext->GetInstance(),
            .PhysicalDevice              = gfxDevice->GetPhysicalDevice(),
            .Device                      = *logicalDevice,
            .QueueFamily                 = gfxDevice->GetGeneralQueue().QueueFamilyIndex,
            .Queue                       = gfxDevice->GetGeneralQueue().Handle,
            .DescriptorPool              = *m_ImGuiPool,
            .MinImageCount               = static_cast<u32>(m_GfxContext->GetSwapchainImageCount()),
            .ImageCount                  = static_cast<u32>(m_GfxContext->GetSwapchainImageCount()),
            .MSAASamples                 = VK_SAMPLE_COUNT_1_BIT,
            .PipelineCache               = gfxDevice->GetPipelineCache(),
            .UseDynamicRendering         = true,
            .PipelineRenderingCreateInfo = {.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                            .colorAttachmentCount    = 1,
                                            .pColorAttachmentFormats = (VkFormat*)&imageFormat},
            .CheckVkResultFn             = [](VkResult err) { RDNT_ASSERT(err == VK_SUCCESS, "ImGui issues!"); },
        };
        ImGui_ImplVulkan_Init(&initInfo);

        const std::string defaultFontPath = "../Assets/Fonts/Signika_Negative/static/SignikaNegative-SemiBold.ttf";
        io.FontDefault                    = io.Fonts->AddFontFromFileTTF(defaultFontPath.data(), 18.0f);
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
        // Blit into swapchain + render UI into swapchain.
        renderGraph->AddPass(
            "ImGuiPass", ERenderGraphPassType::RENDER_GRAPH_PASS_TYPE_GRAPHICS,
            [&](RenderGraphResourceScheduler& scheduler)
            {
                s_ImGuiPassData.BackbufferTexture =
                    scheduler.ReadTexture(backbufferName, MipSet::FirstMip(), EResourceStateBits::RESOURCE_STATE_COPY_SOURCE_BIT);
                scheduler.SetViewportScissors(
                    vk::Viewport().setMinDepth(0.0f).setMaxDepth(1.0f).setWidth(viewportExtent.width).setHeight(viewportExtent.height),
                    vk::Rect2D().setExtent(viewportExtent));
            },
            [&, uiFunc](const RenderGraphResourceScheduler& scheduler, const vk::CommandBuffer& cmd)
            {
                auto& backBufferSrcTexture = scheduler.GetTexture(s_ImGuiPassData.BackbufferTexture);
                RDNT_ASSERT(!GfxTexture::IsDepthFormat(backBufferSrcTexture->GetDescription().Format),
                            "Backbuffer image for swapchain blit should have color format!");

                constexpr auto backbufferImageSubresourceRange = vk::ImageSubresourceRange()
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
                                                                    .setDstStageMask(vk::PipelineStageFlagBits2::eBlit)));

                constexpr auto backbufferImageSubresourceLayers = vk::ImageSubresourceLayers()
                                                                      .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                                                      .setBaseArrayLayer(0)
                                                                      .setLayerCount(1)
                                                                      .setMipLevel(0)
                                                                      .setLayerCount(1);
                cmd.blitImage2(
                    vk::BlitImageInfo2()
                        .setFilter(vk::Filter::eLinear)
                        .setSrcImage(*backBufferSrcTexture)
                        .setSrcImageLayout(vk::ImageLayout::eTransferSrcOptimal)
                        .setDstImage(m_GfxContext->GetCurrentSwapchainImage())
                        .setDstImageLayout(vk::ImageLayout::eTransferDstOptimal)
                        .setRegions(
                            vk::ImageBlit2()
                                .setSrcSubresource(backbufferImageSubresourceLayers)
                                .setDstSubresource(backbufferImageSubresourceLayers)
                                .setSrcOffsets({vk::Offset3D(),
                                                vk::Offset3D(static_cast<i32>(backBufferSrcTexture->GetDescription().Dimensions.x),
                                                             static_cast<i32>(backBufferSrcTexture->GetDescription().Dimensions.y), 1)})
                                .setDstOffsets({vk::Offset3D(), vk::Offset3D(static_cast<i32>(viewportExtent.width),
                                                                             static_cast<i32>(viewportExtent.height), 1)})));

                cmd.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                    vk::ImageMemoryBarrier2()
                        .setImage(m_GfxContext->GetCurrentSwapchainImage())
                        .setSubresourceRange(backbufferImageSubresourceRange)
                        .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
                        .setSrcStageMask(vk::PipelineStageFlagBits2::eBlit)
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

                // Start the Dear ImGui frame
                ImGui_ImplVulkan_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();

                uiFunc();

                ImGui::Render();
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

                cmd.endRendering();
                cmd.pipelineBarrier2(vk::DependencyInfo()
                                         .setDependencyFlags(vk::DependencyFlagBits::eByRegion)
                                         .setImageMemoryBarriers(vk::ImageMemoryBarrier2()
                                                                     .setImage(m_GfxContext->GetCurrentSwapchainImage())
                                                                     .setSubresourceRange(backbufferImageSubresourceRange)
                                                                     .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite |
                                                                                       vk::AccessFlagBits2::eColorAttachmentRead)
                                                                     .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                                                                     .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                                                     .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
                                                                     .setDstAccessMask(vk::AccessFlagBits2::eNone)
                                                                     .setDstStageMask(vk::PipelineStageFlagBits2::eTopOfPipe)));
            });
    }

}  // namespace Radiant
