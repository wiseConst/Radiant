#pragma once

#include <Render/CoreDefines.hpp>

#ifdef RDNT_WINDOWS
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(RDNT_LINUX)
#define VK_USE_PLATFORM_XLIB_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#elif defined(RDNT_MACOS)
#define VK_USE_PLATFORM_MACOS_MVK
#endif

#include <vulkan/vulkan.hpp>

// NOTE: Including device first place ruins surface creation!
#include <Render/GfxDevice.hpp>

namespace Radiant
{
    // NOTE: Small optimization for CPU usage to issue less VK-API calls.
    struct GfxPipelineStateCache
    {
        class GfxPipeline* LastBoundPipeline{nullptr};
        class GfxBuffer* LastBoundIndexBuffer{nullptr};

        // NOTE: Other ops will be defined as the usages of them grows.
        void Bind(const vk::CommandBuffer& cmd, GfxPipeline* pipeline) noexcept;
        void Bind(const vk::CommandBuffer& cmd, GfxBuffer* indexBuffer, const vk::DeviceSize offset = 0,
                  const vk::IndexType indexType = vk::IndexType::eUint32) noexcept;

        std::optional<vk::CullModeFlags> CullMode{std::nullopt};
        std::optional<vk::FrontFace> FrontFace{std::nullopt};
        std::optional<vk::PrimitiveTopology> PrimitiveTopology{std::nullopt};
        std::optional<vk::PolygonMode> PolygonMode{std::nullopt};

        void Set(const vk::CommandBuffer& cmd, const vk::CullModeFlags cullMode) noexcept;
        void Set(const vk::CommandBuffer& cmd, const vk::PrimitiveTopology primitiveTopology) noexcept;
        void Set(const vk::CommandBuffer& cmd, const vk::FrontFace frontFace) noexcept;
        void Set(const vk::CommandBuffer& cmd, const vk::PolygonMode polygonMode) noexcept;

        std::optional<vk::StencilOp> Back{std::nullopt};
        std::optional<vk::StencilOp> Front{std::nullopt};
        bool bStencilTest{false};

        bool bDepthClamp{false};
        bool bDepthTest{false};
        bool bDepthWrite{false};

        std::optional<vk::CompareOp> DepthCompareOp{std::nullopt};
        void Set(const vk::CommandBuffer& cmd, const vk::CompareOp compareOp) noexcept;

        glm::vec2 DepthBounds{0.f};  // Range [0.0f, 1.0f] for example.
    };

    // TODO: Make use of it in multiple queues or subsequent submits.
    class SyncPoint final
    {
      public:
        SyncPoint(const Unique<GfxDevice>& gfxDevice, const vk::Semaphore& timelineSemaphore, const u64& timelineValue,
                  const vk::PipelineStageFlags2& pipelineStages) noexcept
            : m_GfxDevice(gfxDevice), m_TimelimeSemaphore(timelineSemaphore), m_TimelineValue(timelineValue),
              m_PipelineStages(pipelineStages)
        {
        }
        ~SyncPoint() noexcept = default;

        FORCEINLINE void Wait() const noexcept
        {
            RDNT_ASSERT(m_GfxDevice->GetLogicalDevice()->waitSemaphores(vk::SemaphoreWaitInfo()
                                                                            .setSemaphores(m_TimelimeSemaphore)
                                                                            .setValues(m_TimelineValue)
                                                                            .setFlags(vk::SemaphoreWaitFlagBits::eAny),
                                                                        std::numeric_limits<u64>::max()) == vk::Result::eSuccess,
                        "Failed to wait on timeline semaphore!");
        }

        NODISCARD FORCEINLINE const auto& GetValue() const noexcept { return m_TimelineValue; }
        NODISCARD FORCEINLINE const auto& GetSemaphore() const noexcept { return m_TimelimeSemaphore; }
        NODISCARD FORCEINLINE const auto& GetPipelineStages() const noexcept { return m_PipelineStages; }

      private:
        const Unique<GfxDevice>& m_GfxDevice;
        vk::Semaphore m_TimelimeSemaphore{};
        u64 m_TimelineValue{0};
        vk::PipelineStageFlags2 m_PipelineStages{vk::PipelineStageFlagBits2::eNone};

