#pragma once

#include <Render/CoreDefines.hpp>
#include <vulkan/vulkan.hpp>

#define VK_NO_PROTOTYPES
#include <vk_mem_alloc.h>

#include <deque>

template <> struct ankerl::unordered_dense::hash<vk::SamplerCreateInfo>
{
    using is_avalanching = void;

    [[nodiscard]] auto operator()(const vk::SamplerCreateInfo& x) const noexcept -> std::uint64_t
    {
        std::uint64_t pNextHash{0};
        const void* pNext = x.pNext;
        while (pNext)
        {
            const auto* baseStructure = (vk::BaseInStructure*)pNext;
            if (!baseStructure) break;

            if (baseStructure->sType == vk::StructureType::eSamplerReductionModeCreateInfo)
            {
                const auto* samplerReductionCI = (vk::SamplerReductionModeCreateInfo*)pNext;
                if (!samplerReductionCI) continue;

                pNextHash += detail::wyhash::hash(static_cast<std::uint64_t>(samplerReductionCI->reductionMode));
            }

            pNext = baseStructure->pNext ? baseStructure->pNext : nullptr;
        }

        return pNextHash + detail::wyhash::hash(static_cast<std::uint64_t>(x.magFilter) + static_cast<std::uint64_t>(x.minFilter)) +
               detail::wyhash::hash(static_cast<std::uint64_t>(x.mipmapMode) + static_cast<std::uint64_t>(x.addressModeU) +
                                    static_cast<std::uint64_t>(x.addressModeV) + static_cast<std::uint64_t>(x.addressModeW) +
                                    static_cast<std::uint64_t>(x.mipLodBias)) +
               detail::wyhash::hash(static_cast<std::uint64_t>(x.anisotropyEnable) + static_cast<std::uint64_t>(x.maxAnisotropy) +
                                    static_cast<std::uint64_t>(x.compareEnable) + static_cast<std::uint64_t>(x.compareOp)) +
               detail::wyhash::hash(static_cast<std::uint64_t>(x.minLod) + static_cast<std::uint64_t>(x.maxLod) +
                                    static_cast<std::uint64_t>(x.borderColor));
    }
};

namespace Radiant
{

    struct GfxBindlessStatistics
    {
        u32 StorageImagesUsed{0};
        u32 CombinedImageSamplersUsed{0};
        u32 SampledImagesUsed{0};
        u32 SamplersUsed{0};
    };

    class GfxDevice final : private Uncopyable, private Unmovable
    {
      public:
        GfxDevice(const vk::UniqueInstance& instance, const vk::UniqueSurfaceKHR& surface) noexcept
        {
            RDNT_ASSERT(instance && surface, "Instance or surface are invalid!");
            Init(instance, surface);
        }
        ~GfxDevice() noexcept { Shutdown(); };

        FORCEINLINE void WaitIdle() const noexcept { m_Device->waitIdle(); }

        NODISCARD FORCEINLINE const auto& GetPhysicalDevice() const noexcept { return m_PhysicalDevice; }
        NODISCARD FORCEINLINE const auto& GetLogicalDevice() const noexcept { return m_Device; }
        NODISCARD FORCEINLINE const auto& GetPipelineCache() const noexcept
        {
            RDNT_ASSERT(m_PipelineCache, "PipelineCache not valid!");
            return *m_PipelineCache;
        }
        NODISCARD FORCEINLINE const auto& GetGeneralQueue() const noexcept { return m_Queues[0]; }
        NODISCARD FORCEINLINE const auto& GetTransferQueue(const u8 queueIndex = 0) const noexcept
        {
            RDNT_ASSERT(queueIndex < s_MaxTransferQueueCount, "Queue index is greater than: {}", s_MaxTransferQueueCount);
            RDNT_ASSERT(m_Queues[c_TransferQueueOffsetArray + queueIndex].QueueFamilyIndex != std::numeric_limits<u8>::max(),
                        "Queue doesn't exist!");
            return m_Queues[c_TransferQueueOffsetArray + queueIndex];
        }
        NODISCARD FORCEINLINE const auto& GetComputeQueue(const u8 queueIndex = 0) const noexcept
        {
            RDNT_ASSERT(queueIndex < s_MaxComputeQueueCount, "Queue index is greater than: {}", s_MaxComputeQueueCount);
            RDNT_ASSERT(m_Queues[c_ComputeQueueOffsetArray + queueIndex].QueueFamilyIndex != std::numeric_limits<u8>::max(),
                        "Queue doesn't exist!");
            return m_Queues[c_ComputeQueueOffsetArray + queueIndex];
        }
        NODISCARD FORCEINLINE const auto& GetGPUProperties() const noexcept { return m_GPUProperties; }

