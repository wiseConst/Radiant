#pragma once

#include <Render/CoreDefines.hpp>
#include <Render/GfxDevice.hpp>

#define VK_NO_PROTOTYPES
#include <vk_mem_alloc.h>

namespace Radiant
{

    namespace GfxTextureUtils
    {
        void* LoadImage(const std::string_view& imagePath, std::int32_t& width, std::int32_t& height, std::int32_t& channels,
                        const std::int32_t requestedChannels = 4) noexcept;

        void* LoadImage(const void* rawImageData, const std::size_t rawImageDataSize, std::int32_t& width, std::int32_t& height,
                        std::int32_t& channels, const std::int32_t requestedChannels = 4) noexcept;

        void UnloadImage(void* imageData) noexcept;

        std::uint32_t GetMipLevelCount(const std::uint32_t width, const std::uint32_t height) noexcept;

    }  // namespace GfxTextureUtils

    struct GfxTextureDescription
    {
        vk::ImageType Type{vk::ImageType::e2D};
        glm::uvec3 Dimensions{1};
        bool bExposeMips{false};    // Create MipChain of image views?
        bool bGenerateMips{false};  // NOTE: Used only for mesh textures, doesn't create MipChain of image views!
        std::uint32_t LayerCount{1};
        vk::Format Format{vk::Format::eR8G8B8A8Unorm};
        vk::ImageUsageFlags UsageFlags{vk::ImageUsageFlagBits::eSampled};
        std::optional<vk::SamplerCreateInfo> SamplerCreateInfo{std::nullopt};
        vk::SampleCountFlagBits Samples{vk::SampleCountFlagBits::e1};

        // NOTE: We don't care about dimensions cuz we can resize wherever we want.
        bool operator!=(const GfxTextureDescription& other) const noexcept
        {
            return std::tie(Type, bExposeMips, bGenerateMips, LayerCount, Format, UsageFlags, SamplerCreateInfo, Samples) !=
                   std::tie(other.Type, other.bExposeMips, other.bGenerateMips, other.LayerCount, other.Format, other.UsageFlags,
                            other.SamplerCreateInfo, other.Samples);
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
            RDNT_ASSERT((m_Description.bExposeMips && m_Description.bGenerateMips) == false,
                        "GfxTexture can't have both bExposeMips && bGenerateMips specified!");
            if (m_Description.bGenerateMips)
                m_Description.UsageFlags |= vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
            Invalidate();
        }
        ~GfxTexture() noexcept { Destroy(); }

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

        void GenerateMipMaps(const vk::UniqueCommandBuffer& cmd) const noexcept;
        void Resize(const glm::uvec3& dimensions) noexcept;

        NODISCARD FORCEINLINE std::uint32_t GetBindlessImageID(const std::uint8_t mipLevel = 0) const noexcept
        {
            RDNT_ASSERT(mipLevel < m_MipChain.size() && m_MipChain[mipLevel].BindlessImageID.has_value(),
                        "Invalid mip level or BindlessImageID!");
            return *m_MipChain[mipLevel].BindlessImageID;
        }
        NODISCARD FORCEINLINE std::uint32_t GetBindlessTextureID(const std::uint8_t mipLevel = 0) const noexcept
        {
            RDNT_ASSERT(mipLevel < m_MipChain.size() && m_MipChain[mipLevel].BindlessTextureID.has_value(),
                        "Invalid mip level or BindlessTextureID!");
            return *m_MipChain[mipLevel].BindlessTextureID;
        }

        NODISCARD FORCEINLINE const auto& GetDescription() noexcept { return m_Description; }
        NODISCARD FORCEINLINE vk::RenderingAttachmentInfo GetRenderingAttachmentInfo(const vk::ImageLayout imageLayout,
                                                                                     const vk::ClearValue& clearValue,
                                                                                     const vk::AttachmentLoadOp loadOp,
                                                                                     const vk::AttachmentStoreOp storeOp,
                                                                                     const std::uint8_t mipLevel = 0) const noexcept
        {
            return vk::RenderingAttachmentInfo()
                .setImageView(*m_MipChain[mipLevel].ImageView)
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

        struct MipInfo
        {
            vk::UniqueImageView ImageView{};
            std::optional<std::uint32_t> BindlessImageID{std::nullopt};
            std::optional<std::uint32_t> BindlessTextureID{std::nullopt};
        };
        std::vector<MipInfo> m_MipChain{};
        VmaAllocation m_Allocation{};

        constexpr GfxTexture() noexcept = delete;
        void Invalidate() noexcept;
        void Destroy() noexcept;
    };

}  // namespace Radiant
