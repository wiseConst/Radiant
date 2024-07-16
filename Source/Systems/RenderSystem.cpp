#include <pch.h>
#include "RenderSystem.hpp"

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

#define VMA_IMPLEMENTATION
#define VK_NO_PROTOTYPES
#include "vma/vk_mem_alloc.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <slang.h>
#include <slang-com-ptr.h>

namespace Radiant
{

    bool RenderSystem::BeginFrame() noexcept
    {
        RDNT_ASSERT(m_LogicalDevice->waitForFences(*m_FrameData[m_CurrentFrameIndex].RenderFinishedFence, vk::True, UINT64_MAX) ==
                        vk::Result::eSuccess,
                    "{}", __FUNCTION__);

        m_LogicalDevice->resetFences(*m_FrameData[m_CurrentFrameIndex].RenderFinishedFence);

        // NOTE: Apparently on NV cards this throws vk::OutOfDateKHRError.
        try
        {
            const auto [result, imageIndex] =
                m_LogicalDevice->acquireNextImageKHR(*m_Swapchain, UINT64_MAX, *m_FrameData[m_CurrentFrameIndex].ImageAvailableSemaphore);

            if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
            {
                m_LogicalDevice->waitIdle();
                InvalidateSwapchain();
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
            m_LogicalDevice->waitIdle();
            InvalidateSwapchain();
            return false;
        }

        m_LogicalDevice->resetCommandPool(*m_FrameData[m_CurrentFrameIndex].CommandPool);
        m_FrameData[m_CurrentFrameIndex].CommandBuffer.begin(
            vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

        const auto imageSubresourceRange = vk::ImageSubresourceRange()
                                               .setBaseArrayLayer(0)
                                               .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                               .setBaseMipLevel(0)
                                               .setLayerCount(1)
                                               .setLevelCount(1);

        m_FrameData[m_CurrentFrameIndex].CommandBuffer.pipelineBarrier2(
            vk::DependencyInfo()
                .setDependencyFlags(vk::DependencyFlagBits::eByRegion)
                .setImageMemoryBarriers(vk::ImageMemoryBarrier2()
                                            .setImage(m_SwapchainImages[m_CurrentImageIndex])
                                            .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                                            .setSrcStageMask(vk::PipelineStageFlagBits2::eBottomOfPipe)
                                            .setSubresourceRange(imageSubresourceRange)
                                            .setOldLayout(vk::ImageLayout::eUndefined)
                                            .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                            .setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                                            .setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)));

        const auto renderingInfo =
            vk::RenderingAttachmentInfo()
                .setLoadOp(vk::AttachmentLoadOp::eClear)
                .setStoreOp(vk::AttachmentStoreOp::eStore)
                .setImageView(*m_SwapchainImageViews[m_CurrentImageIndex])
                .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
                .setClearValue(vk::ClearValue().setColor(vk::ClearColorValue().setFloat32({0.1f, 0.1f, 0.1f, 1.0f})));
        m_FrameData[m_CurrentFrameIndex].CommandBuffer.beginRendering(vk::RenderingInfo()
                                                                          .setColorAttachments(renderingInfo)
                                                                          .setLayerCount(1)
                                                                          .setRenderArea(vk::Rect2D().setExtent(m_SwapchainExtent)));
        m_FrameData[m_CurrentFrameIndex].CommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *m_TriPipeline);
        m_FrameData[m_CurrentFrameIndex].CommandBuffer.draw(3, 1, 0, 0);

        // m_FrameData[m_CurrentFrameIndex].CommandBuffer.pipelineBarrier2(
        //     vk::DependencyInfo()
        //         .setDependencyFlags(vk::DependencyFlagBits::eByRegion)
        //         .setImageMemoryBarriers(
        //             vk::ImageMemoryBarrier2()
        //                 .setImage(m_SwapchainImages[m_CurrentImageIndex])
        //                 .setSrcAccessMask(vk::AccessFlagBits2::eNone)
        //                 .setSrcStageMask(vk::PipelineStageFlagBits2::eBottomOfPipe)  // Waiting for presentation engine to do his things,
        //                                                                              // acquireNextImageKHR() requires it!!
        //                 .setSubresourceRange(imageSubresourceRange)
        //                 .setOldLayout(vk::ImageLayout::eUndefined)
        //                 .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
        //                 .setDstAccessMask(vk::AccessFlagBits2::eTransferWrite)
        //                 .setDstStageMask(vk::PipelineStageFlagBits2::eTransfer)));

        // m_FrameData[m_CurrentFrameIndex].CommandBuffer.clearColorImage(
        //     m_SwapchainImages[m_CurrentImageIndex], vk::ImageLayout::eTransferDstOptimal,
        //     vk::ClearColorValue().setFloat32({0.1f, 0.1f, 0.1f, 1.0f}), {imageSubresourceRange});

        return true;
    }