        template <typename TObject> constexpr void SetDebugName(const std::string& name, const TObject& object) const noexcept
        {
#if RDNT_DEBUG
            std::scoped_lock lock(m_Mtx);
            m_Device->setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT()
                                                     .setPObjectName(name.data())
                                                     .setObjectType(object.objectType)
                                                     .setObjectHandle(u64(TObject::NativeType(object))));
#endif
        }

        operator const vk::Device&() const noexcept
        {
            RDNT_ASSERT(m_Device, "LogicalDevice not valid!");
            return *m_Device;
        }

        FORCEINLINE void PushObjectToDelete(std::move_only_function<void() noexcept>&& func) noexcept
        {
            std::scoped_lock lock(m_Mtx);
            m_DeletionQueuesPerFrame[m_CurrentFrameNumber].EmplaceBack(std::forward<std::move_only_function<void() noexcept>>(func));
        }

        FORCEINLINE void PushObjectToDelete(vk::UniquePipeline&& pipeline) noexcept
        {
            std::scoped_lock lock(m_Mtx);
            m_DeletionQueuesPerFrame[m_CurrentFrameNumber].EmplaceBack(std::forward<vk::UniquePipeline>(pipeline));
        }

        FORCEINLINE void PushObjectToDelete(vk::Buffer&& buffer, VmaAllocation&& allocation) noexcept
        {
            std::scoped_lock lock(m_Mtx);
            m_DeletionQueuesPerFrame[m_CurrentFrameNumber].EmplaceBack(std::forward<vk::Buffer>(buffer),
                                                                       std::forward<VmaAllocation>(allocation));
        }

        void AllocateMemory(VmaAllocation& allocation, const vk::MemoryRequirements& finalMemoryRequirements,
                            const vk::MemoryPropertyFlags preferredFlags) noexcept;
        void FreeMemory(VmaAllocation& allocation) noexcept;

        void BindTexture(vk::Image& image, const VmaAllocation& allocation, const u64 allocationLocalOffset) const noexcept;
        void AllocateTexture(const vk::ImageCreateInfo& imageCI, VkImage& image, VmaAllocation& allocation) const noexcept;
        void DeallocateTexture(VkImage& image, VmaAllocation& allocation) const noexcept;

        void BindBuffer(vk::Buffer& buffer, const VmaAllocation& allocation, const u64 allocationLocalOffset) const noexcept;
        void AllocateBuffer(const ExtraBufferFlags extraBufferFlags, const vk::BufferCreateInfo& bufferCI, VkBuffer& buffer,
                            VmaAllocation& allocation) const noexcept;
        void DeallocateBuffer(VkBuffer& buffer, VmaAllocation& allocation) const noexcept;

        void* Map(VmaAllocation& allocation) const noexcept;
        void Unmap(VmaAllocation& allocation) const noexcept;

        NODISCARD std::pair<const vk::Sampler&, u32> GetSampler(const vk::SamplerCreateInfo& samplerCI) noexcept
        {
            std::scoped_lock lock(m_Mtx);
            if (!m_SamplerMap.contains(samplerCI))
            {
                m_SamplerMap[samplerCI].first = m_Device->createSamplerUnique(samplerCI);
                PushBindlessThing(vk::DescriptorImageInfo().setSampler(*m_SamplerMap[samplerCI].first), m_SamplerMap[samplerCI].second,
                                  Shaders::s_BINDLESS_SAMPLER_BINDING);
            }
            return {*m_SamplerMap[samplerCI].first, *m_SamplerMap[samplerCI].second};
        }

