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

        // Custom style.
        {
            auto& colors                           = ImGui::GetStyle().Colors;
            colors[ImGuiCol_Text]                  = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
            colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
            colors[ImGuiCol_WindowBg]              = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
            colors[ImGuiCol_ChildBg]               = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_PopupBg]               = ImVec4(0.19f, 0.19f, 0.19f, 0.92f);
            colors[ImGuiCol_Border]                = ImVec4(0.19f, 0.19f, 0.19f, 0.29f);
            colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
            colors[ImGuiCol_FrameBg]               = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
            colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
            colors[ImGuiCol_FrameBgActive]         = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
            colors[ImGuiCol_TitleBg]               = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
            colors[ImGuiCol_TitleBgActive]         = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
            colors[ImGuiCol_MenuBarBg]             = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
            colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
            colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
            colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
            colors[ImGuiCol_CheckMark]             = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
            colors[ImGuiCol_SliderGrab]            = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
            colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
            colors[ImGuiCol_Button]                = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
            colors[ImGuiCol_ButtonHovered]         = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
            colors[ImGuiCol_ButtonActive]          = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
            colors[ImGuiCol_Header]                = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
            colors[ImGuiCol_HeaderHovered]         = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
            colors[ImGuiCol_HeaderActive]          = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
            colors[ImGuiCol_Separator]             = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
            colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
            colors[ImGuiCol_SeparatorActive]       = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
            colors[ImGuiCol_ResizeGrip]            = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
            colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
            colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
            colors[ImGuiCol_Tab]                   = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
            colors[ImGuiCol_TabHovered]            = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
            colors[ImGuiCol_TabActive]             = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
            colors[ImGuiCol_TabUnfocused]          = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
            colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
            colors[ImGuiCol_PlotLines]             = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
            colors[ImGuiCol_PlotLinesHovered]      = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
            colors[ImGuiCol_PlotHistogram]         = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
            colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
            colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
            colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
            colors[ImGuiCol_TableBorderLight]      = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
            colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
            colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
            colors[ImGuiCol_DragDropTarget]        = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
            colors[ImGuiCol_NavHighlight]          = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
            colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
            colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(1.00f, 0.00f, 0.00f, 0.20f);
            colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(1.00f, 0.00f, 0.00f, 0.35f);

            auto& style             = ImGui::GetStyle();
            style.WindowPadding     = ImVec2(8.00f, 8.00f);
            style.FramePadding      = ImVec2(5.00f, 2.00f);
            style.CellPadding       = ImVec2(6.00f, 6.00f);
            style.ItemSpacing       = ImVec2(6.00f, 6.00f);
            style.ItemInnerSpacing  = ImVec2(6.00f, 6.00f);
            style.TouchExtraPadding = ImVec2(0.00f, 0.00f);
            style.IndentSpacing     = 25;
            style.ScrollbarSize     = 15;
            style.GrabMinSize       = 10;
            style.WindowBorderSize  = 1;
            style.ChildBorderSize   = 1;
            style.PopupBorderSize   = 1;
            style.FrameBorderSize   = 1;
            style.TabBorderSize     = 1;
            style.WindowRounding    = 8;
            style.ChildRounding     = 4;
            style.FrameRounding     = 3;
            style.PopupRounding     = 4;
            style.ScrollbarRounding = 9;
            style.GrabRounding      = 3;
            style.LogSliderDeadzone = 4;
            style.TabRounding       = 4;
        }

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
            "ImGuiPass", ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL,
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
