#pragma once

#include <Core/Core.hpp>
#include <Systems/RenderSystem.hpp>

#define VK_NO_PROTOTYPES
#include "vma/vk_mem_alloc.h"

namespace Radiant
{

    struct GfxBufferDescription
    {
        std::size_t Capacity{};
        std::size_t ElementSize{};
    };

    class GfxBuffer final : private Uncopyable, private Unmovable
    {
      public:
        GfxBuffer(const GfxBufferDescription& description) noexcept : m_Description(description) {}
        ~GfxBuffer() noexcept = default;

      private:
        GfxBufferDescription m_Description{};
        vk::UniqueBuffer m_Handle{nullptr};
        VmaAllocation m_Allocation{};

        constexpr GfxBuffer() noexcept = delete;
        void Invalidate() noexcept;
        void Shutdown() noexcept;
    };

}  // namespace Radiant