        NODISCARD std::pair<const vk::Sampler&, u32> GetDefaultSampler() noexcept
        {
            constexpr auto defaultSamplerCI = vk::SamplerCreateInfo()
                                                  .setUnnormalizedCoordinates(vk::False)
                                                  .setAddressModeU(vk::SamplerAddressMode::eRepeat)
                                                  .setAddressModeV(vk::SamplerAddressMode::eRepeat)
                                                  .setAddressModeW(vk::SamplerAddressMode::eRepeat)
                                                  .setMagFilter(vk::Filter::eNearest)
                                                  .setMinFilter(vk::Filter::eNearest)
                                                  .setMipmapMode(vk::SamplerMipmapMode::eNearest)
                                                  .setMinLod(0.0f)
                                                  .setMaxLod(vk::LodClampNone)
                                                  .setBorderColor(vk::BorderColor::eIntOpaqueBlack);
            return GetSampler(defaultSamplerCI);
        }

        NODISCARD FORCEINLINE auto& GetBindlessPipelineLayout() const noexcept { return *m_PipelineLayout; }
        FORCEINLINE const auto GetMSAASamples() const noexcept { return m_MSAASamples; }

        void PushBindlessThing(const vk::DescriptorImageInfo& imageInfo, std::optional<u32>& bindlessID, const u32 binding) noexcept
        {
            std::scoped_lock lock(m_BindlessThingsMtx);
            RDNT_ASSERT(binding == Shaders::s_BINDLESS_STORAGE_IMAGE_BINDING || binding == Shaders::s_BINDLESS_SAMPLER_BINDING ||
                            binding == Shaders::s_BINDLESS_COMBINED_IMAGE_SAMPLER_BINDING ||
                            binding == Shaders::s_BINDLESS_SAMPLED_IMAGE_BINDING,
                        "Unknown binding!");
            RDNT_ASSERT(!bindlessID.has_value(), "BindlessID is already populated!");

            if (binding != Shaders::s_BINDLESS_SAMPLER_BINDING) RDNT_ASSERT(imageInfo.imageView, "ImageView is invalid!");
            if (binding == Shaders::s_BINDLESS_SAMPLER_BINDING || binding == Shaders::s_BINDLESS_COMBINED_IMAGE_SAMPLER_BINDING)
                RDNT_ASSERT(imageInfo.sampler, "Sampler is invalid!");

            bindlessID = static_cast<u32>(m_BindlessThingsIDs[binding].Emplace(m_BindlessThingsIDs[binding].GetSize()));

            const auto descriptorType =
                (binding == Shaders::s_BINDLESS_STORAGE_IMAGE_BINDING)
                    ? vk::DescriptorType::eStorageImage
                    : (binding == Shaders::s_BINDLESS_SAMPLER_BINDING
                           ? vk::DescriptorType::eSampler
                           : (binding == Shaders::s_BINDLESS_SAMPLED_IMAGE_BINDING ? vk::DescriptorType::eSampledImage
                                                                                   : vk::DescriptorType::eCombinedImageSampler));

            std::array<vk::WriteDescriptorSet, s_BufferedFrameCount> writes{};
            for (u8 frame{}; frame < s_BufferedFrameCount; ++frame)
            {
                writes[frame] = vk::WriteDescriptorSet()
                                    .setDescriptorCount(1)
                                    .setDescriptorType(descriptorType)
                                    .setDstArrayElement(*bindlessID)
                                    .setDstBinding(binding)
                                    .setDstSet(m_BindlessResourcesPerFrame[frame].DescriptorSet)
                                    .setImageInfo(imageInfo);
            }

            m_Device->updateDescriptorSets(writes, {});
        }