    void RenderSystem::EndFrame() noexcept
    {
        m_FrameData[m_CurrentFrameIndex].CommandBuffer.endRendering();

        const auto imageSubresourceRange = vk::ImageSubresourceRange()
                                               .setBaseArrayLayer(0)
                                               .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                               .setBaseMipLevel(0)
                                               .setLayerCount(1)
                                               .setLevelCount(1);
        /*m_FrameData[m_CurrentFrameIndex].CommandBuffer.pipelineBarrier2(
            vk::DependencyInfo()
                .setDependencyFlags(vk::DependencyFlagBits::eByRegion)
                .setImageMemoryBarriers(vk::ImageMemoryBarrier2()
                                            .setImage(m_SwapchainImages[m_CurrentImageIndex])
                                            .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
                                            .setSrcStageMask(vk::PipelineStageFlagBits2::eTransfer)
                                            .setSubresourceRange(imageSubresourceRange)
                                            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                                            .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
                                            .setDstAccessMask(vk::AccessFlagBits2::eNone)
                                            .setDstStageMask(vk::PipelineStageFlagBits2::eNone)));*/
        m_FrameData[m_CurrentFrameIndex].CommandBuffer.pipelineBarrier2(
            vk::DependencyInfo()
                .setDependencyFlags(vk::DependencyFlagBits::eByRegion)
                .setImageMemoryBarriers(vk::ImageMemoryBarrier2()
                                            .setImage(m_SwapchainImages[m_CurrentImageIndex])
                                            .setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
                                            .setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
                                            .setSubresourceRange(imageSubresourceRange)
                                            .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
                                            .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
                                            .setDstAccessMask(vk::AccessFlagBits2::eNone)
                                            .setDstStageMask(vk::PipelineStageFlagBits2::eNone)));
        m_FrameData[m_CurrentFrameIndex].CommandBuffer.end();

        const vk::PipelineStageFlags waitDstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                                        vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eComputeShader;
        m_PresentQueue.Handle.submit(vk::SubmitInfo()
                                         .setCommandBuffers(m_FrameData[m_CurrentFrameIndex].CommandBuffer)
                                         .setSignalSemaphores(*m_FrameData[m_CurrentFrameIndex].RenderFinishedSemaphore)
                                         .setWaitSemaphores(*m_FrameData[m_CurrentFrameIndex].ImageAvailableSemaphore)
                                         .setWaitDstStageMask(waitDstStageMask),
                                     *m_FrameData[m_CurrentFrameIndex].RenderFinishedFence);

        // NOTE: Apparently on NV cards this throws vk::OutOfDateKHRError.
        try
        {
            auto result =
                m_PresentQueue.Handle.presentKHR(vk::PresentInfoKHR()
                                                     .setImageIndices(m_CurrentImageIndex)
                                                     .setSwapchains(*m_Swapchain)
                                                     .setWaitSemaphores(*m_FrameData[m_CurrentFrameIndex].RenderFinishedSemaphore));

            if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
            {
                m_LogicalDevice->waitIdle();
                InvalidateSwapchain();
            }
            else if (result != vk::Result::eSuccess)
            {
                RDNT_ASSERT(false, "presentKHR(): unknown result!");
            }
        }
        catch (vk::OutOfDateKHRError)
        {
            m_LogicalDevice->waitIdle();
            InvalidateSwapchain();
        }

        m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % s_BufferedFrameCount;
    }

    void RenderSystem::Init() noexcept
    {
        LOG_INFO("{}", __FUNCTION__);

        // Initialize minimal set of function pointers.
        VULKAN_HPP_DEFAULT_DISPATCHER.init();
        CreateInstanceAndDebugUtilsMessenger();

        std::vector<const char*> requiredDeviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,          // For rendering into OS-window
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,  // Neglect render passes, required by ImGui, core in vk 1.3
            // TODO:  VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME,  // Exclusive fullscreen window
            // TODO:   VK_EXT_MESH_SHADER_EXTENSION_NAME,  // Mesh shading
        };

        constexpr vk::PhysicalDeviceFeatures requiredDeviceFeatures =
            vk::PhysicalDeviceFeatures().setShaderInt16(true).setShaderInt64(true);

        // First chained struct
        void* pNext = nullptr;

        auto vkFeatures13 = vk::PhysicalDeviceVulkan13Features().setDynamicRendering(true).setSynchronization2(true).setMaintenance4(true);
        pNext             = &vkFeatures13;

