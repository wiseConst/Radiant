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

    class GfxTexture;
    class GfxPipeline;
    class GfxBuffer;

    // NOTE: Small optimization for CPU usage to issue less VK-API calls.
    struct GfxPipelineStateCache final
    {
      public:
        GfxPipelineStateCache() noexcept  = default;
        ~GfxPipelineStateCache() noexcept = default;

        void Bind(const vk::CommandBuffer& cmd, GfxPipeline* pipeline) noexcept;
        void Bind(const vk::CommandBuffer& cmd, GfxBuffer* indexBuffer, const vk::DeviceSize offset = 0,
                  const vk::IndexType indexType = vk::IndexType::eUint32) noexcept;

        void Set(const vk::CommandBuffer& cmd, const vk::CullModeFlags cullMode) noexcept;
        void Set(const vk::CommandBuffer& cmd, const vk::PrimitiveTopology primitiveTopology) noexcept;
        void Set(const vk::CommandBuffer& cmd, const vk::FrontFace frontFace) noexcept;
        void Set(const vk::CommandBuffer& cmd, const vk::PolygonMode polygonMode) noexcept;
        void Set(const vk::CommandBuffer& cmd, const vk::CompareOp compareOp) noexcept;
        void SetDepthClamp(const vk::CommandBuffer& cmd, const bool bDepthClampEnable) noexcept;
        void SetStencilTest(const vk::CommandBuffer& cmd, const bool bStencilTestEnable) noexcept;
        void SetDepthTest(const vk::CommandBuffer& cmd, const bool bDepthTestEnable) noexcept;
        void SetDepthWrite(const vk::CommandBuffer& cmd, const bool bDepthWriteEnable) noexcept;
        void SetDepthBounds(const vk::CommandBuffer& cmd, const glm::vec2& depthBounds) noexcept;

        void Invalidate() noexcept
        {
            LastBoundPipeline = nullptr;

            LastBoundIndexBuffer       = nullptr;
            LastBoundIndexBufferOffset = std::nullopt;
            LastBoundIndexType         = std::nullopt;

            CullMode          = std::nullopt;
            FrontFace         = std::nullopt;
            PrimitiveTopology = std::nullopt;
            PolygonMode       = std::nullopt;

            Back         = std::nullopt;
            Front        = std::nullopt;
            bStencilTest = std::nullopt;

            bDepthClamp    = std::nullopt;
            bDepthTest     = std::nullopt;
            bDepthWrite    = std::nullopt;
            DepthCompareOp = std::nullopt;

            DepthBounds = std::nullopt;
        }

      private:
        GfxPipeline* LastBoundPipeline{nullptr};  // Main object, if it changes, whole state is invalidated.
        GfxBuffer* LastBoundIndexBuffer{nullptr};
        std::optional<vk::DeviceSize> LastBoundIndexBufferOffset{std::nullopt};
        std::optional<vk::IndexType> LastBoundIndexType{std::nullopt};

        std::optional<vk::CullModeFlags> CullMode{std::nullopt};
        std::optional<vk::FrontFace> FrontFace{std::nullopt};
        std::optional<vk::PrimitiveTopology> PrimitiveTopology{std::nullopt};
        std::optional<vk::PolygonMode> PolygonMode{std::nullopt};

        std::optional<vk::StencilOp> Back{std::nullopt};
        std::optional<vk::StencilOp> Front{std::nullopt};
        std::optional<bool> bStencilTest{std::nullopt};

        std::optional<bool> bDepthClamp{std::nullopt};
        std::optional<bool> bDepthTest{std::nullopt};
        std::optional<bool> bDepthWrite{std::nullopt};
        std::optional<vk::CompareOp> DepthCompareOp{std::nullopt};

        std::optional<glm::vec2> DepthBounds{std::nullopt};  // Range [0.0f, 1.0f] for example.
    };

    // TODO: Make use of it in multiple queues or subsequent submits.
    struct GfxSyncPoint final
    {
      public:
        GfxSyncPoint(const Unique<GfxDevice>& gfxDevice, const vk::Semaphore& timelineSemaphore, const u64& timelineValue,
                     const vk::PipelineStageFlags2& pipelineStages) noexcept
            : m_Device(gfxDevice), m_TimelimeSemaphore(timelineSemaphore), m_TimelineValue(timelineValue), m_PipelineStages(pipelineStages)
        {
        }
        ~GfxSyncPoint() noexcept = default;

        FORCEINLINE void Wait() const noexcept
        {
            RDNT_ASSERT(m_Device->GetLogicalDevice()->waitSemaphores(vk::SemaphoreWaitInfo()
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
        const Unique<GfxDevice>& m_Device;
        vk::Semaphore m_TimelimeSemaphore{};
        u64 m_TimelineValue{0};
        vk::PipelineStageFlags2 m_PipelineStages{vk::PipelineStageFlagBits2::eNone};

        constexpr GfxSyncPoint() noexcept = delete;
    };

    // NOTE: Currently used only for parallel texture loading.
    struct GfxImmediateExecuteContext
    {
        vk::UniqueCommandPool CommandPool{};
        vk::CommandBuffer CommandBuffer{};
        ECommandQueueType CommandQueueType{};
        u8 QueueIndex{};
    };

    class GfxContext final : private Uncopyable, private Unmovable
    {
      public:
        GfxContext() noexcept { Init(); }
        ~GfxContext() noexcept { Shutdown(); };

        bool BeginFrame() noexcept;
        void EndFrame() noexcept;

        NODISCARD FORCEINLINE const auto& GetSupportedPresentModesList() const noexcept { return m_SupportedPresentModes; }
        FORCEINLINE void SetPresentMode(const vk::PresentModeKHR newPresentMode) noexcept
        {
            if (newPresentMode == m_PresentMode) return;

            m_PresentMode           = newPresentMode;
            m_bSwapchainNeedsResize = true;
        }
        NODISCARD FORCEINLINE const auto GetPresentMode() const noexcept { return m_PresentMode; }

        NODISCARD FORCEINLINE auto& GetCurrentFrameData() const noexcept { return m_FrameData[m_CurrentFrameIndex]; }
        NODISCARD FORCEINLINE const auto& GetInstance() const noexcept { return m_Instance; }
        NODISCARD FORCEINLINE auto& GetDevice() const noexcept { return m_Device; }
        NODISCARD FORCEINLINE auto& GetDefaultWhiteTexture() const noexcept { return m_DefaultWhiteTexture; }

        NODISCARD FORCEINLINE const auto GetSwapchainImageFormat() const noexcept { return m_SwapchainImageFormat; }
        NODISCARD FORCEINLINE const auto& GetSwapchainExtent() const noexcept { return m_SwapchainExtent; }
        NODISCARD FORCEINLINE const auto& GetCurrentSwapchainImage() const noexcept { return m_SwapchainImages[m_CurrentImageIndex]; }
        NODISCARD FORCEINLINE const auto& GetCurrentSwapchainImageView() const noexcept
        {
            return m_SwapchainImageViews[m_CurrentImageIndex];
        }
        NODISCARD FORCEINLINE const auto GetSwapchainImageCount() const noexcept { return m_SwapchainImages.size(); }

        NODISCARD GfxImmediateExecuteContext
        CreateImmediateExecuteContext(const ECommandQueueType commandQueueType, const u8 queueIndex = 0,
                                      const vk::CommandBufferLevel commandBufferLevel = vk::CommandBufferLevel::ePrimary) const noexcept
        {
            const auto& logicalDevice = m_Device->GetLogicalDevice();
            GfxDevice::Queue* queue{nullptr};
            GfxImmediateExecuteContext context = {};
            switch (commandQueueType)
            {
                case ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL:
                {
                    queue                    = (GfxDevice::Queue*)&m_Device->GetGeneralQueue();
                    context.CommandQueueType = ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL;
                    context.QueueIndex       = 0;
                    break;
                }
                case ECommandQueueType::COMMAND_QUEUE_TYPE_ASYNC_COMPUTE:
                {
                    queue                    = (GfxDevice::Queue*)&m_Device->GetComputeQueue(queueIndex);
                    context.CommandQueueType = ECommandQueueType::COMMAND_QUEUE_TYPE_ASYNC_COMPUTE;
                    context.QueueIndex       = queueIndex;
                    break;
                }
                case ECommandQueueType::COMMAND_QUEUE_TYPE_DEDICATED_TRANSFER:
                {
                    queue                    = (GfxDevice::Queue*)&m_Device->GetTransferQueue(queueIndex);
                    context.CommandQueueType = ECommandQueueType::COMMAND_QUEUE_TYPE_DEDICATED_TRANSFER;
                    context.QueueIndex       = queueIndex;
                    break;
                }
            }
            RDNT_ASSERT(queue, "Failed to retreive queue!");
            std::scoped_lock lock(queue->QueueMutex);  // Synchronizing access to single queue

            context.CommandPool = logicalDevice->createCommandPoolUnique(vk::CommandPoolCreateInfo()
                                                                             .setQueueFamilyIndex(queue->QueueFamilyIndex)
                                                                             .setFlags(vk::CommandPoolCreateFlagBits::eTransient));

            context.CommandBuffer = logicalDevice
                                        ->allocateCommandBuffers(vk::CommandBufferAllocateInfo()
                                                                     .setCommandPool(*context.CommandPool)
                                                                     .setLevel(commandBufferLevel)
                                                                     .setCommandBufferCount(1))
                                        .back();

            return context;
        }

        void SubmitImmediateExecuteContext(const GfxImmediateExecuteContext& ieContext) const noexcept
        {
            GfxDevice::Queue* queue{nullptr};
            switch (ieContext.CommandQueueType)
            {
                case ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL:
                {
                    queue = (GfxDevice::Queue*)&m_Device->GetGeneralQueue();
                    break;
                }
                case ECommandQueueType::COMMAND_QUEUE_TYPE_ASYNC_COMPUTE:
                {
                    queue = (GfxDevice::Queue*)&m_Device->GetComputeQueue(ieContext.QueueIndex);
                    break;
                }
                case ECommandQueueType::COMMAND_QUEUE_TYPE_DEDICATED_TRANSFER:
                {
                    queue = (GfxDevice::Queue*)&m_Device->GetTransferQueue(ieContext.QueueIndex);
                    break;
                }
            }
            RDNT_ASSERT(queue, "Failed to retreive queue!");
            std::scoped_lock lock(queue->QueueMutex);  // Synchronizing access to single queue

            // Creating temporary fence to avoid stalling the whole command queue.
            auto waitFence = m_Device->GetLogicalDevice()->createFenceUnique(vk::FenceCreateInfo());
            queue->Handle.submit(vk::SubmitInfo().setCommandBuffers(ieContext.CommandBuffer), *waitFence);

            RDNT_ASSERT(m_Device->GetLogicalDevice()->waitForFences(*waitFence, vk::True, UINT64_MAX) == vk::Result::eSuccess, "{}",
                        __FUNCTION__);
        }

        NODISCARD FORCEINLINE auto& GetPipelineStateCache() noexcept { return m_PipelineStateCache; }

        NODISCARD FORCEINLINE static auto& Get() noexcept
        {
            RDNT_ASSERT(s_Instance, "GfxContext instance is invalid!");
            return *s_Instance;
        }

        NODISCARD FORCEINLINE const auto GetLastFrameCPUProfilerData() const noexcept
        {
            return m_FrameData[(m_CurrentFrameIndex - 1) % s_BufferedFrameCount].CPUProfilerData;
        }

        NODISCARD FORCEINLINE const auto GetLastFrameGPUProfilerData() const noexcept
        {
            return m_FrameData[(m_CurrentFrameIndex - 1) % s_BufferedFrameCount].GPUProfilerData;
        }

      private:
        static inline GfxContext* s_Instance{nullptr};  // NOTE: Used only for safely pushing objects through device into deletion queue.
        vk::UniqueInstance m_Instance{};
        vk::UniqueDebugUtilsMessengerEXT m_DebugUtilsMessenger{};

        Unique<GfxDevice> m_Device{nullptr};
        Shared<GfxTexture> m_DefaultWhiteTexture{nullptr};

        struct FrameData
        {
            // Profiling...
            std::chrono::time_point<std::chrono::high_resolution_clock> FrameStartTime{Timer::Now()};
            mutable vk::UniqueQueryPool TimestampsQueryPool{};
            mutable u32 TimestampsCapacity{};
            mutable u32 CurrentTimestampIndex{};
            mutable std::vector<u64> TimestampResults;
            mutable std::vector<ProfilerTask> GPUProfilerData;
            mutable std::vector<ProfilerTask> CPUProfilerData;

            vk::UniqueCommandPool GeneralCommandPoolVK{};
            //       Pool<vk::CommandBuffer> GeneralCommandPool{};
            //       std::optional<u8> LastUsedGeneralCommandBuffer{std::nullopt};  // Stores index inside pool, u8 is enough.

            vk::CommandBuffer GeneralCommandBuffer{};  // Latest submitted cmdbuf, used in Present.

            vk::UniqueCommandPool AsyncComputeCommandPoolVK{};
            Pool<vk::CommandBuffer> AsyncComputeCommandPool{};
            std::optional<u8> LastUsedAsyncComputeCommandBuffer{
                std::nullopt};  // Stores index inside pool, u8 is enough, no fucking buddy gonna submit >256 cmd buffs omg

            vk::UniqueCommandPool DedicatedTransferCommandPoolVK{};
            Pool<vk::CommandBuffer> DedicatedTransferCommandPool{};
            std::optional<u8> LastUsedDedicatedTransferCommandBuffer{std::nullopt};  // Stores index inside pool, u8 is enough.

            vk::UniqueFence RenderFinishedFence{};
            vk::UniqueSemaphore ImageAvailableSemaphore{};
            vk::UniqueSemaphore RenderFinishedSemaphore{};
        };
        std::array<FrameData, s_BufferedFrameCount> m_FrameData{};

        // Swapchain things
        u64 m_GlobalFrameNumber{0};  // Used to help to determine device's DeferredDeletionQueue flush.
        vk::Extent2D m_SwapchainExtent{};
        vk::Format m_SwapchainImageFormat{};
        vk::UniqueSurfaceKHR m_Surface{};
        vk::UniqueSwapchainKHR m_Swapchain{};
        vk::PresentModeKHR m_PresentMode{vk::PresentModeKHR::eFifo};
        std::vector<vk::PresentModeKHR> m_SupportedPresentModes;
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
