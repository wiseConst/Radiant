#pragma once

#include <Render/CoreDefines.hpp>
#include <vulkan/vulkan.hpp>

#define VK_NO_PROTOTYPES
#include "vma/vk_mem_alloc.h"

#include <deque>

namespace Radiant
{

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
        NODISCARD FORCEINLINE const auto& GetGeneralQueue() const noexcept { return m_GeneralQueue; }
        NODISCARD FORCEINLINE const auto& GetTransferQueue() const noexcept { return m_TransferQueue; }
        NODISCARD FORCEINLINE const auto& GetComputeQueue() const noexcept { return m_ComputeQueue; }
        NODISCARD FORCEINLINE const auto& GetPresentQueue() const noexcept { return m_PresentQueue; }

        template <typename TObject> void SetDebugName(const std::string& name, const TObject& object) const noexcept
        {
            m_Device->setDebugUtilsObjectNameEXT(vk::DebugUtilsObjectNameInfoEXT()
                                                     .setPObjectName(name.data())
                                                     .setObjectType(object.objectType)
                                                     .setObjectHandle(std::uint64_t(TObject::NativeType(object))));
        }

        operator const vk::Device&() const noexcept
        {
            RDNT_ASSERT(m_Device, "LogicalDevice not valid!");
            return *m_Device;
        }

        FORCEINLINE void PushObjectToDelete(std::move_only_function<void()>&& func) noexcept
        {
            m_DeletionQueuesPerFrame[m_CurrentFrameNumber].EmplaceBack(std::forward<std::move_only_function<void()>>(func));
        }

        FORCEINLINE void PushObjectToDelete(vk::UniquePipeline&& pipeline) noexcept
        {
            m_DeletionQueuesPerFrame[m_CurrentFrameNumber].EmplaceBack(std::forward<vk::UniquePipeline>(pipeline));
        }

        FORCEINLINE void PushObjectToDelete(vk::Buffer&& buffer, VmaAllocation&& allocation) noexcept
        {
            m_DeletionQueuesPerFrame[m_CurrentFrameNumber].EmplaceBack(std::forward<vk::Buffer>(buffer),
                                                                       std::forward<VmaAllocation>(allocation));
        }

        void AllocateTexture(const vk::ImageCreateInfo& imageCI, VkImage& image, VmaAllocation& allocation) const noexcept;
        void DeallocateTexture(VkImage& image, VmaAllocation& allocation) const noexcept;

        void AllocateBuffer(const ExtraBufferFlags extraBufferFlags, const vk::BufferCreateInfo& bufferCI, VkBuffer& buffer,
                            VmaAllocation& allocation) const noexcept;
        void DeallocateBuffer(VkBuffer& buffer, VmaAllocation& allocation) const noexcept;

        void* Map(VmaAllocation& allocation) const noexcept;
        void Unmap(VmaAllocation& allocation) const noexcept;

      private:
        vk::UniqueDevice m_Device{};
        vk::PhysicalDevice m_PhysicalDevice{};
        vk::PhysicalDeviceProperties m_GPUProperties{};

        vk::UniquePipelineCache m_PipelineCache{};

        struct Queue
        {
            vk::Queue Handle{};
            std::optional<std::uint32_t> QueueFamilyIndex{std::nullopt};
        };
        Queue m_GeneralQueue{};   // graphics / compute / transfer
        Queue m_PresentQueue{};   // present
        Queue m_TransferQueue{};  // dedicated transfer
        Queue m_ComputeQueue{};   // async compute

        VmaAllocator m_Allocator{};
        std::uint64_t m_CurrentFrameNumber{0};  // Exclusively occupied by DeferredDeletionQueue needs.

        struct DeferredDeletionQueue
        {
          public:
            DeferredDeletionQueue() noexcept  = default;
            ~DeferredDeletionQueue() noexcept = default;

            void EmplaceBack(std::move_only_function<void()>&& func) noexcept
            {
                Deque.emplace_back(std::forward<std::move_only_function<void()>>(func));
            }

            void EmplaceBack(vk::UniquePipeline&& pipeline) noexcept
            {
                PipelineHandlesDeque.emplace_back(std::forward<vk::UniquePipeline>(pipeline));
            }

            void EmplaceBack(vk::Buffer&& buffer, VmaAllocation&& allocation) noexcept
            {
                BufferHandlesDeque.emplace_back(std::forward<vk::Buffer>(buffer), std::forward<VmaAllocation>(allocation));
            }

            void Flush() noexcept
            {
                // Reverse iterate the deletion queue to execute all the functions.
                for (auto it = Deque.rbegin(); it != Deque.rend(); ++it)
                    (*it)();

                while (!PipelineHandlesDeque.empty())
                    PipelineHandlesDeque.pop_front();

                Deque.clear();
                PipelineHandlesDeque.clear();
            }

          private:
            std::deque<std::move_only_function<void()>> Deque;  // In case something special happens.

            std::deque<vk::UniquePipeline> PipelineHandlesDeque;

            std::move_only_function<void(VkBuffer&, VmaAllocation&)> BufferRemoveFunc;
            std::deque<std::pair<vk::Buffer, VmaAllocation>> BufferHandlesDeque;

            friend class GfxDevice;
        };

        // NOTE: std::uint64_t - global frame number
        // TODO: Fix compilation issues using UnorderedMap!
        std::unordered_map<std::uint64_t, DeferredDeletionQueue> m_DeletionQueuesPerFrame;

        // NOTE: Only GfxContext can call it!
        friend class GfxContext;
        void PollDeletionQueues(const bool bImmediate = false /* means somewhere before waitIdle was called, so GPU is free! */) noexcept
        {
            UnorderedSet<std::uint64_t> queuesToRemove;

            for (auto& [frameNumber, deletionQueue] : m_DeletionQueuesPerFrame)
            {
                // We have to make sure that all buffered frames stopped using our resource!
                const std::uint64_t framesPast = frameNumber + static_cast<std::uint64_t>(s_BufferedFrameCount);
                if (!bImmediate && framesPast >= m_CurrentFrameNumber) continue;

                deletionQueue.Flush();

                for (auto it = deletionQueue.BufferHandlesDeque.rbegin(); it != deletionQueue.BufferHandlesDeque.rend(); ++it)
                {
                    DeallocateBuffer(*(VkBuffer*)&it->first, *(VmaAllocation*)&it->second);
                }
                deletionQueue.BufferHandlesDeque.clear();

                queuesToRemove.emplace(frameNumber);
            }

            for (const auto queueFrameNumber : queuesToRemove)
                m_DeletionQueuesPerFrame.erase(queueFrameNumber);

            //       if (!queuesToRemove.empty()) LOG_TRACE("{}: freed {} deletion queues.", __FUNCTION__, queuesToRemove.size());
        }

        constexpr GfxDevice() noexcept = delete;
        void Init(const vk::UniqueInstance& instance, const vk::UniqueSurfaceKHR& surface) noexcept;
        void SelectGPUAndCreateDeviceThings(const vk::UniqueInstance& instance, const vk::UniqueSurfaceKHR& surface,
                                            std::vector<const char*>& requiredDeviceExtensions,
                                            const vk::PhysicalDeviceFeatures& requiredDeviceFeatures, const void* pNext = nullptr) noexcept;
        void InitVMA(const vk::UniqueInstance& instance) noexcept;
        void LoadPipelineCache() noexcept;
        void Shutdown() noexcept;
    };

}  // namespace Radiant
