#include "GfxDevice.hpp"

#define VMA_IMPLEMENTATION
#define VK_NO_PROTOTYPES
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

#include <vulkan/vulkan_to_string.hpp>

namespace Radiant
{
#if RDNT_DEBUG
    static constexpr const char* s_PipelineCacheName = "pso_cache_debug.bin";
#elif RDNT_RELEASE
    static constexpr const char* s_PipelineCacheName = "pso_cache_release.bin";
#else
#error Unknown build type!
#endif

#define RENDER_FORCE_IGPU 0

    void GfxDevice::Init(const vk::UniqueInstance& instance, const vk::UniqueSurfaceKHR& surface) noexcept
    {
        std::vector<const char*> requiredDeviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,                 // For rendering into OS-window
            VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,  // To neglect viewport state definition on pipeline creation
            VK_EXT_MEMORY_BUDGET_EXTENSION_NAME,             // Provides query for current memory usage and budget.
            VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME,          // uint8 index buffer
        };
        // debugPrintEXT shaders
        if constexpr (s_bShaderDebugPrintf) requiredDeviceExtensions.emplace_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);

        if constexpr (s_bRequireMeshShading) requiredDeviceExtensions.emplace_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);

        if constexpr (s_bRequireRayTracing)
        {
            requiredDeviceExtensions.emplace_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);  // To build acceleration structures
            requiredDeviceExtensions.emplace_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);    // To use vkCmdTraceRaysKHR
            requiredDeviceExtensions.emplace_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);               // To trace rays in every shader I want

            // Required by acceleration structure,
            // allows the driver to run some expensive CPU-based Vulkan API calls
            // asynchronously(such as a Vulkan API call that builds an acceleration
            // structure on a CPU instead of a GPU) � much like launching a thread in
            // C++ to perform a task asynchronously, then waiting for it to complete.
            requiredDeviceExtensions.emplace_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        }

        auto vkFeatures13 = vk::PhysicalDeviceVulkan13Features()
                                .setDynamicRendering(vk::True)
                                .setSynchronization2(vk::True)
                                .setShaderDemoteToHelperInvocation(vk::True)  // NOTE: Fucking slang requires it
                                .setMaintenance4(vk::True);

        // The train starts here...
        void** paravozik = &vkFeatures13.pNext;

        auto vkFeatures12 =
            vk::PhysicalDeviceVulkan12Features()
                .setBufferDeviceAddress(vk::True)  // GPU buffer pointers via uint64_t
                .setScalarBlockLayout(vk::True)    // Solving shader data alignment issues.
                .setShaderInt8(vk::True)
                .setShaderFloat16(vk::True)
                .setShaderOutputLayer(vk::True)  // Used for transforming equirectangular map to cube map(instance rendering cube 6 times).
                .setTimelineSemaphore(vk::True)
                .setHostQueryReset(vk::True)
                .setSamplerFilterMinmax(vk::True)
                .setDescriptorIndexing(vk::True)
                .setDescriptorBindingPartiallyBound(vk::True)
                .setDescriptorBindingSampledImageUpdateAfterBind(vk::True)
                .setDescriptorBindingStorageImageUpdateAfterBind(vk::True)
                .setDescriptorBindingUpdateUnusedWhilePending(vk::True)
                .setRuntimeDescriptorArray(vk::True);

        *paravozik = &vkFeatures12;
        paravozik  = &vkFeatures12.pNext;

        auto vkFeatures11 = vk::PhysicalDeviceVulkan11Features()
                                .setStorageBuffer16BitAccess(vk::True)
                                .setShaderDrawParameters(vk::True)
                                .setVariablePointers(vk::True)                // NOTE: Fucking slang requires it
                                .setVariablePointersStorageBuffer(vk::True);  // NOTE: Fucking slang requires it

        *paravozik = &vkFeatures11;
        paravozik  = &vkFeatures11.pNext;

        auto rayTracingPipelineFeaturesKHR    = vk::PhysicalDeviceRayTracingPipelineFeaturesKHR().setRayTracingPipeline(vk::True);
        auto accelerationStructureFeaturesKHR = vk::PhysicalDeviceAccelerationStructureFeaturesKHR().setAccelerationStructure(vk::True);
        auto rayQueryFeaturesKHR              = vk::PhysicalDeviceRayQueryFeaturesKHR().setRayQuery(vk::True);
        if constexpr (s_bRequireRayTracing)
        {
            *paravozik = &rayTracingPipelineFeaturesKHR;
            paravozik  = &rayTracingPipelineFeaturesKHR.pNext;

            *paravozik = &accelerationStructureFeaturesKHR;
            paravozik  = &accelerationStructureFeaturesKHR.pNext;

            *paravozik = &rayQueryFeaturesKHR;
            paravozik  = &rayQueryFeaturesKHR.pNext;
        }

        auto meshShaderFeaturesEXT =
            vk::PhysicalDeviceMeshShaderFeaturesEXT().setMeshShader(vk::True).setMeshShaderQueries(vk::True).setTaskShader(vk::True);
        if constexpr (s_bRequireMeshShading)
        {
            *paravozik = &meshShaderFeaturesEXT;
            paravozik  = &meshShaderFeaturesEXT.pNext;
        }

        auto indexTypeUint8FeaturesEXT = vk::PhysicalDeviceIndexTypeUint8FeaturesEXT().setIndexTypeUint8(vk::True);
        *paravozik                     = &indexTypeUint8FeaturesEXT;
        paravozik                      = &indexTypeUint8FeaturesEXT.pNext;

        auto dynamicState3FeaturesEXT =
            vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT().setExtendedDynamicState3DepthClampEnable(vk::True);
        *paravozik = &dynamicState3FeaturesEXT;
        paravozik  = &dynamicState3FeaturesEXT.pNext;

        constexpr vk::PhysicalDeviceFeatures vkFeatures10 = vk::PhysicalDeviceFeatures()
                                                                .setShaderInt16(vk::True)
                                                                .setShaderInt64(vk::True)
                                                                .setFillModeNonSolid(vk::True)
                                                                .setMultiDrawIndirect(vk::True)
                                                                .setSamplerAnisotropy(vk::True)
                                                                .setPipelineStatisticsQuery(vk::True)
                                                                .setDepthClamp(vk::True)
                                                                .setGeometryShader(vk::True)
                                                                .setTextureCompressionBC(s_bUseTextureCompressionBC ? vk::True : vk::False)
                                                                .setShaderStorageImageArrayDynamicIndexing(vk::True)
                                                                .setShaderSampledImageArrayDynamicIndexing(vk::True);
        SelectGPUAndCreateDeviceThings(instance, surface, requiredDeviceExtensions, vkFeatures10, &vkFeatures13);

        InitVMA(instance);
        LoadPipelineCache();
        CreateBindlessSystem();
    }

    void GfxDevice::SelectGPUAndCreateDeviceThings(const vk::UniqueInstance& instance, const vk::UniqueSurfaceKHR& surface,
                                                   std::vector<const char*>& requiredDeviceExtensions,
                                                   const vk::PhysicalDeviceFeatures& requiredDeviceFeatures, const void* pNext) noexcept
    {
        const auto gpus = instance->enumeratePhysicalDevices();
        LOG_TRACE("Found {} GPUs.", gpus.size());
        for (auto& gpu : gpus)
        {
            const auto gpuProperties = gpu.getProperties();
            LOG_WARN("\t{}", gpuProperties.deviceName.data());

            if (gpus.size() == 1 || RENDER_FORCE_IGPU && gpuProperties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu ||
                !RENDER_FORCE_IGPU && gpuProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
            {
                RDNT_ASSERT(gpuProperties.limits.timestampPeriod != 0, "{} doesn't support timestamp queries!",
                            gpuProperties.deviceName.data());

                const auto supportedDeviceExtensions = gpu.enumerateDeviceExtensionProperties();
                if (std::ranges::find_if(supportedDeviceExtensions,
                                         [](const vk::ExtensionProperties& deviceExtension) {
                                             return strcmp(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME,
                                                           deviceExtension.extensionName) == 0;
                                         }) != supportedDeviceExtensions.end() &&
                    std::ranges::find_if(supportedDeviceExtensions, [](const vk::ExtensionProperties& deviceExtension)
                                         { return strcmp(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME, deviceExtension.extensionName) == 0; }) !=
                        supportedDeviceExtensions.end())
                {
                    requiredDeviceExtensions.emplace_back(VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME);
                    requiredDeviceExtensions.emplace_back(VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME);
                    m_bMemoryPrioritySupported = true;
                }

                for (const auto& rde : requiredDeviceExtensions)
                {
                    const bool bExtensionFound = std::ranges::find_if(supportedDeviceExtensions,
                                                                      [&rde](const vk::ExtensionProperties& deviceExtension) {
                                                                          return strcmp(rde, deviceExtension.extensionName) == 0;
                                                                      }) != supportedDeviceExtensions.end();

                    RDNT_ASSERT(bExtensionFound, "Device extension: {} not supported!", rde);
                }

                m_PhysicalDevice          = gpu;
                m_GPUProperties           = gpuProperties;
                const auto maxMSAASamples = m_GPUProperties.limits.sampledImageColorSampleCounts &
                                            m_GPUProperties.limits.sampledImageDepthSampleCounts &
                                            m_GPUProperties.limits.sampledImageStencilSampleCounts;
                if (maxMSAASamples & vk::SampleCountFlagBits::e64)
                    m_MSAASamples = vk::SampleCountFlagBits::e64;
                else if (maxMSAASamples & vk::SampleCountFlagBits::e32)
                    m_MSAASamples = vk::SampleCountFlagBits::e32;
                else if (maxMSAASamples & vk::SampleCountFlagBits::e16)
                    m_MSAASamples = vk::SampleCountFlagBits::e16;
                else if (maxMSAASamples & vk::SampleCountFlagBits::e8)
                    m_MSAASamples = vk::SampleCountFlagBits::e8;
                else if (maxMSAASamples & vk::SampleCountFlagBits::e4)
                    m_MSAASamples = vk::SampleCountFlagBits::e4;
                else if (maxMSAASamples & vk::SampleCountFlagBits::e2)
                    m_MSAASamples = vk::SampleCountFlagBits::e2;

                LOG_WARN("MSAA Samples: {}", vk::to_string(m_MSAASamples));
                LOG_INFO("Chosen GPU: {}", gpuProperties.deviceName.data());
            }

            auto gpuSubgroupProperties = vk::PhysicalDeviceSubgroupProperties();
            auto gpuProperties2        = vk::PhysicalDeviceProperties2();
            gpuProperties2.pNext       = &gpuSubgroupProperties;
            gpu.getProperties2(&gpuProperties2);

            LOG_TRACE("Subgroup Size: {}", gpuSubgroupProperties.subgroupSize);
            LOG_TRACE("Subgroup Supported Shader Stages: {}", vk::to_string(gpuSubgroupProperties.supportedStages));
            LOG_TRACE("Subgroup Supported Operations: {}", vk::to_string(gpuSubgroupProperties.supportedOperations));
            LOG_TRACE("QuadOperationsInAllStages: {}", gpuSubgroupProperties.quadOperationsInAllStages ? "TRUE" : "FALSE");
        }

        std::vector<vk::QueueFamilyProperties> qfProperties = m_PhysicalDevice.getQueueFamilyProperties();
        RDNT_ASSERT(!qfProperties.empty(), "Queue Families are empty!");

        UnorderedMap<u8, u8> queueFamilyToQueueCount;
        for (u32 i{}; i < qfProperties.size(); ++i)
        {
            const auto queueCount = qfProperties[i].queueCount;
            RDNT_ASSERT(queueCount > 0, "Queue Family[{}] has no queues?!", i);

            const auto queueFlags = qfProperties[i].queueFlags;

            constexpr auto generalQueueFlags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;
            if (m_Queues[0].QueueFamilyIndex == std::numeric_limits<u8>::max() && (queueFlags & generalQueueFlags))
            {
                RDNT_ASSERT(qfProperties[i].timestampValidBits != 0, "Queue Family [{}] doesn't support timestamp queries!", i);
                RDNT_ASSERT(m_PhysicalDevice.getSurfaceSupportKHR(i, *surface), "General queue should support present!");

                m_Queues[0].QueueFamilyIndex = i;
                m_Queues[0].QueueIndex       = 0;
                m_Queues[0].Type             = ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL;

                queueFamilyToQueueCount[i] = 1;

                continue;
            }

            // Check if DMA engine is present.
            const bool bIsDedicatedTransfer = (queueFlags == vk::QueueFlagBits::eTransfer ||
                                               queueFlags == (vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eSparseBinding));
            if (m_Queues[c_TransferQueueOffsetArray].QueueFamilyIndex == std::numeric_limits<u8>::max() && bIsDedicatedTransfer)
            {
                LOG_INFO("Found DMA engine at queue family [{}]", i);

                queueFamilyToQueueCount[i] = queueCount;
                for (u32 queueIndex{}; queueIndex < queueCount; ++queueIndex)
                {
                    m_Queues[c_TransferQueueOffsetArray + queueIndex].QueueFamilyIndex = i;
                    m_Queues[c_TransferQueueOffsetArray + queueIndex].QueueIndex       = queueIndex;
                    m_Queues[c_TransferQueueOffsetArray + queueIndex].Type = ECommandQueueType::COMMAND_QUEUE_TYPE_DEDICATED_TRANSFER;
                }

                continue;
            }

            const bool bIsAsyncCompute =
                (queueFlags == vk::QueueFlagBits::eCompute ||
                 queueFlags == (vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eSparseBinding) ||
                 queueFlags == (vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer) ||
                 queueFlags == (vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eSparseBinding));
            if (m_Queues[c_ComputeQueueOffsetArray].QueueFamilyIndex == std::numeric_limits<u8>::max() && bIsAsyncCompute)
            {
                LOG_INFO("Found Async-Compute queue at family [{}]", i);
                RDNT_ASSERT(qfProperties[i].timestampValidBits != 0, "Queue Family [{}] doesn't support timestamp queries!", i);

                queueFamilyToQueueCount[i] = queueCount;
                for (u32 queueIndex{}; queueIndex < queueCount; ++queueIndex)
                {
                    m_Queues[c_ComputeQueueOffsetArray + queueIndex].QueueFamilyIndex = i;
                    m_Queues[c_ComputeQueueOffsetArray + queueIndex].QueueIndex       = queueIndex;
                    m_Queues[c_ComputeQueueOffsetArray + queueIndex].Type             = ECommandQueueType::COMMAND_QUEUE_TYPE_ASYNC_COMPUTE;
                }
            }
        }
        RDNT_ASSERT(m_Queues[0].QueueFamilyIndex != std::numeric_limits<u8>::max(), "Failed to find General Queue Family Index!");
        RDNT_ASSERT(m_Queues[c_TransferQueueOffsetArray].QueueFamilyIndex != std::numeric_limits<u8>::max(),
                    "Failed to find Dedicated-Transfer Queue Family Index!");
        RDNT_ASSERT(m_Queues[c_ComputeQueueOffsetArray].QueueFamilyIndex != std::numeric_limits<u8>::max(),
                    "Failed to find Async-Compute Queue Family Index!");
        constexpr std::array<f32, std::max(s_MaxComputeQueueCount, s_MaxTransferQueueCount)> queuePriorities = {0.0f, 0.0f, 0.0f, 0.0f,
                                                                                                                0.0f, 0.0f, 0.0f, 0.0f};

        std::vector<vk::DeviceQueueCreateInfo> queuesCI;
        for (const auto& [queueFamily, queueCount] : queueFamilyToQueueCount)
        {
            queuesCI.emplace_back().setQueuePriorities(queuePriorities).setQueueCount(queueCount).setQueueFamilyIndex(queueFamily);
        }

        const auto logicalDeviceCI = vk::DeviceCreateInfo()
                                         .setPEnabledFeatures(&requiredDeviceFeatures)
                                         .setQueueCreateInfos(queuesCI)
                                         .setEnabledExtensionCount(requiredDeviceExtensions.size())
                                         .setPEnabledExtensionNames(requiredDeviceExtensions)
                                         .setPNext(pNext);
        m_Device = m_PhysicalDevice.createDeviceUnique(logicalDeviceCI);

        // Load device functions.
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*m_Device);

        SetDebugName(m_GPUProperties.deviceName, *m_Device);

        const auto InitializeCommandQueueFunc = [&](const std::string& commandQueueName, const u8 commandQueueOffsetInsideGlobalQueueArray,
                                                    const u8 maxCommandQueueCount) noexcept
        {
            constexpr u64 c_TimelineSemaphoreInitialValue = 0;
            u32 detectedQueueCount{};
            for (u32 queueIndex = 0; queueIndex < maxCommandQueueCount; ++queueIndex)
            {
                if (m_Queues[commandQueueOffsetInsideGlobalQueueArray + queueIndex].QueueFamilyIndex == std::numeric_limits<u8>::max())
                    continue;
                ++detectedQueueCount;

                for (auto& timelineValue : m_Queues[commandQueueOffsetInsideGlobalQueueArray + queueIndex].TimelineValue)
                    timelineValue = c_TimelineSemaphoreInitialValue;

                std::ranges::for_each(m_Queues[commandQueueOffsetInsideGlobalQueueArray + queueIndex].TimelineSemaphore,
                                      [&](auto& timelineSemaphore)
                                      {
                                          const auto semaphoreTypeCI =
                                              vk::SemaphoreTypeCreateInfo()
                                                  .setInitialValue(c_TimelineSemaphoreInitialValue)
                                                  .setSemaphoreType(VULKAN_HPP_NAMESPACE::SemaphoreType::eTimeline);
                                          timelineSemaphore =
                                              m_Device->createSemaphoreUnique(vk::SemaphoreCreateInfo().setPNext(&semaphoreTypeCI));
                                      });

                const std::string currentQueueName = commandQueueName + std::to_string(queueIndex);
                m_Queues[commandQueueOffsetInsideGlobalQueueArray + queueIndex].Handle =
                    m_Device->getQueue(m_Queues[commandQueueOffsetInsideGlobalQueueArray + queueIndex].QueueFamilyIndex,
                                       m_Queues[commandQueueOffsetInsideGlobalQueueArray + queueIndex].QueueIndex);
                SetDebugName(currentQueueName.data(), m_Queues[commandQueueOffsetInsideGlobalQueueArray + queueIndex].Handle);
            }
            return detectedQueueCount;
        };

        const auto generalQueueCount = InitializeCommandQueueFunc("COMMAND_QUEUE_GENERAL", 0, 1);
        const auto asyncComputeQueueCount =
            InitializeCommandQueueFunc("COMMAND_QUEUE_ASYNC_COMPUTE_", c_ComputeQueueOffsetArray, s_MaxComputeQueueCount);
        const auto dedicatedTransferQueueCount =
            InitializeCommandQueueFunc("COMMAND_QUEUE_DEDICATED_TRANSFER_", c_TransferQueueOffsetArray, s_MaxTransferQueueCount);

        LOG_INFO("Command Queue Configuration: ");
        LOG_INFO("General Command Queue: {}", generalQueueCount);
        LOG_INFO("Async Compute Queue: {}", asyncComputeQueueCount);
        LOG_INFO("Dedicated Transfer Queue: {}", dedicatedTransferQueueCount);
    }

    void GfxDevice::InitVMA(const vk::UniqueInstance& instance) noexcept
    {
        const VmaVulkanFunctions vulkanFunctions = {.vkGetInstanceProcAddr = instance.getDispatch().vkGetInstanceProcAddr,
                                                    .vkGetDeviceProcAddr   = m_Device.getDispatch().vkGetDeviceProcAddr};

        VmaAllocatorCreateFlags allocatorCF = VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        if (m_bMemoryPrioritySupported) allocatorCF |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT;

        const VmaAllocatorCreateInfo allocatorCI = {.flags            = allocatorCF,
                                                    .physicalDevice   = m_PhysicalDevice,
                                                    .device           = *m_Device,
                                                    .pVulkanFunctions = &vulkanFunctions,
                                                    .instance         = *instance,
                                                    .vulkanApiVersion = VK_API_VERSION_1_3};
        RDNT_ASSERT(vmaCreateAllocator(&allocatorCI, &m_Allocator) == VK_SUCCESS, "Failed to create VMA!");
    }

    void GfxDevice::LoadPipelineCache() noexcept
    {
        auto pipelineCacheCI = vk::PipelineCacheCreateInfo();
        std::vector<u8> pipelineCacheBlob;

        if (std::filesystem::exists(s_PipelineCacheName))
        {
            pipelineCacheBlob = CoreUtils::LoadData<u8>(s_PipelineCacheName);

            // Validate retrieved pipeline cache.
            vk::PipelineCacheHeaderVersionOne pipelineCacheHeader{};
            std::memcpy(&pipelineCacheHeader, pipelineCacheBlob.data(), sizeof(pipelineCacheHeader));

            bool bPipelineCacheValid{!pipelineCacheBlob.empty()};
            if (bPipelineCacheValid)
            {
                bPipelineCacheValid = bPipelineCacheValid && (m_GPUProperties.vendorID == pipelineCacheHeader.vendorID);
                bPipelineCacheValid = bPipelineCacheValid && (m_GPUProperties.deviceID == pipelineCacheHeader.deviceID);
                bPipelineCacheValid =
                    bPipelineCacheValid &&
                    (std::memcmp(m_GPUProperties.pipelineCacheUUID, pipelineCacheHeader.pipelineCacheUUID, VK_UUID_SIZE * sizeof(u8)) == 0);
            }

            LOG_INFO("Found pipeline cache {}!", bPipelineCacheValid ? "valid" : "invalid");
            if (bPipelineCacheValid)
                pipelineCacheCI.setInitialDataSize(pipelineCacheBlob.size() * sizeof(pipelineCacheBlob[0]))
                    .setPInitialData(pipelineCacheBlob.data());
        }

        m_PipelineCache = m_Device->createPipelineCacheUnique(pipelineCacheCI);
    }

    void GfxDevice::CreateBindlessSystem() noexcept
    {
        constexpr std::array<vk::DescriptorSetLayoutBinding, 4> bindings{
            vk::DescriptorSetLayoutBinding()
                .setBinding(Shaders::s_BINDLESS_STORAGE_IMAGE_BINDING)
                .setDescriptorCount(Shaders::s_MAX_BINDLESS_STORAGE_IMAGES)
                .setStageFlags(vk::ShaderStageFlagBits::eAll)
                .setDescriptorType(vk::DescriptorType::eStorageImage),
            vk::DescriptorSetLayoutBinding()
                .setBinding(Shaders::s_BINDLESS_COMBINED_IMAGE_SAMPLER_BINDING)
                .setDescriptorCount(Shaders::s_MAX_BINDLESS_COMBINED_IMAGE_SAMPLERS)
                .setStageFlags(vk::ShaderStageFlagBits::eAll)
                .setDescriptorType(vk::DescriptorType::eCombinedImageSampler),
            vk::DescriptorSetLayoutBinding()
                .setBinding(Shaders::s_BINDLESS_SAMPLED_IMAGE_BINDING)
                .setDescriptorCount(Shaders::s_MAX_BINDLESS_SAMPLED_IMAGES)
                .setStageFlags(vk::ShaderStageFlagBits::eAll)
                .setDescriptorType(vk::DescriptorType::eSampledImage),
            vk::DescriptorSetLayoutBinding()
                .setBinding(Shaders::s_BINDLESS_SAMPLER_BINDING)
                .setDescriptorCount(Shaders::s_MAX_BINDLESS_SAMPLERS)
                .setStageFlags(vk::ShaderStageFlagBits::eAll)
                .setDescriptorType(vk::DescriptorType::eSampler)};

        constexpr std::array<vk::DescriptorBindingFlags, 4> bindingFlags{
            vk::FlagTraits<vk::DescriptorBindingFlagBits>::allFlags ^ vk::DescriptorBindingFlagBits::eVariableDescriptorCount,
            vk::FlagTraits<vk::DescriptorBindingFlagBits>::allFlags ^ vk::DescriptorBindingFlagBits::eVariableDescriptorCount,
            vk::FlagTraits<vk::DescriptorBindingFlagBits>::allFlags ^ vk::DescriptorBindingFlagBits::eVariableDescriptorCount,
            vk::FlagTraits<vk::DescriptorBindingFlagBits>::allFlags ^ vk::DescriptorBindingFlagBits::eVariableDescriptorCount};
        const auto megaSetLayoutExtendedInfo = vk::DescriptorSetLayoutBindingFlagsCreateInfo().setBindingFlags(bindingFlags);

        m_DescriptorSetLayout =
            m_Device->createDescriptorSetLayoutUnique(vk::DescriptorSetLayoutCreateInfo()
                                                          .setBindings(bindings)
                                                          .setFlags(vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool)
                                                          .setPNext(&megaSetLayoutExtendedInfo));
        SetDebugName("RDNT_BINDLESS_DESCRIPTOR_LAYOUT", *m_DescriptorSetLayout);

        m_PipelineLayout = m_Device->createPipelineLayoutUnique(
            vk::PipelineLayoutCreateInfo()
                .setSetLayouts(*m_DescriptorSetLayout)
                .setPushConstantRanges(vk::PushConstantRange()
                                           .setOffset(0)
                                           .setSize(/* guaranteed by the spec min bytes size of maxPushConstantsSize */ 128)
                                           .setStageFlags(vk::ShaderStageFlagBits::eAll)));
        SetDebugName("RDNT_BINDLESS_PIPELINE_LAYOUT", *m_PipelineLayout);

        constexpr std::array<vk::DescriptorPoolSize, 4> poolSizes{
            vk::DescriptorPoolSize().setDescriptorCount(Shaders::s_MAX_BINDLESS_STORAGE_IMAGES).setType(vk::DescriptorType::eStorageImage),
            vk::DescriptorPoolSize()
                .setDescriptorCount(Shaders::s_MAX_BINDLESS_COMBINED_IMAGE_SAMPLERS)
                .setType(vk::DescriptorType::eCombinedImageSampler),
            vk::DescriptorPoolSize().setDescriptorCount(Shaders::s_MAX_BINDLESS_SAMPLED_IMAGES).setType(vk::DescriptorType::eSampledImage),
            vk::DescriptorPoolSize().setDescriptorCount(Shaders::s_MAX_BINDLESS_SAMPLERS).setType(vk::DescriptorType::eSampler)};
        for (u8 i{}; i < s_BufferedFrameCount; ++i)
        {
            m_BindlessResourcesPerFrame[i].DescriptorPool =
                m_Device->createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo()
                                                         .setMaxSets(1)
                                                         .setFlags(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind)
                                                         .setPoolSizes(poolSizes));
            const auto descriptorPoolName = "RDNT_BINDLESS_DESCRIPTOR_POOL_FRAME_" + std::to_string(i);
            SetDebugName(descriptorPoolName.data(), *m_BindlessResourcesPerFrame[i].DescriptorPool);

            m_BindlessResourcesPerFrame[i].DescriptorSet =
                m_Device
                    ->allocateDescriptorSets(vk::DescriptorSetAllocateInfo()
                                                 .setDescriptorPool(*m_BindlessResourcesPerFrame[i].DescriptorPool)
                                                 .setSetLayouts(*m_DescriptorSetLayout))
                    .back();
            const auto descriptorSetName = "RDNT_BINDLESS_DESCRIPTOR_SET_FRAME_" + std::to_string(i);
            SetDebugName(descriptorSetName.data(), m_BindlessResourcesPerFrame[i].DescriptorSet);
        }
    }

    void GfxDevice::AllocateMemory(VmaAllocation& allocation, const vk::MemoryRequirements& finalMemoryRequirements,
                                   const vk::MemoryPropertyFlags preferredFlags) noexcept
    {
        const VmaAllocationCreateInfo allocationCI = {
            .flags = VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT /* | VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT*/,
            /*.usage          = VMA_MEMORY_USAGE_AUTO,*/
            .preferredFlags = (VkMemoryPropertyFlags)preferredFlags};

        RDNT_ASSERT(vmaAllocateMemory(m_Allocator, (const VkMemoryRequirements*)&finalMemoryRequirements, &allocationCI, &allocation,
                                      nullptr) == VK_SUCCESS,
                    "vmaAllocateMemory() failed!");
    }

    void GfxDevice::FreeMemory(VmaAllocation& allocation) noexcept
    {
        vmaFreeMemory(m_Allocator, allocation);
    }

    void GfxDevice::BindTexture(vk::Image& image, const VmaAllocation& allocation, const u64 allocationLocalOffset) const noexcept
    {
        RDNT_ASSERT(vmaBindImageMemory2(m_Allocator, allocation, allocationLocalOffset, (VkImage&)image, nullptr) == VK_SUCCESS,
                    "vmaBindImageMemory2() failed!");
    }

    void GfxDevice::AllocateTexture(const vk::ImageCreateInfo& imageCI, VkImage& image, VmaAllocation& allocation) const noexcept
    {
        const VmaAllocationCreateInfo allocationCI = {/*.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,*/ .usage =
                                                          VMA_MEMORY_USAGE_GPU_ONLY,
                                                      .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};

        const VkImageCreateInfo& oldVkImageCI = imageCI;
        RDNT_ASSERT(vmaCreateImage(m_Allocator, &oldVkImageCI, &allocationCI, &image, &allocation, nullptr) == VK_SUCCESS,
                    "VMA: Failed to allocate image!");
    }

    void GfxDevice::DeallocateTexture(VkImage& image, VmaAllocation& allocation) const noexcept
    {
        vmaDestroyImage(m_Allocator, image, allocation);
    }

    void GfxDevice::BindBuffer(vk::Buffer& buffer, const VmaAllocation& allocation, const u64 allocationLocalOffset) const noexcept
    {
        RDNT_ASSERT(vmaBindBufferMemory2(m_Allocator, allocation, allocationLocalOffset, (VkBuffer&)buffer, nullptr) == VK_SUCCESS,
                    "vmaBindBufferMemory2() failed!");
    }

    void GfxDevice::AllocateBuffer(const ExtraBufferFlags extraBufferFlags, const vk::BufferCreateInfo& bufferCI, VkBuffer& buffer,
                                   VmaAllocation& allocation) const noexcept
    {
        const bool bIsReBARRequired = extraBufferFlags == EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT;
        const bool bIsDeviceLocal   = (extraBufferFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT) &&
                                    !(extraBufferFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT);

        VkMemoryPropertyFlags requiredFlags{0};
        VmaAllocationCreateFlags allocationCreateFlags{0};
        if (bIsReBARRequired /* ReBAR means VRAM writeable by PCIe from CPU, so it's device local! */)
        {
            requiredFlags =
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            allocationCreateFlags |=
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
        }
        else if (bIsDeviceLocal)
            requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        else
            allocationCreateFlags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

        const VmaAllocationCreateInfo allocationCI = {
            .flags = allocationCreateFlags, .usage = VMA_MEMORY_USAGE_AUTO, .requiredFlags = requiredFlags};

        const VkBufferCreateInfo& oldVkBufferCI = bufferCI;
        RDNT_ASSERT(vmaCreateBuffer(m_Allocator, &oldVkBufferCI, &allocationCI, &buffer, &allocation, nullptr) == VK_SUCCESS,
                    "VMA: Failed to allocate buffer!");
    }

    void GfxDevice::DeallocateBuffer(VkBuffer& buffer, VmaAllocation& allocation) const noexcept
    {
        vmaDestroyBuffer(m_Allocator, buffer, allocation);
    }

    void* GfxDevice::Map(VmaAllocation& allocation) const noexcept
    {
        void* mapped{nullptr};

        RDNT_ASSERT(vmaMapMemory(m_Allocator, allocation, &mapped) == VK_SUCCESS, "VMA: Failed to map memory!");
        return mapped;
    }

    void GfxDevice::Unmap(VmaAllocation& allocation) const noexcept
    {
        vmaUnmapMemory(m_Allocator, allocation);
    }

    void GfxDevice::PollDeletionQueues(const bool bImmediate) noexcept
    {
        UnorderedSet<u64> queuesToRemove;

        for (auto& [frameNumber, deletionQueue] : m_DeletionQueuesPerFrame)
        {
            // We have to make sure that all buffered frames stopped using our resource!
            const u64 framesPast = frameNumber + static_cast<u64>(s_BufferedFrameCount);
            if (!bImmediate && framesPast >= m_CurrentFrameNumber) continue;

            deletionQueue.Flush();
            for (auto it = deletionQueue.BufferHandlesDeque.rbegin(); it != deletionQueue.BufferHandlesDeque.rend(); ++it)
                DeallocateBuffer((VkBuffer&)it->first, (VmaAllocation&)it->second);

            queuesToRemove.emplace(frameNumber);
        }

        for (const auto queueFrameNumber : queuesToRemove)
            m_DeletionQueuesPerFrame.erase(queueFrameNumber);

        if (!queuesToRemove.empty()) LOG_TRACE("{}: freed {} deletion queues.", __FUNCTION__, queuesToRemove.size());
    }

    void GfxDevice::Shutdown() noexcept
    {
        m_Device->waitIdle();
        PollDeletionQueues(true);

        for (auto& [samplerCI, samplerPair] : m_SamplerMap)
            PopBindlessThing(samplerPair.second, Shaders::s_BINDLESS_SAMPLER_BINDING);

        vmaDestroyAllocator(m_Allocator);
        CoreUtils::SaveData(s_PipelineCacheName, m_Device->getPipelineCacheData(*m_PipelineCache));
    }

}  // namespace Radiant
