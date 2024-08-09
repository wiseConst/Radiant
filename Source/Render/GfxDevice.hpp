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
        return detail::wyhash::hash(static_cast<std::uint64_t>(x.magFilter) + static_cast<std::uint64_t>(x.minFilter) +
                                    static_cast<std::uint64_t>(x.mipmapMode) + static_cast<std::uint64_t>(x.addressModeU) +
                                    static_cast<std::uint64_t>(x.addressModeV) + static_cast<std::uint64_t>(x.addressModeW) +
                                    static_cast<std::uint64_t>(x.mipLodBias) + static_cast<std::uint64_t>(x.anisotropyEnable) +
                                    static_cast<std::uint64_t>(x.maxAnisotropy) + static_cast<std::uint64_t>(x.compareEnable) +
                                    static_cast<std::uint64_t>(x.compareOp) + static_cast<std::uint64_t>(x.minLod) +
                                    static_cast<std::uint64_t>(x.maxLod) + static_cast<std::uint64_t>(x.borderColor));
    }
};

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

        void AllocateTexture(const vk::ImageCreateInfo& imageCI, VkImage& image, VmaAllocation& allocation) const noexcept;
        void DeallocateTexture(VkImage& image, VmaAllocation& allocation) const noexcept;

        void AllocateBuffer(const ExtraBufferFlags extraBufferFlags, const vk::BufferCreateInfo& bufferCI, VkBuffer& buffer,
                            VmaAllocation& allocation) const noexcept;
        void DeallocateBuffer(VkBuffer& buffer, VmaAllocation& allocation) const noexcept;

        void* Map(VmaAllocation& allocation) const noexcept;
        void Unmap(VmaAllocation& allocation) const noexcept;

        NODISCARD const vk::Sampler& GetSampler(const vk::SamplerCreateInfo& samplerCI) noexcept
        {
            std::scoped_lock lock(m_Mtx);
            if (!m_SamplerMap.contains(samplerCI)) m_SamplerMap[samplerCI] = m_Device->createSamplerUnique(samplerCI);
            return *m_SamplerMap[samplerCI];
        }

        NODISCARD const vk::Sampler& GetDefaultSampler() noexcept
        {
            const auto defaultSamplerCI = vk::SamplerCreateInfo()
                                              .setUnnormalizedCoordinates(vk::False)
                                              .setAddressModeU(vk::SamplerAddressMode::eRepeat)
                                              .setAddressModeV(vk::SamplerAddressMode::eRepeat)
                                              .setAddressModeW(vk::SamplerAddressMode::eRepeat)
                                              .setMagFilter(vk::Filter::eLinear)
                                              .setMinFilter(vk::Filter::eLinear)
                                              .setMipmapMode(vk::SamplerMipmapMode::eLinear)
                                              .setMinLod(0.0f)
                                              .setMaxLod(1.0f)
                                              .setMaxLod(vk::LodClampNone)
                                              .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
                                              .setAnisotropyEnable(vk::True)
                                              .setMaxAnisotropy(m_GPUProperties.limits.maxSamplerAnisotropy);
            return GetSampler(defaultSamplerCI);
        }

        FORCEINLINE const auto GetMSAASamples() const noexcept { return m_MSAASamples; }

      private:
        mutable std::mutex m_Mtx{};
        vk::UniqueDevice m_Device{};
        vk::PhysicalDevice m_PhysicalDevice{};
        vk::PhysicalDeviceProperties m_GPUProperties{};

        vk::UniquePipelineCache m_PipelineCache{};

        UnorderedMap<vk::SamplerCreateInfo, vk::UniqueSampler> m_SamplerMap{};

        struct Queue
        {
            vk::Queue Handle{};
            std::optional<u32> QueueFamilyIndex{std::nullopt};
        } m_GeneralQueue{}, m_PresentQueue{}, m_TransferQueue{}, m_ComputeQueue{};

        VmaAllocator m_Allocator{};
        vk::SampleCountFlagBits m_MSAASamples{vk::SampleCountFlagBits::e1};

        struct DeferredDeletionQueue
        {
          public:
            DeferredDeletionQueue() noexcept  = default;
            ~DeferredDeletionQueue() noexcept = default;

            FORCEINLINE void EmplaceBack(std::move_only_function<void() noexcept>&& func) noexcept
            {
                Deque.emplace_back(std::forward<std::move_only_function<void() noexcept>>(func));
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
                for (auto it = Deque.rbegin(); it != Deque.rend(); ++it)
                    (*it)();

                while (!PipelineHandlesDeque.empty())
                    PipelineHandlesDeque.pop_front();

                Deque.clear();
                PipelineHandlesDeque.clear();
            }

          private:
            std::deque<std::move_only_function<void() noexcept>> Deque;  // In case something special happens.

            std::deque<vk::UniquePipeline> PipelineHandlesDeque;
            std::deque<std::pair<vk::Buffer, VmaAllocation>> BufferHandlesDeque;

            friend class GfxDevice;
        };

        // NOTE: u64 - global frame number
        // TODO: Fix compilation issues using UnorderedMap!
        std::unordered_map<u64, DeferredDeletionQueue> m_DeletionQueuesPerFrame;
        u64 m_CurrentFrameNumber{0};  // Exclusively occupied by DeferredDeletionQueue needs.

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

        void Shutdown() noexcept;
    };

}  // namespace Radiant
