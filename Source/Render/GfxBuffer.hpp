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
        GfxBuffer(const GfxBufferDescription& description) noexcept : m_Description(description) { Invalidate(); }
        ~GfxBuffer() noexcept { Shutdown(); }

        NODISCARD FORCEINLINE auto GetBDA() const noexcept
        {
            RDNT_ASSERT(m_BDA.has_value(), "BDA is invalid!");
            return m_BDA.value();
        }

      private:
        GfxBufferDescription m_Description{};
        vk::UniqueBuffer m_Handle{nullptr};
        VmaAllocation m_Allocation{};
        std::optional<std::uint64_t> m_BDA{std::nullopt};

        constexpr GfxBuffer() noexcept = delete;
        void Invalidate() noexcept;
        void Shutdown() noexcept;
    };

}  // namespace Radiant
