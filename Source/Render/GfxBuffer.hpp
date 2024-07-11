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
      protected:
        GfxBufferDescription m_Description{};
    };

}  // namespace Radiant