        void PopBindlessThing(std::optional<u32>& bindlessID, const u32 binding) noexcept
        {
            std::scoped_lock lock(m_BindlessThingsMtx);
            RDNT_ASSERT(binding == Shaders::s_BINDLESS_STORAGE_IMAGE_BINDING || binding == Shaders::s_BINDLESS_SAMPLER_BINDING ||
                            binding == Shaders::s_BINDLESS_COMBINED_IMAGE_SAMPLER_BINDING ||
                            binding == Shaders::s_BINDLESS_SAMPLED_IMAGE_BINDING,
                        "Unknown binding!");
            RDNT_ASSERT(bindlessID.has_value(), "BindlessID is invalid!");
            m_BindlessThingsIDs[binding].Release(static_cast<PoolID>(*bindlessID));
            bindlessID = std::nullopt;
        }

        NODISCARD FORCEINLINE auto& GetCurrentFrameBindlessResources() const noexcept
        {
            return m_BindlessResourcesPerFrame[m_CurrentFrameNumber % s_BufferedFrameCount];
        }

        NODISCARD auto GetBindlessStatistics() const noexcept
        {
            return GfxBindlessStatistics{
                static_cast<u32>(m_BindlessThingsIDs[Shaders::s_BINDLESS_STORAGE_IMAGE_BINDING].GetPresentObjectsSize()),
                static_cast<u32>(m_BindlessThingsIDs[Shaders::s_BINDLESS_COMBINED_IMAGE_SAMPLER_BINDING].GetPresentObjectsSize()),
                static_cast<u32>(m_BindlessThingsIDs[Shaders::s_BINDLESS_SAMPLED_IMAGE_BINDING].GetPresentObjectsSize()),
                static_cast<u32>(m_BindlessThingsIDs[Shaders::s_BINDLESS_SAMPLER_BINDING].GetPresentObjectsSize())};
        }

      private:
        mutable std::mutex m_Mtx{};
        vk::UniqueDevice m_Device{};
        vk::PhysicalDevice m_PhysicalDevice{};
        vk::PhysicalDeviceProperties m_GPUProperties{};
        bool m_bMemoryPrioritySupported{false};
        u64 m_CurrentFrameNumber{0};

        // Bindless resources part1
        mutable std::mutex m_BindlessThingsMtx{};
        struct BindlessResources
        {
            vk::UniqueDescriptorPool DescriptorPool{};
            vk::DescriptorSet DescriptorSet{};
        };
        std::array<BindlessResources, s_BufferedFrameCount> m_BindlessResourcesPerFrame{};

        // Bindless resources part2
        vk::UniqueDescriptorSetLayout m_DescriptorSetLayout{};
        vk::UniquePipelineLayout m_PipelineLayout{};

        // Bindless resources part3
        std::array<Pool<u32>, 4> m_BindlessThingsIDs{};

        vk::UniquePipelineCache m_PipelineCache{};
        mutable UnorderedMap<vk::SamplerCreateInfo, std::pair<vk::UniqueSampler, std::optional<u32>>> m_SamplerMap{};

