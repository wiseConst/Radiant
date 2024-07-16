#pragma once

#include <Core/Core.hpp>
#include <Systems/RenderSystem.hpp>

#define VK_NO_PROTOTYPES
#include "vma/vk_mem_alloc.h"

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

    class GfxTexture final : private Uncopyable, private Unmovable
    {
      public:
        GfxTexture(const GfxTextureDescription& description) noexcept : m_Description(description) { Invalidate(); };
        ~GfxTexture() noexcept { Shutdown(); }

      private:
        GfxTextureDescription m_Description{};
        vk::UniqueImage m_Image{nullptr};
        vk::UniqueImageView m_ImageView{nullptr};  // Base mip level(0)
        VmaAllocation m_Allocation{};

        constexpr GfxTexture() noexcept = delete;
        void Invalidate() noexcept;
        void Shutdown() noexcept;
    };

}  // namespace Radiant
