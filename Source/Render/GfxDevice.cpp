#include <pch.h>
#include "GfxDevice.hpp"

#define VMA_IMPLEMENTATION
#define VK_NO_PROTOTYPES
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

#include <vulkan/vulkan_to_string.hpp>

namespace Radiant
{
    void GfxDevice::Init(const vk::UniqueInstance& instance, const vk::UniqueSurfaceKHR& surface) noexcept
    {
        std::vector<const char*> requiredDeviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,                 // For rendering into OS-window
            VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,  // To neglect viewport state definition on pipeline creation
        };

        auto vkFeatures13 = vk::PhysicalDeviceVulkan13Features()
                                .setDynamicRendering(vk::True)
                                .setSynchronization2(vk::True)
                                .setShaderDemoteToHelperInvocation(vk::True)  // NOTE: Fucking slang requires it
                                .setMaintenance4(vk::True);

        // The train starts here...
        void** paravozik = &vkFeatures13.pNext;

        auto vkFeatures12 = vk::PhysicalDeviceVulkan12Features()
                                .setBufferDeviceAddress(vk::True)
                                .setScalarBlockLayout(vk::True)
#if !RENDER_FORCE_IGPU
                                .setStoragePushConstant8(vk::True)
#endif
                                .setShaderInt8(vk::True)
                                .setShaderFloat16(vk::True)
                                .setTimelineSemaphore(vk::True)
                                .setHostQueryReset(vk::True)
                                .setDescriptorIndexing(vk::True)
                                .setDescriptorBindingPartiallyBound(vk::True)
                                .setDescriptorBindingSampledImageUpdateAfterBind(vk::True)
                                .setDescriptorBindingStorageImageUpdateAfterBind(vk::True)
                                .setDescriptorBindingUpdateUnusedWhilePending(vk::True)
                                .setRuntimeDescriptorArray(vk::True);

        *paravozik = &vkFeatures12;
        paravozik  = &vkFeatures12.pNext;

        auto vkFeatures11 = vk::PhysicalDeviceVulkan11Features()
#if !RENDER_FORCE_IGPU
                                .setStoragePushConstant16(vk::True)
#endif
                                .setVariablePointers(vk::True)                // NOTE: Fucking slang requires it
                                .setVariablePointersStorageBuffer(vk::True);  // NOTE: Fucking slang requires it

        *paravozik = &vkFeatures11;
        paravozik  = &vkFeatures11.pNext;

        constexpr vk::PhysicalDeviceFeatures requiredDeviceFeatures = vk::PhysicalDeviceFeatures()
                                                                          .setShaderInt16(vk::True)
                                                                          .setShaderInt64(vk::True)
                                                                          .setFillModeNonSolid(vk::True)
                                                                          .setSamplerAnisotropy(vk::True)
                                                                          .setPipelineStatisticsQuery(vk::True);
        SelectGPUAndCreateDeviceThings(instance, surface, requiredDeviceExtensions, requiredDeviceFeatures, &vkFeatures13);

        InitVMA(instance);
        LoadPipelineCache();
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