        constexpr SyncPoint() noexcept = delete;
    };

    // NOTE: Currently used only for parallel texture loading.
    struct ImmediateExecuteContext
    {
        vk::UniqueCommandPool CommandPool{};
        vk::CommandBuffer CommandBuffer{};
        vk::Queue Queue{};
    };

    class GfxTexture;
    class GfxContext final : private Uncopyable, private Unmovable
    {
      public:
        GfxContext() noexcept { Init(); }
        ~GfxContext() noexcept { Shutdown(); };

        bool BeginFrame() noexcept;
        void EndFrame() noexcept;

        NODISCARD FORCEINLINE const auto GetGlobalFrameNumber() const noexcept { return m_GlobalFrameNumber; }
        NODISCARD FORCEINLINE auto& GetCurrentFrameData() const noexcept { return m_FrameData[m_CurrentFrameIndex]; }
        NODISCARD FORCEINLINE const auto& GetInstance() const noexcept { return m_Instance; }
        NODISCARD FORCEINLINE auto& GetBindlessPipelineLayout() const noexcept { return m_PipelineLayout; }
        NODISCARD FORCEINLINE auto& GetDevice() const noexcept { return m_Device; }
        NODISCARD FORCEINLINE auto GetDefaultWhiteTexture() const noexcept { return m_DefaultWhiteTexture; }

        NODISCARD FORCEINLINE const auto GetSwapchainImageFormat() const noexcept { return m_SwapchainImageFormat; }
        NODISCARD FORCEINLINE const auto& GetSwapchainExtent() const noexcept { return m_SwapchainExtent; }
        NODISCARD FORCEINLINE const auto& GetCurrentSwapchainImage() const noexcept { return m_SwapchainImages[m_CurrentImageIndex]; }
        NODISCARD FORCEINLINE const auto& GetCurrentSwapchainImageView() const noexcept
        {
            return m_SwapchainImageViews[m_CurrentImageIndex];
        }
        NODISCARD FORCEINLINE const auto GetSwapchainImageCount() const noexcept { return m_SwapchainImages.size(); }

        NODISCARD ImmediateExecuteContext
        CreateImmediateExecuteContext(const ECommandBufferType commandBufferType,
                                      const vk::CommandBufferLevel commandBufferLevel = vk::CommandBufferLevel::ePrimary) const noexcept
        {
            const auto& logicalDevice = m_Device->GetLogicalDevice();
            std::scoped_lock lock(m_Mtx);

            ImmediateExecuteContext context = {};
            switch (commandBufferType)
            {
                case ECommandBufferType::COMMAND_BUFFER_TYPE_GENERAL:
                {
                    context.Queue = m_Device->GetGeneralQueue().Handle;
                    context.CommandPool =
                        logicalDevice->createCommandPoolUnique(vk::CommandPoolCreateInfo()
                                                                   .setQueueFamilyIndex(*m_Device->GetGeneralQueue().QueueFamilyIndex)
                                                                   .setFlags(vk::CommandPoolCreateFlagBits::eTransient));
                    break;
                }
                case ECommandBufferType::COMMAND_BUFFER_TYPE_ASYNC_COMPUTE:
                {
                    context.Queue = m_Device->GetComputeQueue().Handle;
                    context.CommandPool =
                        logicalDevice->createCommandPoolUnique(vk::CommandPoolCreateInfo()
                                                                   .setQueueFamilyIndex(*m_Device->GetComputeQueue().QueueFamilyIndex)
                                                                   .setFlags(vk::CommandPoolCreateFlagBits::eTransient));

                    break;
                }
                case ECommandBufferType::COMMAND_BUFFER_TYPE_DEDICATED_TRANSFER:
                {
                    context.Queue = m_Device->GetTransferQueue().Handle;
                    context.CommandPool =
                        logicalDevice->createCommandPoolUnique(vk::CommandPoolCreateInfo()
                                                                   .setQueueFamilyIndex(*m_Device->GetTransferQueue().QueueFamilyIndex)
                                                                   .setFlags(vk::CommandPoolCreateFlagBits::eTransient));

                    break;
                }
                default: RDNT_ASSERT(false, "Unknown command buffer type!");
            }

            context.CommandBuffer = logicalDevice
                                        ->allocateCommandBuffers(vk::CommandBufferAllocateInfo()
                                                                     .setCommandPool(*context.CommandPool)
                                                                     .setLevel(commandBufferLevel)
                                                                     .setCommandBufferCount(1))
                                        .back();
            return context;
        }

