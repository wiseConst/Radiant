#pragma once

#include <Core/Core.hpp>
#include <Systems/RenderSystem.hpp>

#include <Volk/volk.h>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

namespace Radiant
{

    class VulkanRenderSystem final : public RenderSystem
    {
      public:
        VulkanRenderSystem(const ERHI rhi) noexcept : RenderSystem(rhi) { Init(); }
        ~VulkanRenderSystem() noexcept final override { Shutdown(); };

      private:
        vk::UniqueInstance m_Instance{};
        vk::DispatchLoaderDynamic m_DispatchLoaderDynamic{};

        constexpr VulkanRenderSystem() noexcept = delete;
        void Init() noexcept;
        void Shutdown() noexcept;
    };

}  // namespace Radiant
