#pragma once

#include <Core/Core.hpp>
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

namespace Radiant
{

    class RenderSystem final : private Uncopyable, private Unmovable
    {
      public:
        RenderSystem() noexcept { Init(); }
        ~RenderSystem() noexcept { Shutdown(); };

        bool BeginFrame() noexcept;
        void EndFrame() noexcept;

      private:
        vk::UniqueInstance m_Instance{};
        vk::UniqueDebugUtilsMessengerEXT m_DebugUtilsMessenger{};

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

        struct Queue
        {
            vk::Queue Handle{};
            std::optional<std::uint32_t> QueueFamilyIndex{std::nullopt};
        };

        vk::UniqueDevice m_LogicalDevice{};
        vk::PhysicalDevice m_PhysicalDevice{};
        vk::UniquePipelineCache m_PipelineCache{};

        Queue m_GCTQueue{};      // graphics / compute / transfer
        Queue m_PresentQueue{};  // present

        std::array<FrameData, s_BufferedFrameCount> m_FrameData;

        // Bindless resources pt. 2
        vk::UniqueDescriptorSetLayout m_DescriptorSetLayout{};
        vk::UniquePipelineLayout m_PipelineLayout{};

        vk::Extent2D m_SwapchainExtent{};
        vk::UniqueSurfaceKHR m_Surface{};
        vk::UniqueSwapchainKHR m_Swapchain{};
        std::uint32_t m_CurrentFrameIndex{0};
        std::uint32_t m_CurrentImageIndex{0};
        std::vector<vk::UniqueImageView> m_SwapchainImageViews;
        std::vector<vk::Image> m_SwapchainImages;

        // Other shit not tied to this class
        vk::UniquePipeline m_TriPipeline{};

        void Init() noexcept;
        void CreateInstanceAndDebugUtilsMessenger() noexcept;
        void SelectGPUAndCreateLogicalDevice(std::vector<const char*>& requiredDeviceExtensions,
                                             const vk::PhysicalDeviceFeatures& requiredDeviceFeatures,
                                             const void* pNext = nullptr) noexcept;
        void CreateSwapchain() noexcept;
        void LoadPipelineCache() noexcept;
        void CreateFrameResources() noexcept;
        void Shutdown() noexcept;
    };

}  // namespace Radiant
