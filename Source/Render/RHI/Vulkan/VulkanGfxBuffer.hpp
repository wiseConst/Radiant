#pragma once

#include <Render/GfxBuffer.hpp>
#include <Render/RHI/Vulkan/VulkanRenderSystem.hpp>

#define VK_NO_PROTOTYPES
#include "vma/vk_mem_alloc.h"

namespace Radiant
{

    class GfxVulkanBuffer final : public GfxBuffer
    {
      public:
        GfxVulkanBuffer(const GfxBufferDescription& description) noexcept : GfxBuffer(description) { Invalidate(); }
        ~GfxVulkanBuffer() noexcept final override { Shutdown(); }

      private:
        vk::UniqueBuffer m_Handle{nullptr};
        VmaAllocation m_Allocation{};

        constexpr GfxVulkanBuffer() noexcept = delete;

        void Invalidate() noexcept;
        void Shutdown() noexcept;
    };

}  // namespace Radiant