        // The train...
        void** paravozik = nullptr;
        paravozik        = &vkFeatures13.pNext;

        auto vkFeatures12 = vk::PhysicalDeviceVulkan12Features()
                                .setBufferDeviceAddress(true)
                                .setScalarBlockLayout(true)
                                .setShaderInt8(true)
                                .setShaderFloat16(true)
                                .setTimelineSemaphore(true)
                                .setDescriptorIndexing(true)
                                .setDescriptorBindingPartiallyBound(true)
                                .setDescriptorBindingVariableDescriptorCount(true)
                                .setDescriptorBindingSampledImageUpdateAfterBind(true)
                                .setDescriptorBindingStorageImageUpdateAfterBind(true);
        *paravozik = vkFeatures12.pNext;
        paravozik  = &vkFeatures12.pNext;

        SelectGPUAndCreateLogicalDevice(requiredDeviceExtensions, requiredDeviceFeatures, pNext);
        InvalidateSwapchain();
        CreateFrameResources();

        LoadPipelineCache();

        using Slang::ComPtr;

        // First we need to create slang global session with work with the Slang API.
        ComPtr<slang::IGlobalSession> slangGlobalSession;
        auto slangResult = slang::createGlobalSession(slangGlobalSession.writeRef());

        // Next we create a compilation session to generate SPIRV code from Slang source.
        slang::SessionDesc sessionDesc = {};
        slang::TargetDesc targetDesc   = {};
        targetDesc.format              = SLANG_SPIRV;
        targetDesc.profile             = slangGlobalSession->findProfile("spirv_1_6");
        targetDesc.flags               = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

        sessionDesc.targets     = &targetDesc;
        sessionDesc.targetCount = 1;

        ComPtr<slang::ISession> session;
        slangResult = slangGlobalSession->createSession(sessionDesc, session.writeRef());

        // Once the session has been obtained, we can start loading code into it.
        //
        // The simplest way to load code is by calling `loadModule` with the name of a Slang
        // module. A call to `loadModule("hello-world")` will behave more or less as if you
        // wrote:
        //
        //      import hello_world;
        //
        // In a Slang shader file. The compiler will use its search paths to try to locate
        // `hello-world.slang`, then compile and load that file. If a matching module had
        // already been loaded previously, that would be used directly.
        slang::IModule* slangModule = nullptr;
        {
            ComPtr<slang::IBlob> diagnosticBlob;
            slangModule = session->loadModule("../Assets/Shaders/shaders.slang", diagnosticBlob.writeRef());
            RDNT_ASSERT(slangModule, "Failed to load slang shader!");
        }

        // Loading the `hello-world` module will compile and check all the shader code in it,
        // including the shader entry points we want to use. Now that the module is loaded
        // we can look up those entry points by name.
        //
        // Note: If you are using this `loadModule` approach to load your shader code it is
        // important to tag your entry point functions with the `[shader("...")]` attribute
        // (e.g., `[shader("compute")] void computeMain(...)`). Without that information there
        // is no umambiguous way for the compiler to know which functions represent entry
        // points when it parses your code via `loadModule()`.
        //
        ComPtr<slang::IEntryPoint> vsEntryPoint, fsEntryPoint;
        slangResult = slangModule->findEntryPointByName("vertexMain", vsEntryPoint.writeRef());
        slangResult = slangModule->findEntryPointByName("fragmentMain", fsEntryPoint.writeRef());

        // At this point we have a few different Slang API objects that represent
        // pieces of our code: `module`, `vertexEntryPoint`, and `fragmentEntryPoint`.
        //
        // A single Slang module could contain many different entry points (e.g.,
        // four vertex entry points, three fragment entry points, and two compute
        // shaders), and before we try to generate output code for our target API
        // we need to identify which entry points we plan to use together.
        //
        // Modules and entry points are both examples of *component types* in the
        // Slang API. The API also provides a way to build a *composite* out of
        // other pieces, and that is what we are going to do with our module
        // and entry points.
        //
        const std::vector<slang::IComponentType*> vsComponentTypes{slangModule, vsEntryPoint};
        const std::vector<slang::IComponentType*> fsComponentTypes{slangModule, fsEntryPoint};

        // Actually creating the composite component type is a single operation
        // on the Slang session, but the operation could potentially fail if
        // something about the composite was invalid (e.g., you are trying to
        // combine multiple copies of the same module), so we need to deal
        // with the possibility of diagnostic output.
        //
        ComPtr<slang::IComponentType> vsComposedProgram;
        {
            ComPtr<slang::IBlob> diagnosticsBlob;
            slangResult = session->createCompositeComponentType(vsComponentTypes.data(), vsComponentTypes.size(),
                                                                vsComposedProgram.writeRef(), diagnosticsBlob.writeRef());
        }

