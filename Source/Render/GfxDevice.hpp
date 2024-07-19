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
        NODISCARD FORCEINLINE const auto& GetGCTQueue() const noexcept { return m_GCTQueue; }
        NODISCARD FORCEINLINE const auto& GetPresentQueue() const noexcept { return m_PresentQueue; }

        operator const vk::Device&() const noexcept
        {
            RDNT_ASSERT(m_Device, "LogicalDevice not valid!");
            return *m_Device;
        }

        FORCEINLINE void PushObjectToDelete(std::move_only_function<void()>&& func) noexcept
        {
            m_DeletionQueuesPerFrame[m_CurrentFrameNumber].EmplaceBack(std::forward<std::move_only_function<void()>>(func));
        }

        void AllocateTexture(const vk::ImageCreateInfo& imageCI, VkImage& image, VmaAllocation& allocation) const noexcept;
        void DeallocateTexture(VkImage& image, VmaAllocation& allocation) const noexcept;

        void AllocateBuffer(const EExtraBufferFlag extraBufferFlag, const vk::BufferCreateInfo& bufferCI, VkBuffer& buffer,
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
        Queue m_GCTQueue{};      // graphics / compute / transfer
        Queue m_PresentQueue{};  // present

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

            void Flush() noexcept
            {
                // Reverse iterate the deletion queue to execute all the functions.
                for (auto it = Deque.rbegin(); it != Deque.rend(); ++it)
                    (*it)();

                Deque.clear();
            }

          private:
            std::deque<std::move_only_function<void()>> Deque;
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
                queuesToRemove.emplace(frameNumber);
            }

            for (const auto queueFrameNumber : queuesToRemove)
                m_DeletionQueuesPerFrame.erase(queueFrameNumber);
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
