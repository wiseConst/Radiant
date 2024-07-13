#pragma once

#include <Render/GfxTexture.hpp>
#include <Render/RHI/Vulkan/VulkanRenderSystem.hpp>

#define VK_NO_PROTOTYPES
#include "vma/vk_mem_alloc.h"

namespace Radiant
{

    class GfxVulkanTexture final : public GfxTexture
    {
      public:
        GfxVulkanTexture(const GfxTextureDescription& description) noexcept : GfxTexture(description) { Invalidate(); }
        ~GfxVulkanTexture() noexcept final override { Shutdown(); }

      private:
        vk::UniqueImage m_Image{nullptr};
        VmaAllocation m_Allocation{};

        constexpr GfxVulkanTexture() noexcept = delete;

        void Invalidate() noexcept;
        void Shutdown() noexcept;
    };

}  // namespace Radiant