        NODISCARD std::tuple<vk::UniqueCommandBuffer, vk::Queue> AllocateSingleUseCommandBufferWithQueue(
            const ECommandBufferType commandBufferType,
            const vk::CommandBufferLevel commandBufferLevel = vk::CommandBufferLevel::ePrimary) const noexcept
        {
            std::scoped_lock lock(m_Mtx);
            switch (commandBufferType)
            {
                case ECommandBufferType::COMMAND_BUFFER_TYPE_GENERAL:
                {
                    return {std::move(m_Device->GetLogicalDevice()
                                          ->allocateCommandBuffersUnique(
                                              vk::CommandBufferAllocateInfo()
                                                  .setCommandBufferCount(1)
                                                  .setLevel(commandBufferLevel)
                                                  .setCommandPool(*m_FrameData[m_CurrentFrameIndex].GeneralCommandPool))
                                          .back()),
                            m_Device->GetGeneralQueue().Handle};
                }
                case ECommandBufferType::COMMAND_BUFFER_TYPE_ASYNC_COMPUTE:
                {
                    return {std::move(m_Device->GetLogicalDevice()
                                          ->allocateCommandBuffersUnique(
                                              vk::CommandBufferAllocateInfo()
                                                  .setCommandBufferCount(1)
                                                  .setLevel(commandBufferLevel)
                                                  .setCommandPool(*m_FrameData[m_CurrentFrameIndex].AsyncComputeCommandPool))
                                          .back()),
                            m_Device->GetComputeQueue().Handle};
                }
                case ECommandBufferType::COMMAND_BUFFER_TYPE_DEDICATED_TRANSFER:
                {
                    return {std::move(m_Device->GetLogicalDevice()
                                          ->allocateCommandBuffersUnique(
                                              vk::CommandBufferAllocateInfo()
                                                  .setCommandBufferCount(1)
                                                  .setLevel(commandBufferLevel)
                                                  .setCommandPool(*m_FrameData[m_CurrentFrameIndex].DedicatedTransferCommandPool))
                                          .back()),
                            m_Device->GetTransferQueue().Handle};
                }
                default: RDNT_ASSERT(false, "Unknown command buffer type!");
            }
        }

        NODISCARD FORCEINLINE auto& GetPipelineStateCache() noexcept { return m_PipelineStateCache; }

        NODISCARD FORCEINLINE static auto& Get() noexcept
        {
            RDNT_ASSERT(s_Instance, "GfxContext instance is invalid!");
            return *s_Instance;
        }

        void PushBindlessThing(const vk::DescriptorImageInfo& imageInfo, std::optional<u32>& bindlessID,
                               const u32 binding) noexcept
        {
            std::scoped_lock lock(m_Mtx);
            RDNT_ASSERT(binding == Shaders::s_BINDLESS_IMAGE_BINDING || binding == Shaders::s_BINDLESS_SAMPLER_BINDING ||
                            binding == Shaders::s_BINDLESS_TEXTURE_BINDING,
                        "Unknown binding!");
            RDNT_ASSERT(!bindlessID.has_value(), "BindlessID is already populated!");

            if (binding != Shaders::s_BINDLESS_SAMPLER_BINDING) RDNT_ASSERT(imageInfo.imageView, "ImageView is invalid!");
            if (binding != Shaders::s_BINDLESS_IMAGE_BINDING) RDNT_ASSERT(imageInfo.sampler, "Sampler is invalid!");

            bindlessID = static_cast<u32>(m_BindlessThingsIDs[binding].Emplace(m_BindlessThingsIDs[binding].GetSize()));

            const auto descriptorType = (binding == Shaders::s_BINDLESS_IMAGE_BINDING) ? vk::DescriptorType::eStorageImage
                                                                                       : ((binding == Shaders::s_BINDLESS_SAMPLER_BINDING)
                                                                                              ? vk::DescriptorType::eSampler
                                                                                              : vk::DescriptorType::eCombinedImageSampler);

            std::array<vk::WriteDescriptorSet, s_BufferedFrameCount> writes{};
            for (u8 frame{}; frame < s_BufferedFrameCount; ++frame)
            {
                writes[frame] = vk::WriteDescriptorSet()
                                    .setDescriptorCount(1)
                                    .setDescriptorType(descriptorType)
                                    .setDstArrayElement(*bindlessID)
                                    .setDstBinding(binding)
                                    .setDstSet(m_FrameData[frame].DescriptorSet)
                                    .setImageInfo(imageInfo);
            }

            m_Device->GetLogicalDevice()->updateDescriptorSets(writes, {});
        }

