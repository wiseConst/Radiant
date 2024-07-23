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
        bool bExposeMips{false};  // Create image view per mip?
        std::uint32_t LayerCount{1};
        vk::Format Format{vk::Format::eR8G8B8A8Unorm};
        vk::ImageUsageFlags UsageFlags{vk::ImageUsageFlagBits::eSampled};

        // NOTE: We don't care about dimensions cuz we can resize wherever we want.
        bool operator!=(const GfxTextureDescription& other) const noexcept
        {
            return std::tie(Type, bExposeMips, LayerCount, Format, UsageFlags) !=
                   std::tie(other.Type, other.bExposeMips, other.LayerCount, other.Format, other.UsageFlags);
        }
    };

    class GfxTexture final : private Uncopyable, private Unmovable
    {
      public:
        GfxTexture(const Unique<GfxDevice>& device, const GfxTextureDescription& textureDesc) noexcept
            : m_Device(device), m_Description(textureDesc)
        {
            m_Description.UsageFlags |= vk::ImageUsageFlagBits::eSampled;
            m_UUID = ankerl::unordered_dense::detail::wyhash::hash(this, sizeof(GfxTexture));
            Invalidate();
        }
        ~GfxTexture() noexcept { Destroy(); }

        NODISCARD FORCEINLINE const auto& GetDescription() noexcept { return m_Description; }

        operator const vk::Image&() const noexcept { return m_Image; }

        NODISCARD FORCEINLINE static bool IsDepthFormat(const vk::Format format) noexcept
        {
            switch (format)
            {
                case vk::Format::eD16Unorm:
                case vk::Format::eX8D24UnormPack32:
                case vk::Format::eD32Sfloat:
                case vk::Format::eD16UnormS8Uint:
                case vk::Format::eD24UnormS8Uint:
                case vk::Format::eD32SfloatS8Uint: return true;
            }

            return false;
        }

        void Resize(const glm::uvec3& dimensions) noexcept;

        NODISCARD FORCEINLINE vk::RenderingAttachmentInfo GetRenderingAttachmentInfo(const vk::ImageLayout imageLayout,
                                                                                     const vk::ClearValue& clearValue,
                                                                                     const vk::AttachmentLoadOp loadOp,
                                                                                     const vk::AttachmentStoreOp storeOp,
                                                                                     const std::uint8_t mipLevel = 0) const noexcept
        {
            return vk::RenderingAttachmentInfo()
                .setImageView(*m_MipChain.at(mipLevel))
                .setImageLayout(imageLayout)
                .setClearValue(clearValue)
                .setLoadOp(loadOp)
                .setStoreOp(storeOp);
        }

      private:
        std::size_t m_UUID{0};
        const Unique<GfxDevice>& m_Device;
        GfxTextureDescription m_Description{};
        vk::Image m_Image{};
        std::vector<vk::UniqueImageView> m_MipChain{};
        VmaAllocation m_Allocation{};

        constexpr GfxTexture() noexcept = delete;
        void Invalidate() noexcept;
        void Destroy() noexcept;
    };

}  // namespace Radiant
