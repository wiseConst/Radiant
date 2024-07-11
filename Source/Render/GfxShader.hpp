#pragma once

#include <Core/Core.hpp>

namespace Radiant
{

    struct GfxShaderDescription
    {
        glm::uvec3 Dimensions{1};
    };

    class GfxShader
    {
      public:
      protected:
        GfxShaderDescription m_Description{};
    };

}  // namespace Radiant
