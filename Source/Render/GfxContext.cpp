#include "GfxContext.hpp"

#include <Render/GfxTexture.hpp>
#include <Render/GfxBuffer.hpp>
#include <Render/GfxPipeline.hpp>

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

    void GfxPipelineStateCache::Bind(const vk::CommandBuffer& cmd, GfxPipeline* pipeline) noexcept
    {
        RDNT_ASSERT(pipeline, "GfxPipelineStateCache: Pipeline is invalid!");
        RDNT_ASSERT(!std::holds_alternative<std::monostate>(pipeline->GetDescription().PipelineOptions),
                    "GfxPipelineStateCache: Pipeline holds invalid options!");

        // Every pipeline bind invalidates the whole state.
        Invalidate(nullptr);

        std::scoped_lock lock(m_Mtx);
        if (LastBoundPipeline == pipeline) return;

        vk::PipelineBindPoint pipelineBindPoint{vk::PipelineBindPoint::eGraphics};
        if (std::holds_alternative<GfxGraphicsPipelineOptions>(pipeline->GetDescription().PipelineOptions))
            pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        else if (std::holds_alternative<GfxComputePipelineOptions>(pipeline->GetDescription().PipelineOptions))
            pipelineBindPoint = vk::PipelineBindPoint::eCompute;
        else if (std::holds_alternative<GfxRayTracingPipelineOptions>(pipeline->GetDescription().PipelineOptions))
            pipelineBindPoint = vk::PipelineBindPoint::eRayTracingKHR;
        else
            RDNT_ASSERT(false, "Pipeline holds no options?!");

        cmd.bindPipeline(pipelineBindPoint, *pipeline);
        LastBoundPipeline = pipeline;
        LastUsedCmd       = (vk::CommandBuffer*)&cmd;
    }

    void GfxPipelineStateCache::Bind(const vk::CommandBuffer& cmd, GfxBuffer* indexBuffer, const vk::DeviceSize offset,
                                     const vk::IndexType indexType) noexcept
    {
        RDNT_ASSERT(indexBuffer, "GfxPipelineStateCache: IndexBuffer is invalid!");

        Invalidate(&cmd);

        std::scoped_lock lock(m_Mtx);
        if (LastBoundIndexBuffer == indexBuffer && LastBoundIndexBufferOffset.has_value() && *LastBoundIndexBufferOffset == offset &&
            LastBoundIndexType.has_value() && *LastBoundIndexType == indexType)
            return;

        cmd.bindIndexBuffer(*indexBuffer, offset, indexType);
        LastBoundIndexBuffer       = indexBuffer;
        LastBoundIndexBufferOffset = offset;
        LastBoundIndexType         = indexType;
    }

    void GfxPipelineStateCache::Set(const vk::CommandBuffer& cmd, const vk::CullModeFlags cullMode) noexcept
    {
        Invalidate(&cmd);

        std::scoped_lock lock(m_Mtx);
        if (CullMode.has_value() && CullMode == cullMode) return;

        cmd.setCullMode(cullMode);
        CullMode = cullMode;
    }

    void GfxPipelineStateCache::Set(const vk::CommandBuffer& cmd, const vk::PrimitiveTopology primitiveTopology) noexcept
    {
        Invalidate(&cmd);

        std::scoped_lock lock(m_Mtx);
        if (PrimitiveTopology.has_value() && PrimitiveTopology == primitiveTopology) return;

        cmd.setPrimitiveTopology(primitiveTopology);
        PrimitiveTopology = primitiveTopology;
    }

    void GfxPipelineStateCache::Set(const vk::CommandBuffer& cmd, const vk::FrontFace frontFace) noexcept
    {
        Invalidate(&cmd);

        std::scoped_lock lock(m_Mtx);
        if (FrontFace.has_value() && FrontFace == frontFace) return;

        cmd.setFrontFace(frontFace);
        FrontFace = frontFace;
    }

    void GfxPipelineStateCache::Set(const vk::CommandBuffer& cmd, const vk::PolygonMode polygonMode) noexcept
    {
        Invalidate(&cmd);

        std::scoped_lock lock(m_Mtx);
        if (PolygonMode.has_value() && PolygonMode == polygonMode) return;

        cmd.setPolygonModeEXT(polygonMode);
        PolygonMode = polygonMode;
    }

    void GfxPipelineStateCache::Set(const vk::CommandBuffer& cmd, const vk::CompareOp compareOp) noexcept
    {
        Invalidate(&cmd);

        std::scoped_lock lock(m_Mtx);
        if (DepthCompareOp.has_value() && DepthCompareOp == compareOp) return;

        cmd.setDepthCompareOp(compareOp);
        DepthCompareOp = compareOp;
    }

    void GfxPipelineStateCache::SetDepthClamp(const vk::CommandBuffer& cmd, const bool bDepthClampEnable) noexcept
    {
        Invalidate(&cmd);

        std::scoped_lock lock(m_Mtx);
        if (bDepthClamp.has_value() && bDepthClamp == bDepthClampEnable) return;

        cmd.setDepthClampEnableEXT(bDepthClampEnable);
        bDepthClamp = bDepthClampEnable;
    }

    void GfxPipelineStateCache::SetStencilTest(const vk::CommandBuffer& cmd, const bool bStencilTestEnable) noexcept
    {
        Invalidate(&cmd);

        std::scoped_lock lock(m_Mtx);
        if (bStencilTest.has_value() && bStencilTest == bStencilTestEnable) return;

        cmd.setStencilTestEnable(bStencilTestEnable);
        bStencilTest = bStencilTestEnable;
    }

    void GfxPipelineStateCache::SetDepthTest(const vk::CommandBuffer& cmd, const bool bDepthTestEnable) noexcept
    {
        Invalidate(&cmd);

        std::scoped_lock lock(m_Mtx);
        if (bDepthTest.has_value() && bDepthTest == bDepthTestEnable) return;

        cmd.setDepthTestEnable(bDepthTestEnable);
        bDepthTest = bDepthTestEnable;
    }

    void GfxPipelineStateCache::SetDepthWrite(const vk::CommandBuffer& cmd, const bool bDepthWriteEnable) noexcept
    {
        Invalidate(&cmd);

        std::scoped_lock lock(m_Mtx);
        if (bDepthWrite.has_value() && bDepthWrite == bDepthWriteEnable) return;

        cmd.setDepthWriteEnable(bDepthWriteEnable);
        bDepthWrite = bDepthWriteEnable;
    }

    void GfxPipelineStateCache::SetDepthBounds(const vk::CommandBuffer& cmd, const glm::vec2& depthBounds) noexcept
    {
        Invalidate(&cmd);

        std::scoped_lock lock(m_Mtx);
        if (DepthBounds.has_value() && DepthBounds == depthBounds) return;

        cmd.setDepthBounds(depthBounds.x, depthBounds.y);
        DepthBounds = depthBounds;
    }

    bool GfxContext::BeginFrame() noexcept
    {
        if (m_bSwapchainNeedsResize)
        {
            m_Device->WaitIdle();
            InvalidateSwapchain();
            m_bSwapchainNeedsResize        = false;
            m_Device->m_CurrentFrameNumber = m_GlobalFrameNumber = 0;
            m_Device->PollDeletionQueues(true);

            return false;
        }

        auto& currentFrameData = m_FrameData[m_CurrentFrameIndex];
        {
            auto& cpuTask = m_FrameData[(m_CurrentFrameIndex - 1) % s_BufferedFrameCount].CPUProfilerData.emplace_back();
            cpuTask.StartTime =
                Timer::GetElapsedSecondsFromNow(m_FrameData[(m_CurrentFrameIndex - 1) % s_BufferedFrameCount].FrameStartTime);
            cpuTask.Name  = "WaitForFence";
            cpuTask.Color = Colors::ColorArray[1];

            RDNT_ASSERT(m_Device->GetLogicalDevice()->waitForFences(*currentFrameData.RenderFinishedFence, vk::True,
                                                                    std::numeric_limits<u64>::max()) == vk::Result::eSuccess,
                        "{}", __FUNCTION__);
            m_Device->GetLogicalDevice()->resetFences(*currentFrameData.RenderFinishedFence);

            cpuTask.EndTime = Timer::GetElapsedSecondsFromNow(m_FrameData[(m_CurrentFrameIndex - 1) % s_BufferedFrameCount].FrameStartTime);
        }

        // NOTE: Reset all states only after every GPU op finished!
        m_Device->PollDeletionQueues();

        m_Device->GetLogicalDevice()->resetCommandPool(*currentFrameData.GeneralCommandPoolVK);
        m_Device->GetLogicalDevice()->resetCommandPool(*currentFrameData.AsyncComputeCommandPoolVK);
        m_Device->GetLogicalDevice()->resetCommandPool(*currentFrameData.DedicatedTransferCommandPoolVK);

        m_PipelineStateCache.Invalidate();
        currentFrameData.CPUProfilerData.clear();
        currentFrameData.GPUProfilerData.clear();
        currentFrameData.FrameStartTime = Timer::Now();

        {
            auto& cpuTask = m_FrameData[(m_CurrentFrameIndex - 1) % s_BufferedFrameCount].CPUProfilerData.emplace_back();
            cpuTask.StartTime =
                Timer::GetElapsedSecondsFromNow(m_FrameData[(m_CurrentFrameIndex - 1) % s_BufferedFrameCount].FrameStartTime);
            cpuTask.Name  = "CollectGPUTimings";
            cpuTask.Color = Colors::pomegranate;
            if (currentFrameData.TimestampsQueryPool)
            {
                auto [result, data] = m_Device->GetLogicalDevice()->getQueryPoolResults<u64>(
                    *currentFrameData.TimestampsQueryPool, 0, currentFrameData.CurrentTimestampIndex,
                    sizeof(u64) * currentFrameData.TimestampsCapacity, sizeof(u64),
                    vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
                RDNT_ASSERT(result == vk::Result::eSuccess, "Failed to getQueryPoolResults()!");

                currentFrameData.TimestampResults = std::move(data);
                m_Device->GetLogicalDevice()->resetQueryPool(*currentFrameData.TimestampsQueryPool, 0, currentFrameData.TimestampsCapacity);

                // NOTE: CPUProfilerData is populated right when executing rendergraph, but with GPU things are different and we populate
                // it's timings right after it finished work for appropriate frame.
                auto& prevFrameGPUProfilerData = m_FrameData[(m_CurrentFrameIndex - 1) % s_BufferedFrameCount].GPUProfilerData;
                for (u32 taskIndex{}, timestampIndex{};
                     taskIndex < prevFrameGPUProfilerData.size() && (timestampIndex + 1) < currentFrameData.TimestampResults.size();
                     ++taskIndex, ++timestampIndex)
                {
                    const auto frequencyFactor = m_Device->GetGPUProperties().limits.timestampPeriod / 1e9;

                    prevFrameGPUProfilerData[taskIndex].StartTime =
                        (currentFrameData.TimestampResults[timestampIndex] - currentFrameData.TimestampResults[0]) * frequencyFactor;
                    prevFrameGPUProfilerData[taskIndex].EndTime =
                        (currentFrameData.TimestampResults[timestampIndex + 1] - currentFrameData.TimestampResults[0]) * frequencyFactor;
                }
            }
            currentFrameData.CurrentTimestampIndex = 0;
            cpuTask.EndTime = Timer::GetElapsedSecondsFromNow(m_FrameData[(m_CurrentFrameIndex - 1) % s_BufferedFrameCount].FrameStartTime);
        }

        // NOTE: Apparently on NV cards this throws vk::OutOfDateKHRError.
        try
        {
            const auto [result, imageIndex] =
                m_Device->GetLogicalDevice()->acquireNextImageKHR(*m_Swapchain, UINT64_MAX, *currentFrameData.ImageAvailableSemaphore);

            if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
            {
                m_bSwapchainNeedsResize = true;
                return false;
            }
            else if (result != vk::Result::eSuccess)
                RDNT_ASSERT(false, "acquireNextImageKHR(): unknown result!");

            m_CurrentImageIndex = imageIndex;
        }
        catch (const vk::OutOfDateKHRError&)
        {
            m_bSwapchainNeedsResize = true;
            return false;
        }

        return true;
    }

    void GfxContext::EndFrame() noexcept
    {
        // NOTE: Apparently on NV cards this throws vk::OutOfDateKHRError.
        try
        {
            auto& cpuTask     = m_FrameData[m_CurrentFrameIndex].CPUProfilerData.emplace_back();
            cpuTask.StartTime = Timer::GetElapsedSecondsFromNow(m_FrameData[m_CurrentFrameIndex].FrameStartTime);
            cpuTask.Name      = "SwapchainPresent";
            cpuTask.Color     = Colors::ColorArray[0];

            const auto result = m_Device->GetGeneralQueue().Handle.presentKHR(
                vk::PresentInfoKHR()
                    .setImageIndices(m_CurrentImageIndex)
                    .setSwapchains(*m_Swapchain)
                    .setWaitSemaphores(*m_FrameData[m_CurrentFrameIndex].RenderFinishedSemaphore));

            cpuTask.EndTime = Timer::GetElapsedSecondsFromNow(m_FrameData[m_CurrentFrameIndex].FrameStartTime);

            if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
                m_bSwapchainNeedsResize = true;
            else if (result != vk::Result::eSuccess)
                RDNT_ASSERT(false, "presentKHR(): unknown result!");
        }
        catch (const vk::OutOfDateKHRError&)
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

        const u32 apiVersion = vk::enumerateInstanceVersion();
        RDNT_ASSERT(apiVersion >= VK_API_VERSION_1_3, "Old vulkan API version! Required at least 1.3!");
        const auto appInfo = vk::ApplicationInfo()
                                 .setPApplicationName(s_ENGINE_NAME)
                                 .setApplicationVersion(VK_MAKE_VERSION(1, 0, 0))
                                 .setPEngineName(s_ENGINE_NAME)
                                 .setEngineVersion(VK_MAKE_VERSION(1, 0, 0))
                                 .setApiVersion(apiVersion);

        constexpr auto validationFeatureToEnable = vk::ValidationFeatureEnableEXT::eDebugPrintf;
        const auto validationInfo                = vk::ValidationFeaturesEXT().setEnabledValidationFeatures(validationFeatureToEnable);
        m_Instance                               = vk::createInstanceUnique(vk::InstanceCreateInfo()
                                                                                .setPNext(s_bShaderDebugPrintf ? &validationInfo : nullptr)
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

            m_DebugUtilsMessenger = m_Instance->createDebugUtilsMessengerEXTUnique(
                vk::DebugUtilsMessengerCreateInfoEXT()
                    .setPfnUserCallback(debugCallback)
                    .setMessageSeverity(vk::FlagTraits<vk::DebugUtilsMessageSeverityFlagBitsEXT>::allFlags ^
                                        vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo)
                    .setMessageType(vk::FlagTraits<vk::DebugUtilsMessageTypeFlagBitsEXT>::allFlags));
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
        const auto& logicalDevice = m_Device->GetLogicalDevice();
        for (u8 i{}; i < s_BufferedFrameCount; ++i)
        {
            m_FrameData[i].GeneralCommandPoolVK = logicalDevice->createCommandPoolUnique(
                vk::CommandPoolCreateInfo().setQueueFamilyIndex(m_Device->GetGeneralQueue().QueueFamilyIndex));

            m_FrameData[i].AsyncComputeCommandPoolVK = logicalDevice->createCommandPoolUnique(
                vk::CommandPoolCreateInfo().setQueueFamilyIndex(m_Device->GetComputeQueue().QueueFamilyIndex));

            m_FrameData[i].DedicatedTransferCommandPoolVK = logicalDevice->createCommandPoolUnique(
                vk::CommandPoolCreateInfo().setQueueFamilyIndex(m_Device->GetTransferQueue().QueueFamilyIndex));

            m_FrameData[i].GeneralCommandBuffer = logicalDevice
                                                      ->allocateCommandBuffers(vk::CommandBufferAllocateInfo()
                                                                                   .setCommandBufferCount(1)
                                                                                   .setCommandPool(*m_FrameData[i].GeneralCommandPoolVK)
                                                                                   .setLevel(vk::CommandBufferLevel::ePrimary))
                                                      .back();

            m_FrameData[i].RenderFinishedFence =
                logicalDevice->createFenceUnique(vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled));
            m_FrameData[i].ImageAvailableSemaphore = logicalDevice->createSemaphoreUnique(vk::SemaphoreCreateInfo());
            m_FrameData[i].RenderFinishedSemaphore = logicalDevice->createSemaphoreUnique(vk::SemaphoreCreateInfo());
        }

        // Creating default white texture 1x1.
        {
            constexpr u32 whiteTextureData = 0xFFFFFFFF;
            m_DefaultWhiteTexture =
                MakeUnique<GfxTexture>(m_Device, GfxTextureDescription(vk::ImageType::e2D, {1, 1, 1}, vk::Format::eR8G8B8A8Unorm,
                                                                       vk::ImageUsageFlagBits::eTransferDst));
            m_Device->SetDebugName("RDNT_DEFAULT_WHITE_TEX", (const vk::Image&)*m_DefaultWhiteTexture);

            auto stagingBuffer = MakeUnique<GfxBuffer>(m_Device, GfxBufferDescription(sizeof(whiteTextureData), sizeof(whiteTextureData),
                                                                                      vk::BufferUsageFlagBits::eTransferSrc,
                                                                                      EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT));
            stagingBuffer->SetData(&whiteTextureData, sizeof(whiteTextureData));

            auto executionContext = CreateImmediateExecuteContext(ECommandQueueType::COMMAND_QUEUE_TYPE_DEDICATED_TRANSFER);
            executionContext.CommandBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

            executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                vk::ImageMemoryBarrier2()
                    .setImage(*m_DefaultWhiteTexture)
                    .setSubresourceRange(
                        vk::ImageSubresourceRange().setBaseArrayLayer(0).setBaseMipLevel(0).setLevelCount(1).setLayerCount(1).setAspectMask(
                            vk::ImageAspectFlagBits::eColor))
                    .setOldLayout(vk::ImageLayout::eUndefined)
                    .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                    .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                    .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                    .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
                    .setDstStageMask(vk::PipelineStageFlagBits2::eAllTransfer)));

            executionContext.CommandBuffer.copyBufferToImage(
                *stagingBuffer, *m_DefaultWhiteTexture, vk::ImageLayout::eTransferDstOptimal,
                vk::BufferImageCopy()
                    .setImageSubresource(vk::ImageSubresourceLayers().setLayerCount(1).setAspectMask(vk::ImageAspectFlagBits::eColor))
                    .setImageExtent(vk::Extent3D(1, 1, 1)));

            executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                vk::ImageMemoryBarrier2()
                    .setImage(*m_DefaultWhiteTexture)
                    .setSubresourceRange(
                        vk::ImageSubresourceRange().setBaseArrayLayer(0).setBaseMipLevel(0).setLevelCount(1).setLayerCount(1).setAspectMask(
                            vk::ImageAspectFlagBits::eColor))
                    .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                    .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
                    .setSrcStageMask(vk::PipelineStageFlagBits2::eAllTransfer)
                    .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                    .setDstAccessMask(vk::AccessFlagBits2::eNone)
                    .setDstStageMask(vk::PipelineStageFlagBits2::eBottomOfPipe)));

            executionContext.CommandBuffer.end();
            SubmitImmediateExecuteContext(executionContext);
        }
    }

    void GfxContext::Shutdown() noexcept
    {
        LOG_INFO("{}", __FUNCTION__);
    }

    void GfxContext::InvalidateSwapchain() noexcept
    {
        // Render finished fences also need to be recreated cuz they're stalling the CPU.
        std::ranges::for_each(m_FrameData,
                              [&](auto& frameData) noexcept
                              {
                                  frameData.RenderFinishedFence = m_Device->GetLogicalDevice()->createFenceUnique(
                                      vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled));
                              });
        m_CurrentImageIndex = m_CurrentFrameIndex = 0;

        const auto& window = Application::Get().GetMainWindow();
        const auto& extent = window->GetDescription().Extent;

        const std::vector<vk::SurfaceFormatKHR> availableSurfaceFormats = m_Device->GetPhysicalDevice().getSurfaceFormatsKHR(*m_Surface);
        RDNT_ASSERT(!availableSurfaceFormats.empty(), "No surface formats present?!");

        const auto imageFormat =
            availableSurfaceFormats[0].format == vk::Format::eUndefined ? vk::Format::eB8G8R8A8Unorm : availableSurfaceFormats[0].format;
        const vk::SurfaceCapabilitiesKHR availableSurfaceCapabilities = m_Device->GetPhysicalDevice().getSurfaceCapabilitiesKHR(*m_Surface);
        constexpr auto requestedImageUsageFlags = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
        RDNT_ASSERT(availableSurfaceCapabilities.supportedUsageFlags & requestedImageUsageFlags,
                    "Swapchain's supportedUsageFlags != requestedImageUsageFlags.");

        // If the surface size is defined, the swap chain size must match
        m_SwapchainExtent = availableSurfaceCapabilities.currentExtent;
        if (m_SwapchainExtent.width == std::numeric_limits<u32>::max() || m_SwapchainExtent.height == std::numeric_limits<u32>::max())
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

        m_SupportedPresentModes = m_Device->GetPhysicalDevice().getSurfacePresentModesKHR(*m_Surface);
        if (std::find(m_SupportedPresentModes.cbegin(), m_SupportedPresentModes.cend(), m_PresentMode) == m_SupportedPresentModes.cend())
        {
            // The FIFO present mode is guaranteed by the spec.
            m_PresentMode = vk::PresentModeKHR::eFifo;
        }

        m_SwapchainImageFormat = imageFormat;
        auto swapchainCI =
            vk::SwapchainCreateInfoKHR()
                .setSurface(*m_Surface)
                .setImageSharingMode(vk::SharingMode::eExclusive)
                .setQueueFamilyIndexCount(1)
                .setPQueueFamilyIndices((const u32*)&m_Device->GetGeneralQueue().QueueFamilyIndex)
                .setCompositeAlpha(compositeAlpha)
                .setPresentMode(m_PresentMode)
                .setImageFormat(imageFormat)
                .setImageExtent(m_SwapchainExtent)
                .setImageArrayLayers(1)
                .setClipped(vk::True)
                .setMinImageCount(std::clamp(3u, availableSurfaceCapabilities.minImageCount, availableSurfaceCapabilities.maxImageCount))
                .setImageColorSpace(vk::ColorSpaceKHR::eSrgbNonlinear)
                .setImageUsage(requestedImageUsageFlags);

        auto oldSwapchain = std::move(m_Swapchain);
        if (oldSwapchain)
        {
            m_SwapchainImages.clear();
            m_SwapchainImageViews.clear();
            swapchainCI.setOldSwapchain(*oldSwapchain);
        }

        m_Swapchain       = m_Device->GetLogicalDevice()->createSwapchainKHRUnique(swapchainCI);
        m_SwapchainImages = m_Device->GetLogicalDevice()->getSwapchainImagesKHR(*m_Swapchain);

        m_SwapchainImageViews.resize(m_SwapchainImages.size());
        vk::ImageViewCreateInfo imageViewCreateInfo({}, {}, vk::ImageViewType::e2D, imageFormat, {},
                                                    {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        for (u32 i{}; i < m_SwapchainImageViews.size(); ++i)
        {
            imageViewCreateInfo.image = m_SwapchainImages[i];
            m_SwapchainImageViews[i]  = m_Device->GetLogicalDevice()->createImageViewUnique(imageViewCreateInfo);

            const std::string swapchainImageName = "SwapchainImage[" + std::to_string(i) + "]";
            m_Device->SetDebugName(swapchainImageName, m_SwapchainImages[i]);

            const std::string swapchainImageViewName = "SwapchainImageView[" + std::to_string(i) + "]";
            m_Device->SetDebugName(swapchainImageViewName, *m_SwapchainImageViews[i]);
        }
    }

}  // namespace Radiant
