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

      private:
        vk::UniqueDevice m_Device{};
        vk::PhysicalDevice m_PhysicalDevice{};

        vk::UniquePipelineCache m_PipelineCache{};

        struct Queue
        {
            vk::Queue Handle{};
            std::optional<std::uint32_t> QueueFamilyIndex{std::nullopt};
        };
        Queue m_GCTQueue{};      // graphics / compute / transfer
        Queue m_PresentQueue{};  // present

        VmaAllocator m_Allocator{};

        // TODO: std::vector<std::pair<std::uint64_t, DeferredDeletionQueue>> and PollDeletionQueues, std::uint64_t - frame number
        struct DeferredDeletionQueue
        {
            using DeletionFunc = std::move_only_function<void()>;

          public:
            DeferredDeletionQueue() noexcept  = default;
            ~DeferredDeletionQueue() noexcept = default;

            void EmplaceBack(DeletionFunc&& func) noexcept { Deque.emplace_back(std::forward<DeletionFunc>(func)); }

            void Flush() noexcept
            {
                // Reverse iterate the deletion queue to execute all the functions.
                for (auto it = Deque.rbegin(); it != Deque.rend(); ++it)
                    (*it)();

                Deque.clear();
            }

          private:
            std::deque<DeletionFunc> Deque;
        };

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