        void PopBindlessThing(std::optional<u32>& bindlessID, const u32 binding) noexcept
        {
            std::scoped_lock lock(m_Mtx);
            RDNT_ASSERT(binding == Shaders::s_BINDLESS_IMAGE_BINDING || binding == Shaders::s_BINDLESS_SAMPLER_BINDING ||
                            binding == Shaders::s_BINDLESS_TEXTURE_BINDING,
                        "Unknown binding!");
            RDNT_ASSERT(bindlessID.has_value(), "BindlessID is invalid!");
            m_BindlessThingsIDs[binding].Release(static_cast<PoolID>(*bindlessID));
            bindlessID = std::nullopt;
        }

        NODISCARD FORCEINLINE auto& GetMutex() noexcept { return m_Mtx; }

      private:
        mutable std::mutex m_Mtx{};
        static inline GfxContext* s_Instance{nullptr};  // NOTE: Used only for safely pushing objects through device into deletion queue.
        vk::UniqueInstance m_Instance{};
        vk::UniqueDebugUtilsMessengerEXT m_DebugUtilsMessenger{};

        // NOTE: Destroy pools only after deferred deletion queues are finished!
        // Bindless resources pt. 3
        std::array<Pool<u32>, 3> m_BindlessThingsIDs{};

        Unique<GfxDevice> m_Device{};
        Shared<GfxTexture> m_DefaultWhiteTexture{nullptr};

        struct FrameData
        {
            vk::UniqueCommandPool GeneralCommandPool{};
            vk::CommandBuffer GeneralCommandBuffer{};

            vk::UniqueCommandPool AsyncComputeCommandPool{};
            vk::UniqueCommandPool DedicatedTransferCommandPool{};

            vk::UniqueFence RenderFinishedFence{};
            vk::UniqueSemaphore ImageAvailableSemaphore{};
            vk::UniqueSemaphore RenderFinishedSemaphore{};

            // Bindless resources pt. 1
            vk::UniqueDescriptorPool DescriptorPool{};
            vk::DescriptorSet DescriptorSet{};
        };
        std::array<FrameData, s_BufferedFrameCount> m_FrameData{};

        // Bindless resources pt. 2
        vk::UniqueDescriptorSetLayout m_DescriptorSetLayout{};
        vk::UniquePipelineLayout m_PipelineLayout{};

        // Swapchain things
        u64 m_GlobalFrameNumber{0};  // Used to help determine device's DeferredDeletionQueue flush.
        vk::Extent2D m_SwapchainExtent{};
        vk::Format m_SwapchainImageFormat{};
        vk::UniqueSurfaceKHR m_Surface{};
        vk::UniqueSwapchainKHR m_Swapchain{};
        u32 m_CurrentFrameIndex{0};
        u32 m_CurrentImageIndex{0};
        std::vector<vk::UniqueImageView> m_SwapchainImageViews;
        std::vector<vk::Image> m_SwapchainImages;
        bool m_bSwapchainNeedsResize{false};

        GfxPipelineStateCache m_PipelineStateCache = {};

        void Init() noexcept;
        void CreateInstanceAndDebugUtilsMessenger() noexcept;
        void CreateSurface() noexcept;
        void InvalidateSwapchain() noexcept;
        void CreateFrameResources() noexcept;
        void Shutdown() noexcept;
    };

}  // namespace Radiant
