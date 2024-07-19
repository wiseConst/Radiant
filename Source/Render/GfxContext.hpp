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

// Including device first place ruins surface creation!
#include <Render/GfxDevice.hpp>

namespace Radiant
{

    class GfxContext final : private Uncopyable, private Unmovable
    {
      public:
        GfxContext() noexcept { Init(); }
        ~GfxContext() noexcept { Shutdown(); };

        bool BeginFrame() noexcept;
        void EndFrame() noexcept;

        NODISCARD FORCEINLINE auto& GetBindlessPipelineLayout() const noexcept { return m_PipelineLayout; }
        NODISCARD FORCEINLINE auto& GetDevice() const noexcept { return m_Device; }
        NODISCARD FORCEINLINE auto& GetCurrentFrameData() const noexcept { return m_FrameData[m_CurrentFrameIndex]; }
        NODISCARD FORCEINLINE const auto& GetSwapchainExtent() const noexcept { return m_SwapchainExtent; }
        NODISCARD FORCEINLINE const auto& GetCurrentSwapchainImage() const noexcept { return m_SwapchainImages[m_CurrentImageIndex]; }
        NODISCARD FORCEINLINE const auto& GetCurrentSwapchainImageView() const noexcept
        {
            return m_SwapchainImageViews[m_CurrentImageIndex];
        }
        NODISCARD FORCEINLINE const auto GetGlobalFrameNumber() const noexcept { return m_GlobalFrameNumber; }

        NODISCARD vk::UniqueCommandBuffer AllocateCommandBuffer() noexcept
        {
            return std::move(m_Device->GetLogicalDevice()
                                 ->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo()
                                                                    .setCommandBufferCount(1)
                                                                    .setLevel(vk::CommandBufferLevel::ePrimary)
                                                                    .setCommandPool(*m_FrameData[m_CurrentFrameIndex].CommandPool))
                                 .back());
        }

        NODISCARD FORCEINLINE static const auto& Get() noexcept
        {
            RDNT_ASSERT(s_Instance, "GfxContext instance is invalid!");
            return *s_Instance;
        }

      private:
        static inline GfxContext* s_Instance{nullptr};  // NOTE: Used only for safely pushing objects through device into deletion queue.
        vk::UniqueInstance m_Instance{};
        vk::UniqueDebugUtilsMessengerEXT m_DebugUtilsMessenger{};
        Unique<GfxDevice> m_Device{};

        struct FrameData
        {
            vk::UniqueCommandPool CommandPool{};
            vk::CommandBuffer CommandBuffer{};

            vk::UniqueFence RenderFinishedFence{};
            vk::UniqueSemaphore ImageAvailableSemaphore{};
            vk::UniqueSemaphore RenderFinishedSemaphore{};

            // Bindless resources pt. 1
            vk::UniqueDescriptorPool DescriptorPool{};
            vk::DescriptorSet DescriptorSet{};
        };
        std::array<FrameData, s_BufferedFrameCount> m_FrameData;

        // Bindless resources pt. 2
        vk::UniqueDescriptorSetLayout m_DescriptorSetLayout{};
        vk::UniquePipelineLayout m_PipelineLayout{};

        std::uint64_t m_GlobalFrameNumber{0};  // Used to help determine device's DeferredDeletionQueue flush.
        vk::Extent2D m_SwapchainExtent{};
        vk::UniqueSurfaceKHR m_Surface{};
        vk::UniqueSwapchainKHR m_Swapchain{};
        std::uint32_t m_CurrentFrameIndex{0};
        std::uint32_t m_CurrentImageIndex{0};
        std::vector<vk::UniqueImageView> m_SwapchainImageViews;
        std::vector<vk::Image> m_SwapchainImages;
        bool m_bSwapchainNeedsResize{false};

        void Init() noexcept;
        void CreateInstanceAndDebugUtilsMessenger() noexcept;
        void CreateSurface() noexcept;
        void InvalidateSwapchain() noexcept;
        void CreateFrameResources() noexcept;
        void Shutdown() noexcept;
    };

}  // namespace Radiant
