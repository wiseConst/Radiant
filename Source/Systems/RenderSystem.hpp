#pragma once

#include <Core/Core.hpp>

namespace Radiant
{

    enum class ERHI : std::uint8_t
    {
        RHI_NONE,
        RHI_VULKAN,
        RHI_DX12,
    };

    class RenderSystem
    {
      public:
        RenderSystem(const ERHI rhi) noexcept : m_RHI(rhi) { RDNT_ASSERT(m_RHI != ERHI::RHI_NONE, "Invalid RHI!") }
        virtual ~RenderSystem() noexcept = default;

      protected:
        ERHI m_RHI{ERHI::RHI_NONE};

      private:
        constexpr RenderSystem() noexcept = delete;
    };

}  // namespace Radiant
