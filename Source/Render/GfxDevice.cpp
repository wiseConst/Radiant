#include <pch.h>
#include "GfxDevice.hpp"

#define VMA_IMPLEMENTATION
#define VK_NO_PROTOTYPES
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

namespace Radiant
{
    void GfxDevice::Init(const vk::UniqueInstance& instance, const vk::UniqueSurfaceKHR& surface) noexcept
    {
        std::vector<const char*> requiredDeviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,                // For rendering into OS-window
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,        // Neglect render passes, required by ImGui, core in vk 1.3
            VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME  // To neglect viewport state definition on pipeline creation
        };

        auto vkFeatures13 =
            vk::PhysicalDeviceVulkan13Features().setDynamicRendering(vk::True).setSynchronization2(vk::True).setMaintenance4(vk::True);

        // The train starts here...
        void** paravozik = &vkFeatures13.pNext;

        auto vkFeatures12 = vk::PhysicalDeviceVulkan12Features()
                                .setBufferDeviceAddress(vk::True)
                                .setScalarBlockLayout(vk::True)
                                .setShaderInt8(vk::True)
                                .setShaderFloat16(vk::True)
                                .setTimelineSemaphore(vk::True)
                                .setDescriptorIndexing(vk::True)
                                .setDescriptorBindingPartiallyBound(vk::True)
                                .setDescriptorBindingSampledImageUpdateAfterBind(vk::True)
                                .setDescriptorBindingStorageImageUpdateAfterBind(vk::True)
                                .setDescriptorBindingUpdateUnusedWhilePending(vk::True)
                                .setRuntimeDescriptorArray(vk::True);

        *paravozik = &vkFeatures12;
        paravozik  = &vkFeatures12.pNext;

        auto vkFeatures11 = vk::PhysicalDeviceVulkan11Features().setVariablePointers(vk::True).setVariablePointersStorageBuffer(vk::True);

        *paravozik = &vkFeatures11;
        paravozik  = &vkFeatures11.pNext;

        constexpr vk::PhysicalDeviceFeatures requiredDeviceFeatures = vk::PhysicalDeviceFeatures()
                                                                          .setShaderInt16(vk::True)
                                                                          .setShaderInt64(vk::True)
                                                                          .setFillModeNonSolid(vk::True)
                                                                          .setSamplerAnisotropy(vk::True);
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
            LOG_TRACE("\t{}", gpuProperties.deviceName.data());