        ComPtr<slang::IComponentType> fsComposedProgram;
        {
            ComPtr<slang::IBlob> diagnosticsBlob;
            slangResult = session->createCompositeComponentType(fsComponentTypes.data(), fsComponentTypes.size(),
                                                                fsComposedProgram.writeRef(), diagnosticsBlob.writeRef());
        }

        // Now we can call `composedProgram->getEntryPointCode()` to retrieve the
        // compiled SPIRV code that we will use to create a vulkan compute pipeline.
        // This will trigger the final Slang compilation and spirv code generation.
        ComPtr<slang::IBlob> vsSpirvCode;
        {
            ComPtr<slang::IBlob> diagnosticsBlob;
            slangResult = vsComposedProgram->getEntryPointCode(0, 0, vsSpirvCode.writeRef(), diagnosticsBlob.writeRef());
        }

        vk::UniqueShaderModule vsModule =
            m_LogicalDevice->createShaderModuleUnique(vk::ShaderModuleCreateInfo()
                                                          .setPCode(static_cast<const uint32_t*>(vsSpirvCode->getBufferPointer()))
                                                          .setCodeSize(vsSpirvCode->getBufferSize()));

        ComPtr<slang::IBlob> fsSpirvCode;
        {
            ComPtr<slang::IBlob> diagnosticsBlob;
            slangResult = fsComposedProgram->getEntryPointCode(0, 0, fsSpirvCode.writeRef(), diagnosticsBlob.writeRef());
        }

        vk::UniqueShaderModule fsModule =
            m_LogicalDevice->createShaderModuleUnique(vk::ShaderModuleCreateInfo()
                                                          .setPCode(static_cast<const uint32_t*>(fsSpirvCode->getBufferPointer()))
                                                          .setCodeSize(fsSpirvCode->getBufferSize()));

        const std::vector<vk::PipelineShaderStageCreateInfo> shaderStages{
            vk::PipelineShaderStageCreateInfo().setStage(vk::ShaderStageFlagBits::eVertex).setModule(*vsModule).setPName("main"),
            vk::PipelineShaderStageCreateInfo().setStage(vk::ShaderStageFlagBits::eFragment).setModule(*fsModule).setPName("main")};

        const vk::Format colorAttachmentFormat = vk::Format::eB8G8R8A8Unorm;
        const auto dynamicRenderingInfo        = vk::PipelineRenderingCreateInfo().setColorAttachmentFormats(colorAttachmentFormat);
        const auto inputAssemblyStateCI = vk::PipelineInputAssemblyStateCreateInfo().setTopology(vk::PrimitiveTopology::eTriangleList);
        const auto vtxInputStateCI      = vk::PipelineVertexInputStateCreateInfo();
        const auto depthStencilStateCI  = vk::PipelineDepthStencilStateCreateInfo();
        const auto colorBlendAttachment =
            vk::PipelineColorBlendAttachmentState().setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                                                      vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
        const auto blendStateCI         = vk::PipelineColorBlendStateCreateInfo().setAttachments(colorBlendAttachment);
        const auto rasterizationStateCI = vk::PipelineRasterizationStateCreateInfo()
                                              .setCullMode(vk::CullModeFlagBits::eNone)
                                              .setPolygonMode(VULKAN_HPP_NAMESPACE::PolygonMode::eFill)
                                              .setFrontFace(vk::FrontFace::eCounterClockwise)
                                              .setLineWidth(1.0f);
        const auto msaaStateCI = vk::PipelineMultisampleStateCreateInfo().setRasterizationSamples(vk::SampleCountFlagBits::e1);

        const auto scissor  = vk::Rect2D().setExtent(m_SwapchainExtent);
        const auto viewport = vk::Viewport()
                                  .setMinDepth(0.0f)
                                  .setMaxDepth(1.0f)
                                  .setWidth(static_cast<float>(m_SwapchainExtent.width))
                                  .setHeight(static_cast<float>(m_SwapchainExtent.height));
        const auto viewportStateCI = vk::PipelineViewportStateCreateInfo().setScissors(scissor).setViewports(viewport);

