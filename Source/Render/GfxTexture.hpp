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
        GfxTexture(const GfxTextureDescription& description) noexcept : m_Description(description){};
        virtual ~GfxTexture() noexcept = default;

      protected:
        GfxTextureDescription m_Description{};

      private:
        constexpr GfxTexture() noexcept = delete;
    };

}  // namespace Radiant
