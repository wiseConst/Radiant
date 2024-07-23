#include <pch.h>
#include "GfxContext.hpp"

#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(RDNT_WINDOWS)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(RDNT_LINUX)
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#elif defined(RDNT_MACOS)
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Radiant
{

    bool GfxContext::BeginFrame() noexcept
    {
        m_Device->PollDeletionQueues();

        if (m_bSwapchainNeedsResize)
        {
            m_Device->WaitIdle();
            InvalidateSwapchain();
            m_bSwapchainNeedsResize        = false;
            m_Device->m_CurrentFrameNumber = m_GlobalFrameNumber = 0;
            m_Device->PollDeletionQueues(true);

            return false;
        }

        RDNT_ASSERT(m_Device->GetLogicalDevice()->waitForFences(*m_FrameData[m_CurrentFrameIndex].RenderFinishedFence, vk::True,
                                                                UINT64_MAX) == vk::Result::eSuccess,
                    "{}", __FUNCTION__);
        m_Device->GetLogicalDevice()->resetFences(*m_FrameData[m_CurrentFrameIndex].RenderFinishedFence);

        // NOTE: Apparently on NV cards this throws vk::OutOfDateKHRError.
        try
        {
            const auto [result, imageIndex] = m_Device->GetLogicalDevice()->acquireNextImageKHR(
                *m_Swapchain, UINT64_MAX, *m_FrameData[m_CurrentFrameIndex].ImageAvailableSemaphore);

            if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
            {
                m_bSwapchainNeedsResize = true;
                return false;
            }
            else if (result != vk::Result::eSuccess)
            {
                RDNT_ASSERT(false, "acquireNextImageKHR(): unknown result!");
            }
            m_CurrentImageIndex = imageIndex;
        }
        catch (vk::OutOfDateKHRError)
        {
            m_bSwapchainNeedsResize = true;
            return false;
        }

        m_Device->GetLogicalDevice()->resetCommandPool(*m_FrameData[m_CurrentFrameIndex].GeneralCommandPool);
        return true;
    }

    void GfxContext::EndFrame() noexcept
    {
        // NOTE: In future I might upscale(compute) or load into swapchain image or render into so here's optimal flags.
        const vk::PipelineStageFlags waitDstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                                        vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eComputeShader |
                                                        vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                                        vk::PipelineStageFlagBits::eLateFragmentTests;

        const auto& presentQueue = m_Device->GetPresentQueue().Handle;
        presentQueue.submit(vk::SubmitInfo()
                                .setCommandBuffers(m_FrameData[m_CurrentFrameIndex].GeneralCommandBuffer)
                                .setSignalSemaphores(*m_FrameData[m_CurrentFrameIndex].RenderFinishedSemaphore)
                                .setWaitSemaphores(*m_FrameData[m_CurrentFrameIndex].ImageAvailableSemaphore)
                                .setWaitDstStageMask(waitDstStageMask),
                            *m_FrameData[m_CurrentFrameIndex].RenderFinishedFence);

        // NOTE: Apparently on NV cards this throws vk::OutOfDateKHRError.
        try
        {
            auto result = m_Device->GetPresentQueue().Handle.presentKHR(
                vk::PresentInfoKHR()
                    .setImageIndices(m_CurrentImageIndex)
                    .setSwapchains(*m_Swapchain)
                    .setWaitSemaphores(*m_FrameData[m_CurrentFrameIndex].RenderFinishedSemaphore));

            if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
            {
                m_bSwapchainNeedsResize = true;
            }
            else if (result != vk::Result::eSuccess)
            {
                RDNT_ASSERT(false, "presentKHR(): unknown result!");
            }
        }
        catch (vk::OutOfDateKHRError)
        {
            m_bSwapchainNeedsResize = true;
        }

        m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % s_BufferedFrameCount;
        ++m_Device->m_CurrentFrameNumber;
        ++m_GlobalFrameNumber;
    }

    void GfxContext::Init() noexcept
    {
        RDNT_ASSERT(!s_Instance, "GfxContext already exists!");
        s_Instance = this;
        LOG_INFO("{}", __FUNCTION__);

        CreateInstanceAndDebugUtilsMessenger();
        CreateSurface();
        m_Device = MakeUnique<GfxDevice>(m_Instance, m_Surface);

        InvalidateSwapchain();
        CreateFrameResources();
    }

    void GfxContext::CreateInstanceAndDebugUtilsMessenger() noexcept
    {
        // Initialize minimal set of function pointers.
        VULKAN_HPP_DEFAULT_DISPATCHER.init();

        std::vector<const char*> enabledInstanceLayers;
        std::vector<const char*> enabledInstanceExtensions;

        if constexpr (RDNT_DEBUG || s_bForceGfxValidation)
        {
            enabledInstanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            enabledInstanceLayers.emplace_back("VK_LAYER_KHRONOS_validation");
        }

        const auto windowExtensions = Application::Get().GetMainWindow()->GetRequiredExtensions();
        enabledInstanceExtensions.insert(enabledInstanceExtensions.end(), windowExtensions.begin(), windowExtensions.end());

        // Simple safety check if our layers/extensions are supported.
        const auto instanceExtensions = vk::enumerateInstanceExtensionProperties();
        for (const auto& eie : enabledInstanceExtensions)
        {
            bool bExtensionSupported{false};
            for (const auto& ie : instanceExtensions)
            {
                if (strcmp(eie, ie.extensionName.data()) != 0) continue;

                bExtensionSupported = true;
                break;
            }
            RDNT_ASSERT(bExtensionSupported, "Unsupported extension: {} ", eie);
        }

        const auto instanceLayers = vk::enumerateInstanceLayerProperties();
        for (const auto& eil : enabledInstanceLayers)
        {
            bool bLayerSupported{false};
            for (const auto& il : instanceLayers)
            {
                // LOG_TRACE("{}", il.layerName.data());
                if (strcmp(eil, il.layerName.data()) != 0) continue;

                bLayerSupported = true;
                break;
            }
            RDNT_ASSERT(bLayerSupported, "Unsupported layer: {} ", eil);
        }

        const std::uint32_t apiVersion = vk::enumerateInstanceVersion();
        RDNT_ASSERT(apiVersion >= VK_API_VERSION_1_3, "Old vulkan API version! Required at least 1.3!");
        const auto appInfo = vk::ApplicationInfo()
                                 .setPApplicationName(s_ENGINE_NAME)
                                 .setApplicationVersion(VK_MAKE_VERSION(1, 0, 0))
                                 .setPEngineName(s_ENGINE_NAME)
                                 .setEngineVersion(VK_MAKE_VERSION(1, 0, 0))
                                 .setApiVersion(apiVersion);

        m_Instance = vk::createInstanceUnique(vk::InstanceCreateInfo()
                                                  .setPApplicationInfo(&appInfo)
                                                  .setEnabledExtensionCount(enabledInstanceExtensions.size())
                                                  .setPEnabledExtensionNames(enabledInstanceExtensions)
                                                  .setEnabledLayerCount(enabledInstanceLayers.size())
                                                  .setPEnabledLayerNames(enabledInstanceLayers));

        LOG_TRACE("VkInstance {}.{}.{} created.", VK_VERSION_MAJOR(apiVersion), VK_VERSION_MINOR(apiVersion), VK_VERSION_PATCH(apiVersion));

        // Load other set of function pointers.
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*m_Instance);

        // Creating debug utils messenger.
        if constexpr (RDNT_DEBUG || s_bForceGfxValidation)
        {
            constexpr auto debugCallback =
                [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                   const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, MAYBE_UNUSED void* pUserData) noexcept -> VkBool32
            {
                switch (messageSeverity)
                {
                    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: LOG_TRACE("{}", pCallbackData->pMessage); break;
                    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: LOG_INFO("{}", pCallbackData->pMessage); break;
                    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: LOG_WARN("{}", pCallbackData->pMessage); break;
                    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: LOG_ERROR("{}", pCallbackData->pMessage); break;
                }

                return VK_FALSE;
            };

            constexpr auto dumCI =
                vk::DebugUtilsMessengerCreateInfoEXT()
                    .setPfnUserCallback(debugCallback)
                    .setMessageSeverity(
                        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError /*|vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo*/ |
                        vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose)
                    .setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                                    vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                                    vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding);
            m_DebugUtilsMessenger = m_Instance->createDebugUtilsMessengerEXTUnique(dumCI);
        }
    }

    void GfxContext::CreateSurface() noexcept
    {
        const auto& mainWindow = Application::Get().GetMainWindow();
#ifdef RDNT_WINDOWS
        m_Surface = m_Instance->createWin32SurfaceKHRUnique(
            vk::Win32SurfaceCreateInfoKHR().setHwnd(glfwGetWin32Window(mainWindow->Get())).setHinstance(GetModuleHandle(nullptr)));
// #elif defined(RDNT_LINUX)
#else
#error Do override surface khr creation on other platforms
#endif
    }

    void GfxContext::CreateFrameResources() noexcept
    {
        constexpr std::array<vk::DescriptorSetLayoutBinding, 3> bindings{vk::DescriptorSetLayoutBinding()
                                                                             .setBinding(0)
                                                                             .setDescriptorCount(Shaders::s_MAX_BINDLESS_IMAGES)
                                                                             .setStageFlags(vk::ShaderStageFlagBits::eAll)
                                                                             .setDescriptorType(vk::DescriptorType::eStorageImage),
                                                                         vk::DescriptorSetLayoutBinding()
                                                                             .setBinding(1)
                                                                             .setDescriptorCount(Shaders::s_MAX_BINDLESS_TEXTURES)
                                                                             .setStageFlags(vk::ShaderStageFlagBits::eAll)
                                                                             .setDescriptorType(vk::DescriptorType::eCombinedImageSampler),
                                                                         vk::DescriptorSetLayoutBinding()
                                                                             .setBinding(2)
                                                                             .setDescriptorCount(Shaders::s_MAX_BINDLESS_SAMPLERS)
                                                                             .setStageFlags(vk::ShaderStageFlagBits::eAll)
                                                                             .setDescriptorType(vk::DescriptorType::eSampler)};

        constexpr std::array<vk::DescriptorBindingFlags, 3> bindingFlags{
            vk::FlagTraits<vk::DescriptorBindingFlagBits>::allFlags ^ vk::DescriptorBindingFlagBits::eVariableDescriptorCount,
            vk::FlagTraits<vk::DescriptorBindingFlagBits>::allFlags ^ vk::DescriptorBindingFlagBits::eVariableDescriptorCount,
            vk::FlagTraits<vk::DescriptorBindingFlagBits>::allFlags ^ vk::DescriptorBindingFlagBits::eVariableDescriptorCount};
        const auto megaSetLayoutExtendedInfo = vk::DescriptorSetLayoutBindingFlagsCreateInfo().setBindingFlags(bindingFlags);

        m_DescriptorSetLayout = m_Device->GetLogicalDevice()->createDescriptorSetLayoutUnique(
            vk::DescriptorSetLayoutCreateInfo()
                .setBindings(bindings)
                .setFlags(vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool)
                .setPNext(&megaSetLayoutExtendedInfo));

        m_PipelineLayout = m_Device->GetLogicalDevice()->createPipelineLayoutUnique(
            vk::PipelineLayoutCreateInfo()
                .setSetLayouts(*m_DescriptorSetLayout)
                .setPushConstantRanges(vk::PushConstantRange()
                                           .setOffset(0)
                                           .setSize(/* guaranteed by the spec min bytes size of maxPushConstantsSize */ 128)
                                           .setStageFlags(vk::ShaderStageFlagBits::eAll)));

        constexpr std::array<vk::DescriptorPoolSize, 3> poolSizes{
            vk::DescriptorPoolSize().setDescriptorCount(Shaders::s_MAX_BINDLESS_IMAGES).setType(vk::DescriptorType::eStorageImage),
            vk::DescriptorPoolSize()
                .setDescriptorCount(Shaders::s_MAX_BINDLESS_TEXTURES)
                .setType(vk::DescriptorType::eCombinedImageSampler),
            vk::DescriptorPoolSize().setDescriptorCount(Shaders::s_MAX_BINDLESS_SAMPLERS).setType(vk::DescriptorType::eSampler)};
        for (std::uint8_t i{}; i < s_BufferedFrameCount; ++i)
        {
            m_FrameData[i].GeneralCommandPool = m_Device->GetLogicalDevice()->createCommandPoolUnique(
                vk::CommandPoolCreateInfo().setQueueFamilyIndex(*m_Device->GetGeneralQueue().QueueFamilyIndex));

            m_FrameData[i].AsyncComputeCommandPool = m_Device->GetLogicalDevice()->createCommandPoolUnique(
                vk::CommandPoolCreateInfo().setQueueFamilyIndex(*m_Device->GetComputeQueue().QueueFamilyIndex));

            m_FrameData[i].DedicatedTransferCommandPool = m_Device->GetLogicalDevice()->createCommandPoolUnique(
                vk::CommandPoolCreateInfo().setQueueFamilyIndex(*m_Device->GetTransferQueue().QueueFamilyIndex));

            m_FrameData[i].GeneralCommandBuffer = m_Device->GetLogicalDevice()
                                                      ->allocateCommandBuffers(vk::CommandBufferAllocateInfo()
                                                                                   .setCommandBufferCount(1)
                                                                                   .setCommandPool(*m_FrameData[i].GeneralCommandPool)
                                                                                   .setLevel(vk::CommandBufferLevel::ePrimary))
                                                      .back();

            m_FrameData[i].RenderFinishedFence =
                m_Device->GetLogicalDevice()->createFenceUnique(vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled));
            m_FrameData[i].ImageAvailableSemaphore = m_Device->GetLogicalDevice()->createSemaphoreUnique(vk::SemaphoreCreateInfo());
            m_FrameData[i].RenderFinishedSemaphore = m_Device->GetLogicalDevice()->createSemaphoreUnique(vk::SemaphoreCreateInfo());

            m_FrameData[i].DescriptorPool =
                m_Device->GetLogicalDevice()->createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo()
                                                                             .setMaxSets(1)
                                                                             .setFlags(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind)
                                                                             .setPoolSizes(poolSizes));

            m_FrameData[i].DescriptorSet = m_Device->GetLogicalDevice()
                                               ->allocateDescriptorSets(vk::DescriptorSetAllocateInfo()
                                                                            .setDescriptorPool(*m_FrameData[i].DescriptorPool)
                                                                            .setSetLayouts(*m_DescriptorSetLayout))
                                               .back();
        }
    }

    void GfxContext::InvalidateSwapchain() noexcept
    {
        // Render finished fences also need to be recreated cuz they're stalling the CPU.
        for (std::uint8_t i{}; i < s_BufferedFrameCount; ++i)
        {
            m_FrameData[i].RenderFinishedFence =
                m_Device->GetLogicalDevice()->createFenceUnique(vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled));
        }
        m_CurrentImageIndex = m_CurrentFrameIndex = 0;

        const auto& window = Application::Get().GetMainWindow();
        const auto& extent = window->GetDescription().Extent;

        const std::vector<vk::SurfaceFormatKHR> availableSurfaceFormats = m_Device->GetPhysicalDevice().getSurfaceFormatsKHR(*m_Surface);
        RDNT_ASSERT(!availableSurfaceFormats.empty(), "No surface formats present?!");

        const auto imageFormat =
            availableSurfaceFormats[0].format == vk::Format::eUndefined ? vk::Format::eB8G8R8A8Unorm : availableSurfaceFormats[0].format;
        const vk::SurfaceCapabilitiesKHR availableSurfaceCapabilities = m_Device->GetPhysicalDevice().getSurfaceCapabilitiesKHR(*m_Surface);
        constexpr auto requestedImageUsageFlags = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
        RDNT_ASSERT((availableSurfaceCapabilities.supportedUsageFlags & requestedImageUsageFlags) == requestedImageUsageFlags,
                    "Swapchain's supportedUsageFlags != requestedImageUsageFlags.");

        // If the surface size is defined, the swap chain size must match
        m_SwapchainExtent = availableSurfaceCapabilities.currentExtent;
        if (m_SwapchainExtent.width == std::numeric_limits<std::uint32_t>::max() ||
            m_SwapchainExtent.height == std::numeric_limits<std::uint32_t>::max())
        {
            // If the surface size is undefined, the size is set to the size of the images requested.
            m_SwapchainExtent.width =
                std::clamp(extent.x, availableSurfaceCapabilities.minImageExtent.width, availableSurfaceCapabilities.maxImageExtent.width);
            m_SwapchainExtent.height = std::clamp(extent.y, availableSurfaceCapabilities.minImageExtent.height,
                                                  availableSurfaceCapabilities.maxImageExtent.height);
        }

        const auto preTransform = (availableSurfaceCapabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)
                                      ? vk::SurfaceTransformFlagBitsKHR::eIdentity
                                      : availableSurfaceCapabilities.currentTransform;

        const auto compositeAlpha =
            (availableSurfaceCapabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
                ? vk::CompositeAlphaFlagBitsKHR::ePreMultiplied
            : (availableSurfaceCapabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied)
                ? vk::CompositeAlphaFlagBitsKHR::ePostMultiplied
            : (availableSurfaceCapabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit)
                ? vk::CompositeAlphaFlagBitsKHR::eInherit
                : vk::CompositeAlphaFlagBitsKHR::eOpaque;

        // The FIFO present mode is guaranteed by the spec.
        const auto presentMode = vk::PresentModeKHR::eFifo;

        m_SwapchainImageFormat = imageFormat;
        auto swapchainCI =
            vk::SwapchainCreateInfoKHR()
                .setSurface(*m_Surface)
                .setImageSharingMode(vk::SharingMode::eExclusive)
                .setQueueFamilyIndexCount(1)
                .setPQueueFamilyIndices(&m_Device->GetGeneralQueue().QueueFamilyIndex.value())
                .setCompositeAlpha(compositeAlpha)
                .setPresentMode(presentMode)
                .setImageFormat(imageFormat)
                .setImageExtent(m_SwapchainExtent)
                .setImageArrayLayers(1)
                .setClipped(vk::True)
                .setMinImageCount(std::clamp(3u, availableSurfaceCapabilities.minImageCount, availableSurfaceCapabilities.maxImageCount))
                .setImageColorSpace(vk::ColorSpaceKHR::eSrgbNonlinear)
                .setImageUsage(requestedImageUsageFlags);
        const std::array<std::uint32_t, 2> queueFamilyIndices{*m_Device->GetGeneralQueue().QueueFamilyIndex,
                                                              *m_Device->GetPresentQueue().QueueFamilyIndex};
        if (m_Device->GetGeneralQueue().QueueFamilyIndex != m_Device->GetPresentQueue().QueueFamilyIndex)
        {
            // If the graphics and present queues are from different queue families, we either have to explicitly transfer
            // ownership of images between the queues, or we have to create the swapchain with imageSharingMode as
            // VK_SHARING_MODE_CONCURRENT
            swapchainCI.setImageSharingMode(vk::SharingMode::eConcurrent)
                .setQueueFamilyIndexCount(2)
                .setPQueueFamilyIndices(queueFamilyIndices.data());
        }

        auto oldSwapchain = std::move(m_Swapchain);
        if (oldSwapchain)
        {
            m_SwapchainImages.clear();
            m_SwapchainImageViews.clear();
            swapchainCI.setOldSwapchain(*oldSwapchain);
        }

        m_Swapchain = m_Device->GetLogicalDevice()->createSwapchainKHRUnique(swapchainCI);

        m_SwapchainImages = m_Device->GetLogicalDevice()->getSwapchainImagesKHR(*m_Swapchain);
        m_SwapchainImageViews.reserve(m_SwapchainImages.size());
        vk::ImageViewCreateInfo imageViewCreateInfo({}, {}, vk::ImageViewType::e2D, imageFormat, {},
                                                    {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        for (std::uint32_t i{}; i < m_SwapchainImages.size(); ++i)
        {
            imageViewCreateInfo.image = m_SwapchainImages[i];
            m_SwapchainImageViews.emplace_back(m_Device->GetLogicalDevice()->createImageViewUnique(imageViewCreateInfo));

            const std::string swapchainImageName = "SwapchainImage[" + std::to_string(i) + "]";
            m_Device->SetDebugName(swapchainImageName, m_SwapchainImages[i]);

            const std::string swapchainImageViewName = "SwapchainImageView[" + std::to_string(i) + "]";
            m_Device->SetDebugName(swapchainImageViewName, *m_SwapchainImageViews[i]);
        }
    }

    void GfxContext::Shutdown() noexcept
    {
        LOG_INFO("{}", __FUNCTION__);
    }

}  // namespace Radiant