            if (gpus.size() == 1 || s_bForceIGPU && gpuProperties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu ||
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

                m_PhysicalDevice          = gpu;
                m_GPUProperties           = gpuProperties;
                const auto maxMSAASamples = m_GPUProperties.limits.sampledImageColorSampleCounts &
                                            m_GPUProperties.limits.sampledImageDepthSampleCounts &
                                            m_GPUProperties.limits.sampledImageStencilSampleCounts;
                if ((maxMSAASamples & vk::SampleCountFlagBits::e64) == vk::SampleCountFlagBits::e64)
                {
                    m_MSAASamples = vk::SampleCountFlagBits::e64;
                }
                else if ((maxMSAASamples & vk::SampleCountFlagBits::e32) == vk::SampleCountFlagBits::e32)
                {
                    m_MSAASamples = vk::SampleCountFlagBits::e32;
                }
                else if ((maxMSAASamples & vk::SampleCountFlagBits::e16) == vk::SampleCountFlagBits::e16)
                {
                    m_MSAASamples = vk::SampleCountFlagBits::e16;
                }
                else if ((maxMSAASamples & vk::SampleCountFlagBits::e8) == vk::SampleCountFlagBits::e8)
                {
                    m_MSAASamples = vk::SampleCountFlagBits::e8;
                }
                else if ((maxMSAASamples & vk::SampleCountFlagBits::e4) == vk::SampleCountFlagBits::e4)
                {
                    m_MSAASamples = vk::SampleCountFlagBits::e4;
                }
                else if ((maxMSAASamples & vk::SampleCountFlagBits::e2) == vk::SampleCountFlagBits::e2)
                {
                    m_MSAASamples = vk::SampleCountFlagBits::e2;
                }
                LOG_INFO("Chosen GPU: {}", gpuProperties.deviceName.data());
            }
        }

        std::vector<vk::QueueFamilyProperties> qfProperties = m_PhysicalDevice.getQueueFamilyProperties();
        RDNT_ASSERT(!qfProperties.empty(), "Queue Families are empty!");

        for (std::uint32_t i{}; i < qfProperties.size(); ++i)
        {
            const auto queueCount = qfProperties[i].queueCount;
            RDNT_ASSERT(queueCount > 0, "Queue Family[{}] has no queues?!", i);

            const auto queueFlags = qfProperties[i].queueFlags;

            constexpr auto generalQueueFlags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;
            if (!m_GeneralQueue.QueueFamilyIndex.has_value() && (queueFlags & generalQueueFlags) == generalQueueFlags)
            {
                m_GeneralQueue.QueueFamilyIndex = i;

                if (!m_PresentQueue.QueueFamilyIndex.has_value() && m_PhysicalDevice.getSurfaceSupportKHR(i, *surface))
                    m_PresentQueue.QueueFamilyIndex = i;

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
            }
        }
        RDNT_ASSERT(m_GeneralQueue.QueueFamilyIndex.has_value(), "Failed to find General Queue Family Index!");
        RDNT_ASSERT(m_PresentQueue.QueueFamilyIndex.has_value(), "Failed to find Present Queue Family Index!");
        RDNT_ASSERT(m_TransferQueue.QueueFamilyIndex.has_value(), "Failed to find Dedicated-Transfer Queue Family Index!");
        RDNT_ASSERT(m_ComputeQueue.QueueFamilyIndex.has_value(), "Failed to find Async-Compute Queue Family Index!");

        constexpr float queuePriority = 1.0f;
        std::vector<vk::DeviceQueueCreateInfo> queuesCI;
        for (const std::set<std::uint32_t> uniqueQFIndices{*m_GeneralQueue.QueueFamilyIndex, *m_PresentQueue.QueueFamilyIndex,
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
        // TODO: Expand flags to VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT based on extension availability
        const VmaVulkanFunctions vulkanFunctions = {.vkGetInstanceProcAddr = instance.getDispatch().vkGetInstanceProcAddr,
                                                    .vkGetDeviceProcAddr   = m_Device.getDispatch().vkGetDeviceProcAddr};
        const VmaAllocatorCreateInfo allocatorCI = {.flags = VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT |
                                                             VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
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
        std::vector<std::uint8_t> pipelineCacheBlob;
        if (std::filesystem::exists("pso_cache.bin"))
        {
            pipelineCacheBlob = CoreUtils::LoadData<std::uint8_t>("pso_cache.bin");

            // Validate retrieved pipeline cache.
            vk::PipelineCacheHeaderVersionOne pipelineCacheHeader{};
            std::memcpy(&pipelineCacheHeader, pipelineCacheBlob.data(), sizeof(pipelineCacheHeader));

            bool bPipelineCacheValid{true};
            if (!pipelineCacheBlob.empty())
            {
                bPipelineCacheValid = bPipelineCacheValid && (m_GPUProperties.vendorID == pipelineCacheHeader.vendorID);
                bPipelineCacheValid = bPipelineCacheValid && (m_GPUProperties.deviceID == pipelineCacheHeader.deviceID);
                bPipelineCacheValid =
                    bPipelineCacheValid && (std::memcmp(m_GPUProperties.pipelineCacheUUID, pipelineCacheHeader.pipelineCacheUUID,
                                                        VK_UUID_SIZE * sizeof(std::uint8_t)) == 0);
            }

            LOG_INFO("Found pipeline cache {}!", bPipelineCacheValid ? "valid" : "invalid");

            // NOTE: Currently on my AMD iGPU loading pipeline caches throws exception from (bcryptprimitives.dll)
            if (bPipelineCacheValid && !s_bForceIGPU)
                pipelineCacheCI.setInitialDataSize(pipelineCacheBlob.size() * pipelineCacheBlob[0])
                    .setPInitialData(pipelineCacheBlob.data());
        }

        m_PipelineCache = m_Device->createPipelineCacheUnique(pipelineCacheCI);
    }

    void GfxDevice::AllocateTexture(const vk::ImageCreateInfo& imageCI, VkImage& image, VmaAllocation& allocation) const noexcept
    {
        const VmaAllocationCreateInfo allocationCI = {.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage = VMA_MEMORY_USAGE_AUTO};

        const VkImageCreateInfo& oldVkImageCI = imageCI;
        RDNT_ASSERT(vmaCreateImage(m_Allocator, &oldVkImageCI, &allocationCI, &image, &allocation, nullptr) == VK_SUCCESS,
                    "VMA: Failed to allocate image!");
    }

    void GfxDevice::DeallocateTexture(VkImage& image, VmaAllocation& allocation) const noexcept
    {
        vmaDestroyImage(m_Allocator, image, allocation);
    }

    void GfxDevice::AllocateBuffer(const ExtraBufferFlags extraBufferFlags, const vk::BufferCreateInfo& bufferCI, VkBuffer& buffer,
                                   VmaAllocation& allocation) const noexcept
    {
        const bool bIsDeviceLocal =
            (extraBufferFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL) == EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL;
        const VmaAllocationCreateFlags allocationCreateFlags =
            bIsDeviceLocal ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        const VmaAllocationCreateInfo allocationCI = {.flags = allocationCreateFlags,
                                                      .usage = VMA_MEMORY_USAGE_AUTO,
                                                      .requiredFlags =
                                                          bIsDeviceLocal ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : VkMemoryPropertyFlags{0}};

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

    void GfxDevice::Shutdown() noexcept
    {
        m_Device->waitIdle();
        PollDeletionQueues(true);

        vmaDestroyAllocator(m_Allocator);
        CoreUtils::SaveData("pso_cache.bin", m_Device->getPipelineCacheData(*m_PipelineCache));
    }

}  // namespace Radiant
