#pragma once

#include <Core/Core.hpp>

namespace Radiant
{

    enum class ETextureType : std::uint8_t
    {
        TEXTURE_TYPE_1D,
        TEXTURE_TYPE_3D,
        TEXTURE_TYPE_2D
    };

    struct GfxTextureDescription
    {
        ETextureType TextureType{ETextureType::TEXTURE_TYPE_1D};
        glm::uvec3 Dimensions{1};
    };

    class GfxTexture
    {
      public:
      protected:
        GfxTextureDescription m_Description{};

        GfxTexture(const GfxTextureDescription& description) noexcept : m_Description(description){};

      private:
        constexpr GfxTexture() noexcept = delete;
    };

}  // namespace Radiant
