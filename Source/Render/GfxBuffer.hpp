#pragma once

#include <Core/Core.hpp>

namespace Radiant
{

    struct GfxBufferDescription
    {
        std::size_t Size{};
    };

    class GfxBuffer
    {
      public:
        GfxBuffer(const GfxBufferDescription& description) noexcept : m_Description(description) {}
        virtual ~GfxBuffer() noexcept = default;

      protected:
        GfxBufferDescription m_Description{};

      private:
        constexpr GfxBuffer() noexcept = delete;
    };

}  // namespace Radiant