                const auto deviceExtensions = gpu.enumerateDeviceExtensionProperties();

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
                    m_bMemoryPrioritySupported = true;
                }

                for (const auto& rde : requiredDeviceExtensions)
                {
                    const bool bExtensionFound = std::find_if(deviceExtensions.cbegin(), deviceExtensions.cend(),
                                                              [&rde](const vk::ExtensionProperties& deviceExtension) {
                                                                  return strcmp(rde, deviceExtension.extensionName) == 0;
                                                              }) != deviceExtensions.end();

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

        for (u32 i{}; i < qfProperties.size(); ++i)
        {
            const auto queueCount = qfProperties[i].queueCount;
            RDNT_ASSERT(queueCount > 0, "Queue Family[{}] has no queues?!", i);

            const auto queueFlags = qfProperties[i].queueFlags;

            constexpr auto generalQueueFlags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;
            if (!m_GeneralQueue.QueueFamilyIndex.has_value() && queueFlags & generalQueueFlags)
            {
                m_GeneralQueue.QueueFamilyIndex = i;
                RDNT_ASSERT(qfProperties[i].timestampValidBits != 0, "Queue Family [{}] doesn't support timestamp queries!", i);

                RDNT_ASSERT(m_PhysicalDevice.getSurfaceSupportKHR(i, *surface), "Dedicated present queue not supported right now!");
                if (!m_PresentQueue.QueueFamilyIndex.has_value()) m_PresentQueue.QueueFamilyIndex = i;

                continue;
            }

            if (!m_PresentQueue.QueueFamilyIndex.has_value() && m_PhysicalDevice.getSurfaceSupportKHR(i, *surface))
            {
                LOG_INFO("Found Dedicated-Present queue at family [{}] ??", i);
                m_PresentQueue.QueueFamilyIndex = i;
                continue;
            }

            // Check if DMA engine is present.
            const bool bIsDedicatedTransfer = (queueFlags == vk::QueueFlagBits::eTransfer ||
                                               queueFlags == (vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eSparseBinding));
            if (!m_TransferQueue.QueueFamilyIndex.has_value() && bIsDedicatedTransfer)
            {
                LOG_INFO("Found DMA engine at queue family [{}]", i);
                m_TransferQueue.QueueFamilyIndex = i;
                continue;
            }

            const bool bIsAsyncCompute =
                (queueFlags == vk::QueueFlagBits::eCompute ||
                 queueFlags == (vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eSparseBinding) ||
                 queueFlags == (vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer) ||
                 queueFlags == (vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eSparseBinding));
            if (!m_ComputeQueue.QueueFamilyIndex.has_value() && bIsAsyncCompute)
            {
                LOG_INFO("Found Async-Compute queue at family [{}]", i);
                m_ComputeQueue.QueueFamilyIndex = i;
                RDNT_ASSERT(qfProperties[i].timestampValidBits != 0, "Queue Family [{}] doesn't support timestamp queries!", i);
            }
        }
        RDNT_ASSERT(m_GeneralQueue.QueueFamilyIndex.has_value() && m_PresentQueue.QueueFamilyIndex.has_value() &&
                        *m_GeneralQueue.QueueFamilyIndex == *m_PresentQueue.QueueFamilyIndex,
                    "General Queue Family Index should contain present support!");
        RDNT_ASSERT(m_GeneralQueue.QueueFamilyIndex.has_value(), "Failed to find General Queue Family Index!");
        RDNT_ASSERT(m_PresentQueue.QueueFamilyIndex.has_value(), "Failed to find Present Queue Family Index!");
        RDNT_ASSERT(m_TransferQueue.QueueFamilyIndex.has_value(), "Failed to find Dedicated-Transfer Queue Family Index!");
        RDNT_ASSERT(m_ComputeQueue.QueueFamilyIndex.has_value(), "Failed to find Async-Compute Queue Family Index!");

        constexpr f32 queuePriority = 1.0f;
        std::vector<vk::DeviceQueueCreateInfo> queuesCI;
        for (const std::set<u32> uniqueQFIndices{*m_GeneralQueue.QueueFamilyIndex, *m_PresentQueue.QueueFamilyIndex,
                                                 *m_TransferQueue.QueueFamilyIndex, *m_ComputeQueue.QueueFamilyIndex};
             const auto qfIndex : uniqueQFIndices)
        {
            queuesCI.emplace_back().setQueuePriorities(queuePriority).setQueueCount(1).setQueueFamilyIndex(qfIndex);
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

        m_GeneralQueue.Handle  = m_Device->getQueue(*m_GeneralQueue.QueueFamilyIndex, 0);
        m_PresentQueue.Handle  = m_Device->getQueue(*m_PresentQueue.QueueFamilyIndex, 0);
        m_TransferQueue.Handle = m_Device->getQueue(*m_TransferQueue.QueueFamilyIndex, 0);
        m_ComputeQueue.Handle  = m_Device->getQueue(*m_ComputeQueue.QueueFamilyIndex, 0);

        SetDebugName("COMMAND_QUEUE_TRANSFER", m_TransferQueue.Handle);
        SetDebugName("COMMAND_QUEUE_COMPUTE", m_ComputeQueue.Handle);
        SetDebugName("COMMAND_QUEUE_GRAPHICS_PRESENT", m_GeneralQueue.Handle);
        if (m_GeneralQueue.Handle != m_PresentQueue.Handle)
        {
            SetDebugName("COMMAND_QUEUE_PRESENT", m_PresentQueue.Handle);
            SetDebugName("COMMAND_QUEUE_GRAPHICS", m_GeneralQueue.Handle);
        }
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
        if (std::filesystem::exists("pso_cache.bin"))
        {
            pipelineCacheBlob = CoreUtils::LoadData<u8>("pso_cache.bin");

            // Validate retrieved pipeline cache.
            vk::PipelineCacheHeaderVersionOne pipelineCacheHeader{};
            std::memcpy(&pipelineCacheHeader, pipelineCacheBlob.data(), sizeof(pipelineCacheHeader));

            bool bPipelineCacheValid{true};
            if (!pipelineCacheBlob.empty())
            {
                bPipelineCacheValid = bPipelineCacheValid && (m_GPUProperties.vendorID == pipelineCacheHeader.vendorID);
                bPipelineCacheValid = bPipelineCacheValid && (m_GPUProperties.deviceID == pipelineCacheHeader.deviceID);
                bPipelineCacheValid =
                    bPipelineCacheValid &&
                    (std::memcmp(m_GPUProperties.pipelineCacheUUID, pipelineCacheHeader.pipelineCacheUUID, VK_UUID_SIZE * sizeof(u8)) == 0);
            }

            LOG_INFO("Found pipeline cache {}!", bPipelineCacheValid ? "valid" : "invalid");

            // NOTE: Currently on my AMD iGPU loading pipeline caches throws exception from (bcryptprimitives.dll)
            if (bPipelineCacheValid && !RENDER_FORCE_IGPU)
                pipelineCacheCI.setInitialDataSize(pipelineCacheBlob.size() * pipelineCacheBlob[0])
                    .setPInitialData(pipelineCacheBlob.data());
        }

        m_PipelineCache = m_Device->createPipelineCacheUnique(pipelineCacheCI);
    }

    void GfxDevice::AllocateMemory(VmaAllocation& allocation, const vk::MemoryRequirements& finalMemoryRequirements,
                                   const vk::MemoryPropertyFlags preferredFlags) noexcept
    {
        const VmaAllocationCreateInfo allocationCI = {/*.usage          = VMA_MEMORY_USAGE_AUTO,*/
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
                                                          VMA_MEMORY_USAGE_GPU_ONLY};

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
        const bool bIsReBARRequired = extraBufferFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_RESIZABLE_BAR_BIT;
        const bool bIsDeviceLocal   = extraBufferFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_DEVICE_LOCAL_BIT;

        VmaAllocationCreateFlags allocationCreateFlags{0};
        if (bIsReBARRequired /* ReBAR means VRAM writeable by PCIe from CPU, so it's device local! */)
        {
            allocationCreateFlags |=
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
        }
        else if (!bIsDeviceLocal)
        {
            allocationCreateFlags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        }

        const VmaAllocationCreateInfo allocationCI = {
            .flags         = allocationCreateFlags,
            .usage         = VMA_MEMORY_USAGE_AUTO,
            .requiredFlags = bIsDeviceLocal && !bIsReBARRequired ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : VkMemoryPropertyFlags{0}};

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
            {
                DeallocateBuffer((VkBuffer&)it->first, (VmaAllocation&)it->second);
            }
            deletionQueue.BufferHandlesDeque.clear();

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

        vmaDestroyAllocator(m_Allocator);
        CoreUtils::SaveData("pso_cache.bin", m_Device->getPipelineCacheData(*m_PipelineCache));
    }

}  // namespace Radiant