        struct Queue
        {
            ECommandQueueType Type{};
            u8 QueueIndex{};
            u8 QueueFamilyIndex{std::numeric_limits<u8>::max()};
            std::mutex QueueMutex{};
            vk::Queue Handle{};
            std::array<vk::UniqueSemaphore, s_BufferedFrameCount> TimelineSemaphore;
            std::array<u64, s_BufferedFrameCount> TimelineValue;
        };
        static constexpr u8 s_QueueCount         = 1 + s_MaxComputeQueueCount + s_MaxTransferQueueCount;
        std::array<Queue, s_QueueCount> m_Queues = {
            Queue{ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL, 0},
            Queue{ECommandQueueType::COMMAND_QUEUE_TYPE_ASYNC_COMPUTE, 0},
            Queue{ECommandQueueType::COMMAND_QUEUE_TYPE_ASYNC_COMPUTE, 1},
            Queue{ECommandQueueType::COMMAND_QUEUE_TYPE_ASYNC_COMPUTE, 2},
            Queue{ECommandQueueType::COMMAND_QUEUE_TYPE_ASYNC_COMPUTE, 3},
            Queue{ECommandQueueType::COMMAND_QUEUE_TYPE_ASYNC_COMPUTE, 4},
            Queue{ECommandQueueType::COMMAND_QUEUE_TYPE_ASYNC_COMPUTE, 5},
            Queue{ECommandQueueType::COMMAND_QUEUE_TYPE_ASYNC_COMPUTE, 6},
            Queue{ECommandQueueType::COMMAND_QUEUE_TYPE_ASYNC_COMPUTE, 7},
            Queue{ECommandQueueType::COMMAND_QUEUE_TYPE_DEDICATED_TRANSFER, 0},
            Queue{ECommandQueueType::COMMAND_QUEUE_TYPE_DEDICATED_TRANSFER, 1},
        };
        const u8 c_ComputeQueueOffsetArray  = 1;  // since first queue is general.
        const u8 c_TransferQueueOffsetArray = c_ComputeQueueOffsetArray + s_MaxComputeQueueCount;

        VmaAllocator m_Allocator{VK_NULL_HANDLE};
        vk::SampleCountFlagBits m_MSAASamples{vk::SampleCountFlagBits::e1};

        struct DeferredDeletionQueue
        {
            DeferredDeletionQueue() noexcept  = default;
            ~DeferredDeletionQueue() noexcept = default;

            FORCEINLINE void EmplaceBack(std::move_only_function<void() noexcept>&& func) noexcept
            {
                FunctionsDeque.emplace_back(std::forward<std::move_only_function<void() noexcept>>(func));
            }

            FORCEINLINE void EmplaceBack(vk::UniquePipeline&& pipeline) noexcept
            {
                PipelineHandlesDeque.emplace_back(std::forward<vk::UniquePipeline>(pipeline));
            }

            FORCEINLINE void EmplaceBack(vk::Buffer&& buffer, VmaAllocation&& allocation) noexcept
            {
                BufferHandlesDeque.emplace_back(std::forward<vk::Buffer>(buffer), std::forward<VmaAllocation>(allocation));
            }

            void Flush() noexcept
            {
                // Reverse iterate the deletion queue to execute all the functions.
                for (auto it = FunctionsDeque.rbegin(); it != FunctionsDeque.rend(); ++it)
                    (*it)();

                PipelineHandlesDeque.clear();
                FunctionsDeque.clear();
            }

          private:
            std::deque<std::move_only_function<void() noexcept>> FunctionsDeque;  // In case something special happens.

            std::deque<vk::UniquePipeline> PipelineHandlesDeque;
            std::deque<std::pair<vk::Buffer, VmaAllocation>> BufferHandlesDeque;

            friend class GfxDevice;
        };

        // NOTE: u64 - global frame number
        std::unordered_map<u64, DeferredDeletionQueue> m_DeletionQueuesPerFrame;

        // NOTE: Only GfxContext can call it!
        friend class GfxContext;
        void PollDeletionQueues(const bool bImmediate = false /* means somewhere before waitIdle was called, so GPU is free! */) noexcept;

        constexpr GfxDevice() noexcept = delete;
        void Init(const vk::UniqueInstance& instance, const vk::UniqueSurfaceKHR& surface) noexcept;
        void SelectGPUAndCreateDeviceThings(const vk::UniqueInstance& instance, const vk::UniqueSurfaceKHR& surface,
                                            std::vector<const char*>& requiredDeviceExtensions,
                                            const vk::PhysicalDeviceFeatures& requiredDeviceFeatures, const void* pNext = nullptr) noexcept;
        void InitVMA(const vk::UniqueInstance& instance) noexcept;
        void LoadPipelineCache() noexcept;
        void CreateBindlessSystem() noexcept;

        void Shutdown() noexcept;
    };

}  // namespace Radiant
