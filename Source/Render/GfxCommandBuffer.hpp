#pragma once

#include <Core/Core.hpp>

namespace Radiant
{

    struct GfxCommandBufferDescription
    {
    };

    class GfxCommandBuffer
    {
      public:
      protected:
        GfxCommandBufferDescription m_Description{};

      private:
        constexpr GfxCommandBuffer() noexcept = delete;
    };

}  // namespace Radiant
