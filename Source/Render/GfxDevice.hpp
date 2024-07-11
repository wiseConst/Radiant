#pragma once

#include <Core/Core.hpp>

namespace Radiant
{

    struct GfxDeviceDescription
    {
    };

    class GfxDevice
    {
      public:
        virtual void WaitTillWorkFinished() const noexcept = 0;

      protected:
        GfxDeviceDescription m_Description{};

      private:
        constexpr GfxDevice() noexcept = delete;
    };

}  // namespace Radiant
