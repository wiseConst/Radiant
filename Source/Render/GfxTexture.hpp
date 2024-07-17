#pragma once

#include <Render/CoreDefines.hpp>
#include <Render/GfxDevice.hpp>

#define VK_NO_PROTOTYPES
#include "vma/vk_mem_alloc.h"

namespace Radiant
{

    struct GfxTextureDescription
    {
        vk::ImageType Type{vk::ImageType::e2D};
        glm::uvec3 Dimensions{1};
        /*  bool bGenerateMips;
          bool bExposeMips;*/
    };

    class GfxTexture final : private Uncopyable, private Unmovable
    {
      public:
        GfxTexture(const Unique<GfxDevice>& device, const GfxTextureDescription& textureDesc) noexcept
            : m_Device(device), m_Description(textureDesc)
        {
            Invalidate();
        };
        ~GfxTexture() noexcept { Shutdown(); }

      private:
        const Unique<GfxDevice>& m_Device;
        GfxTextureDescription m_Description{};
        vk::UniqueImage m_Image{nullptr};
        vk::UniqueImageView m_ImageView{nullptr};  // Base mip level(0)
        std::vector<vk::UniqueImageView> m_MipChain{};
        VmaAllocation m_Allocation{};

        constexpr GfxTexture() noexcept = delete;
        void Invalidate() noexcept;
        void Shutdown() noexcept;
    };

}  // namespace Radiant
