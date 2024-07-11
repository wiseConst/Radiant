#pragma once

#include <Core/Core.hpp>
#include <Render/CoreDefines.hpp>

namespace Radiant
{

    class Camera final : private Uncopyable, private Unmovable
    {
      public:
        ~Camera() noexcept {}

      private:
        constexpr Camera() = delete;
    };

}  // namespace Radiant