        {
            auto [result, pipeline] =
                m_LogicalDevice->createGraphicsPipelineUnique(*m_PipelineCache, vk::GraphicsPipelineCreateInfo()
                                                                                    .setLayout(*m_PipelineLayout)
                                                                                    .setStages(shaderStages)
                                                                                    .setPNext(&dynamicRenderingInfo)
                                                                                    .setPInputAssemblyState(&inputAssemblyStateCI)
                                                                                    .setPVertexInputState(&vtxInputStateCI)
                                                                                    .setPDepthStencilState(&depthStencilStateCI)
                                                                                    .setPViewportState(&viewportStateCI)
                                                                                    .setPColorBlendState(&blendStateCI)
                                                                                    .setPRasterizationState(&rasterizationStateCI)
                                                                                    .setPMultisampleState(&msaaStateCI));
            RDNT_ASSERT(result == vk::Result::eSuccess, "Failed to create pipeline!");

            m_TriPipeline = std::move(pipeline);
        }
    }

    void RenderSystem::CreateInstanceAndDebugUtilsMessenger() noexcept
    {
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
                LOG_TRACE("{}", il.layerName.data());
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

    void RenderSystem::SelectGPUAndCreateLogicalDevice(std::vector<const char*>& requiredDeviceExtensions,
                                                       const vk::PhysicalDeviceFeatures& requiredDeviceFeatures, const void* pNext) noexcept
    {
        const auto gpus = m_Instance->enumeratePhysicalDevices();
        LOG_TRACE("{} gpus present.", gpus.size());
        for (auto& gpu : gpus)
        {
            const auto gpuProperties = gpu.getProperties();
            LOG_TRACE("{}", gpuProperties.deviceName.data());

            if (s_bForceIGPU && gpuProperties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu ||
                !s_bForceIGPU && gpuProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
            {
                const auto deviceExtensions = gpu.enumerateDeviceExtensionProperties();

                constexpr std::uint32_t NVidiaVendorID{0x10DE};
                constexpr std::uint32_t AMDVendorID{0x1002};

                // [NVIDIA] called without pageable device local memory.
                // Use pageableDeviceLocalMemory from VK_EXT_pageable_device_local_memory when it is available.
                if (std::find_if(deviceExtensions.cbegin(), deviceExtensions.cend(),
                                 [](const vk::ExtensionProperties& deviceExtension) {
                                     return strcmp(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME, deviceExtension.extensionName) == 0;
                                 }) != deviceExtensions.cend() &&
                    std::find_if(deviceExtensions.cbegin(), deviceExtensions.cend(),
                                 [](const vk::ExtensionProperties& deviceExtension) {
                                     return strcmp(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME, deviceExtension.extensionName) == 0;
                                 }) != deviceExtensions.cend())
                {
                    requiredDeviceExtensions.emplace_back(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME);
                    requiredDeviceExtensions.emplace_back(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);
                }

                for (const auto& rde : requiredDeviceExtensions)
                {
                    const bool bExtensionFound = std::find_if(deviceExtensions.cbegin(), deviceExtensions.cend(),
                                                              [&rde](const vk::ExtensionProperties& deviceExtension) {
                                                                  return strcmp(rde, deviceExtension.extensionName) == 0;
                                                              }) != deviceExtensions.end();

                    RDNT_ASSERT(bExtensionFound, "Device extension: {} not supported!", rde);
                }

                // Check if left structure is contained in second.
                constexpr auto AreAllFlagsSet = [](const vk::PhysicalDeviceFeatures& lhs, const vk::PhysicalDeviceFeatures& rhs)
                {
                    return (!lhs.robustBufferAccess || rhs.robustBufferAccess) && (!lhs.fullDrawIndexUint32 || rhs.fullDrawIndexUint32) &&
                           (!lhs.imageCubeArray || rhs.imageCubeArray) && (!lhs.independentBlend || rhs.independentBlend) &&
                           (!lhs.geometryShader || rhs.geometryShader) && (!lhs.tessellationShader || rhs.tessellationShader) &&
                           (!lhs.sampleRateShading || rhs.sampleRateShading) && (!lhs.dualSrcBlend || rhs.dualSrcBlend) &&
                           (!lhs.logicOp || rhs.logicOp) && (!lhs.multiDrawIndirect || rhs.multiDrawIndirect) &&
                           (!lhs.drawIndirectFirstInstance || rhs.drawIndirectFirstInstance) && (!lhs.depthClamp || rhs.depthClamp) &&
                           (!lhs.depthBiasClamp || rhs.depthBiasClamp) && (!lhs.fillModeNonSolid || rhs.fillModeNonSolid) &&
                           (!lhs.depthBounds || rhs.depthBounds) && (!lhs.wideLines || rhs.wideLines) &&
                           (!lhs.largePoints || rhs.largePoints) && (!lhs.alphaToOne || rhs.alphaToOne) &&
                           (!lhs.multiViewport || rhs.multiViewport) && (!lhs.samplerAnisotropy || rhs.samplerAnisotropy) &&
                           (!lhs.textureCompressionETC2 || rhs.textureCompressionETC2) &&
                           (!lhs.textureCompressionASTC_LDR || rhs.textureCompressionASTC_LDR) &&
                           (!lhs.textureCompressionBC || rhs.textureCompressionBC) &&
                           (!lhs.occlusionQueryPrecise || rhs.occlusionQueryPrecise) &&
                           (!lhs.pipelineStatisticsQuery || rhs.pipelineStatisticsQuery) &&
                           (!lhs.vertexPipelineStoresAndAtomics || rhs.vertexPipelineStoresAndAtomics) &&
                           (!lhs.fragmentStoresAndAtomics || rhs.fragmentStoresAndAtomics) &&
                           (!lhs.shaderTessellationAndGeometryPointSize || rhs.shaderTessellationAndGeometryPointSize) &&
                           (!lhs.shaderImageGatherExtended || rhs.shaderImageGatherExtended) &&
                           (!lhs.shaderStorageImageExtendedFormats || rhs.shaderStorageImageExtendedFormats) &&
                           (!lhs.shaderStorageImageMultisample || rhs.shaderStorageImageMultisample) &&
                           (!lhs.shaderStorageImageReadWithoutFormat || rhs.shaderStorageImageReadWithoutFormat) &&
                           (!lhs.shaderStorageImageWriteWithoutFormat || rhs.shaderStorageImageWriteWithoutFormat) &&
                           (!lhs.shaderUniformBufferArrayDynamicIndexing || rhs.shaderUniformBufferArrayDynamicIndexing) &&
                           (!lhs.shaderSampledImageArrayDynamicIndexing || rhs.shaderSampledImageArrayDynamicIndexing) &&
                           (!lhs.shaderStorageBufferArrayDynamicIndexing || rhs.shaderStorageBufferArrayDynamicIndexing) &&
                           (!lhs.shaderStorageImageArrayDynamicIndexing || rhs.shaderStorageImageArrayDynamicIndexing) &&
                           (!lhs.shaderClipDistance || rhs.shaderClipDistance) && (!lhs.shaderCullDistance || rhs.shaderCullDistance) &&
                           (!lhs.shaderFloat64 || rhs.shaderFloat64) && (!lhs.shaderInt64 || rhs.shaderInt64) &&
                           (!lhs.shaderInt16 || rhs.shaderInt16) && (!lhs.shaderResourceResidency || rhs.shaderResourceResidency) &&
                           (!lhs.shaderResourceMinLod || rhs.shaderResourceMinLod) && (!lhs.sparseBinding || rhs.sparseBinding) &&
                           (!lhs.sparseResidencyBuffer || rhs.sparseResidencyBuffer) &&
                           (!lhs.sparseResidencyImage2D || rhs.sparseResidencyImage2D) &&
                           (!lhs.sparseResidencyImage3D || rhs.sparseResidencyImage3D) &&
                           (!lhs.sparseResidency2Samples || rhs.sparseResidency2Samples) &&
                           (!lhs.sparseResidency4Samples || rhs.sparseResidency4Samples) &&
                           (!lhs.sparseResidency8Samples || rhs.sparseResidency8Samples) &&
                           (!lhs.sparseResidency16Samples || rhs.sparseResidency16Samples) &&
                           (!lhs.sparseResidencyAliased || rhs.sparseResidencyAliased) &&
                           (!lhs.variableMultisampleRate || rhs.variableMultisampleRate) && (!lhs.inheritedQueries || rhs.inheritedQueries);
                };

                RDNT_ASSERT(AreAllFlagsSet(requiredDeviceFeatures, gpu.getFeatures()),
                            "Required device features flags aren't present in available device features!");

                m_PhysicalDevice = gpu;
                LOG_INFO("Chosen GPU: {}", gpuProperties.deviceName.data());
            }
        }

#ifdef RDNT_WINDOWS
        const auto& mainWindow = Application::Get().GetMainWindow();

        const auto win32SurfaceCI =
            vk::Win32SurfaceCreateInfoKHR().setHwnd(glfwGetWin32Window(mainWindow->Get())).setHinstance(GetModuleHandle(nullptr));
        m_Surface = m_Instance->createWin32SurfaceKHRUnique(win32SurfaceCI);
#else
#error Do override surface khr creation on other platforms
#endif

        std::vector<vk::QueueFamilyProperties> qfProperties = m_PhysicalDevice.getQueueFamilyProperties();
        RDNT_ASSERT(!qfProperties.empty(), "Queue Families are empty!");

        for (std::uint32_t i{}; i < qfProperties.size(); ++i)
        {
            const auto queueCount = qfProperties[i].queueCount;
            RDNT_ASSERT(queueCount > 0, "Queue Family[{}] has no queues?!", i);

            const auto queueFlags = qfProperties[i].queueFlags;

            // Check if DMA engine is present.
            if (queueFlags == vk::QueueFlagBits::eTransfer ||
                queueFlags == (vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eSparseBinding))
            {
                LOG_INFO("Found DMA engine at queue family [{}]", i);
            }

            constexpr auto gctQueueFlags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;
            if (!m_GCTQueue.QueueFamilyIndex.has_value() && (queueFlags & gctQueueFlags) == gctQueueFlags)
            {
                m_GCTQueue.QueueFamilyIndex = i;
            }

            if (!m_PresentQueue.QueueFamilyIndex.has_value() && m_PhysicalDevice.getSurfaceSupportKHR(i, *m_Surface))
            {
                m_PresentQueue.QueueFamilyIndex = i;
            }
        }
        RDNT_ASSERT(m_GCTQueue.QueueFamilyIndex.has_value(), "Failed to find GCT Queue Family Index!");
        RDNT_ASSERT(m_PresentQueue.QueueFamilyIndex.has_value(), "Failed to find Present Queue Family Index!");

        constexpr float queuePriority = 1.0f;

        std::vector<vk::DeviceQueueCreateInfo> queuesCI;
        for (const std::set<std::uint32_t> uniqueQFIndices{*m_GCTQueue.QueueFamilyIndex, *m_PresentQueue.QueueFamilyIndex};
             const auto qfIndex : uniqueQFIndices)
        {
            queuesCI.emplace_back().setPQueuePriorities(&queuePriority).setQueueCount(1).setQueueFamilyIndex(qfIndex);
        }

        const auto logicalDeviceCI = vk::DeviceCreateInfo()
                                         .setPEnabledFeatures(&requiredDeviceFeatures)
                                         .setQueueCreateInfos(queuesCI)
                                         .setEnabledExtensionCount(requiredDeviceExtensions.size())
                                         .setPEnabledExtensionNames(requiredDeviceExtensions)
                                         .setPNext(pNext);
        m_LogicalDevice = m_PhysicalDevice.createDeviceUnique(logicalDeviceCI);

        // Load device functions.
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*m_LogicalDevice);

        m_GCTQueue.Handle     = m_LogicalDevice->getQueue(*m_GCTQueue.QueueFamilyIndex, 0);
        m_PresentQueue.Handle = m_LogicalDevice->getQueue(*m_PresentQueue.QueueFamilyIndex, 0);

        /*VmaAllocator testAllocator{};
        VmaAllocatorCreateInfo allocatorCI = {};
        RDNT_ASSERT(vmaCreateAllocator(&allocatorCI, &testAllocator) == VK_SUCCESS, "Failed to create VMA!");*/
    }

    void RenderSystem::CreateFrameResources() noexcept
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
        /*  const auto bindlessInfo = {
              vk::DescriptorSetLayoutBindingFlagsCreateInfoEXT().setBindingFlags(vk::FlagTraits<vk::DescriptorBindingFlagBits>::allFlags)};*/
        m_DescriptorSetLayout = m_LogicalDevice->createDescriptorSetLayoutUnique(
            vk::DescriptorSetLayoutCreateInfo().setBindingCount(3).setBindings(bindings) /*.setPNext(&bindlessInfo)*/);

        m_PipelineLayout = m_LogicalDevice->createPipelineLayoutUnique(
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
            m_FrameData[i].CommandPool =
                m_LogicalDevice->createCommandPoolUnique(vk::CommandPoolCreateInfo().setQueueFamilyIndex(*m_GCTQueue.QueueFamilyIndex));

            m_FrameData[i].CommandBuffer = m_LogicalDevice
                                               ->allocateCommandBuffers(vk::CommandBufferAllocateInfo()
                                                                            .setCommandBufferCount(1)
                                                                            .setCommandPool(*m_FrameData[i].CommandPool)
                                                                            .setLevel(vk::CommandBufferLevel::ePrimary))
                                               .back();

            m_FrameData[i].RenderFinishedFence =
                m_LogicalDevice->createFenceUnique(vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled));
            m_FrameData[i].ImageAvailableSemaphore = m_LogicalDevice->createSemaphoreUnique(vk::SemaphoreCreateInfo());
            m_FrameData[i].RenderFinishedSemaphore = m_LogicalDevice->createSemaphoreUnique(vk::SemaphoreCreateInfo());

            m_FrameData[i].DescriptorPool =
                m_LogicalDevice->createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo()
                                                                .setMaxSets(1)
                                                                .setFlags(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind)
                                                                .setPoolSizes(poolSizes));

            m_FrameData[i].DescriptorSet = m_LogicalDevice
                                               ->allocateDescriptorSets(vk::DescriptorSetAllocateInfo()
                                                                            .setDescriptorPool(*m_FrameData[i].DescriptorPool)
                                                                            .setSetLayouts(*m_DescriptorSetLayout))
                                               .back();
        }
    }

    void RenderSystem::InvalidateSwapchain() noexcept
    {
        const auto& window = Application::Get().GetMainWindow();
        const auto& extent = window->GetDescription().Extent;

        const std::vector<vk::SurfaceFormatKHR> availableSurfaceFormats = m_PhysicalDevice.getSurfaceFormatsKHR(*m_Surface);
        RDNT_ASSERT(!availableSurfaceFormats.empty(), "No surface formats present?!");

        const auto imageFormat =
            availableSurfaceFormats[0].format == vk::Format::eUndefined ? vk::Format::eB8G8R8A8Unorm : availableSurfaceFormats[0].format;
        const vk::SurfaceCapabilitiesKHR availableSurfaceCapabilities = m_PhysicalDevice.getSurfaceCapabilitiesKHR(*m_Surface);
        constexpr auto requestedImageUsageFlags = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
        RDNT_ASSERT((availableSurfaceCapabilities.supportedUsageFlags & requestedImageUsageFlags) == requestedImageUsageFlags,
                    "Swapchain's supportedUsageFlags != requestedImageUsageFlags.");

        // If the surface size is defined, the swap chain size must match
        m_SwapchainExtent = availableSurfaceCapabilities.currentExtent;
        if (m_SwapchainExtent.width == std::numeric_limits<std::uint32_t>::max())
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

        auto swapchainCI =
            vk::SwapchainCreateInfoKHR()
                .setSurface(*m_Surface)
                .setImageSharingMode(vk::SharingMode::eExclusive)
                .setQueueFamilyIndexCount(1)
                .setPQueueFamilyIndices(&m_GCTQueue.QueueFamilyIndex.value())
                .setCompositeAlpha(compositeAlpha)
                .setPresentMode(presentMode)
                .setImageFormat(imageFormat)
                .setImageExtent(m_SwapchainExtent)
                .setImageArrayLayers(1)
                .setClipped(vk::True)
                .setMinImageCount(std::clamp(3u, availableSurfaceCapabilities.minImageCount, availableSurfaceCapabilities.maxImageCount))
                .setImageColorSpace(vk::ColorSpaceKHR::eSrgbNonlinear)
                .setImageUsage(requestedImageUsageFlags);
        const std::array<std::uint32_t, 2> queueFamilyIndices{*m_GCTQueue.QueueFamilyIndex, *m_PresentQueue.QueueFamilyIndex};
        if (m_GCTQueue.QueueFamilyIndex != m_PresentQueue.QueueFamilyIndex)
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

        m_Swapchain = m_LogicalDevice->createSwapchainKHRUnique(swapchainCI);

        m_SwapchainImages = m_LogicalDevice->getSwapchainImagesKHR(*m_Swapchain);
        m_SwapchainImageViews.reserve(m_SwapchainImages.size());
        vk::ImageViewCreateInfo imageViewCreateInfo({}, {}, vk::ImageViewType::e2D, imageFormat, {},
                                                    {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        for (const auto& image : m_SwapchainImages)
        {
            imageViewCreateInfo.image = image;
            m_SwapchainImageViews.emplace_back(m_LogicalDevice->createImageViewUnique(imageViewCreateInfo));
        }
    }

    void RenderSystem::LoadPipelineCache() noexcept
    {
        auto pipelineCacheCI = vk::PipelineCacheCreateInfo();
        std::vector<std::uint8_t> pipelineCacheBlob;
        if (std::filesystem::exists("pso_cache.bin"))
        {
            pipelineCacheBlob = CoreUtils::LoadData<std::uint8_t>("pso_cache.bin");
            pipelineCacheCI.setInitialDataSize(pipelineCacheBlob.size() * pipelineCacheBlob[0]).setPInitialData(pipelineCacheBlob.data());
        }

        m_PipelineCache = m_LogicalDevice->createPipelineCacheUnique(pipelineCacheCI);
    }

    void RenderSystem::Shutdown() noexcept
    {
        m_LogicalDevice->waitIdle();
        CoreUtils::SaveData("pso_cache.bin", m_LogicalDevice->getPipelineCacheData(*m_PipelineCache));

        LOG_INFO("{}", __FUNCTION__);
    }

}  // namespace Radiant
