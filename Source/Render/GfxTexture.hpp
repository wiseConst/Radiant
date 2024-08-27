#pragma once

#include <Core/Core.hpp>

#define VK_NO_PROTOTYPES
#include <vk_mem_alloc.h>

#include <vulkan/vulkan.hpp>

namespace Radiant
{

    namespace GfxTextureUtils
    {
        void* LoadImage(const std::string_view& imagePath, i32& width, i32& height, i32& channels,
                        const i32 requestedChannels = 4) noexcept;

        void* LoadImage(const void* rawImageData, const std::size_t rawImageDataSize, i32& width, i32& height, i32& channels,
                        const i32 requestedChannels = 4) noexcept;

        void UnloadImage(void* imageData) noexcept;

        u32 GetMipLevelCount(const u32 width, const u32 height) noexcept;

    }  // namespace GfxTextureUtils

    struct GfxTextureDescription
    {
        GfxTextureDescription(const vk::ImageType type, const glm::uvec3& dimensions, const vk::Format format,
                              const vk::ImageUsageFlags usageFlags,
                              const std::optional<vk::SamplerCreateInfo> samplerCreateInfo = std::nullopt, const u32 layerCount = 1,
                              const vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1, const bool bExposeMips = false,
                              const bool bGenerateMips = false, const bool bControlledByRenderGraph = false) noexcept
            : Type(type), Dimensions(dimensions), Format(format), UsageFlags(usageFlags), SamplerCreateInfo(samplerCreateInfo),
              LayerCount(layerCount), Samples(samples), bExposeMips(bExposeMips), bGenerateMips(bGenerateMips),
              bControlledByRenderGraph(bControlledByRenderGraph)
        {
            UsageFlags |= vk::ImageUsageFlagBits::eSampled;

            if (bGenerateMips) UsageFlags |= vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
        }
        constexpr GfxTextureDescription() noexcept =
            default;  // NOTE: NEVER USE IT, IT'S NOT DELETED ONLY FOR COMPATIBILITY WITH maps/other containers!
        ~GfxTextureDescription() noexcept = default;

        vk::ImageType Type{vk::ImageType::e2D};
        glm::uvec3 Dimensions{1};
        vk::Format Format{vk::Format::eR8G8B8A8Unorm};
        vk::ImageUsageFlags UsageFlags{vk::ImageUsageFlagBits::eSampled};
        std::optional<vk::SamplerCreateInfo> SamplerCreateInfo{std::nullopt};
        u32 LayerCount{1};
        vk::SampleCountFlagBits Samples{vk::SampleCountFlagBits::e1};
        bool bExposeMips{false};               // Create MipChain of image views?
        bool bGenerateMips{false};             // NOTE: Used only for mesh textures, doesn't create MipChain of image views!
        bool bControlledByRenderGraph{false};  // NOTE: Means resource can be only created but no be bind to any memory.

        // NOTE: We don't care about dimensions cuz we can resize wherever we want.
        bool operator!=(const GfxTextureDescription& other) const noexcept
        {
            return std::tie(Type, bExposeMips, bGenerateMips, LayerCount, Format, UsageFlags, SamplerCreateInfo, Samples,
                            bControlledByRenderGraph) != std::tie(other.Type, other.bExposeMips, other.bGenerateMips, other.LayerCount,
                                                                  other.Format, other.UsageFlags, other.SamplerCreateInfo, other.Samples,
                                                                  other.bControlledByRenderGraph);
        }
    };

    // TODO: Cubemaps
    class GfxDevice;
    class GfxTexture final : private Uncopyable, private Unmovable
    {
      public:
        GfxTexture(const Unique<GfxDevice>& device, const GfxTextureDescription& textureDesc) noexcept
            : m_Device(device), m_Description(textureDesc)
        {
            m_UUID = ankerl::unordered_dense::detail::wyhash::hash(this, sizeof(GfxTexture));
            RDNT_ASSERT((m_Description.bExposeMips && m_Description.bGenerateMips) == false,
                        "GfxTexture can't have both bExposeMips && bGenerateMips specified!");
            Invalidate();
        }
        ~GfxTexture() noexcept { Destroy(); }

        void RenderGraph_Finalize() noexcept;
        operator vk::Image&() noexcept { return m_Image; }

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

        void GenerateMipMaps(const vk::CommandBuffer& cmd) const noexcept;
        bool Resize(const glm::uvec3& dimensions) noexcept;

        FORCEINLINE u32 GetMipChainSize() const noexcept { return m_MipChain.size(); }
        NODISCARD FORCEINLINE u32 GetBindlessImageID(const u32 mipLevel = 0) const noexcept
        {
            RDNT_ASSERT(mipLevel < m_MipChain.size() && m_MipChain[mipLevel].BindlessImageID.has_value(),
                        "Invalid mip level or BindlessImageID!");
            return *m_MipChain[mipLevel].BindlessImageID;
        }
        NODISCARD FORCEINLINE u32 GetBindlessTextureID(const u32 mipLevel = 0) const noexcept
        {
            RDNT_ASSERT(mipLevel < m_MipChain.size() && m_MipChain[mipLevel].BindlessTextureID.has_value(),
                        "Invalid mip level or BindlessTextureID!");
            return *m_MipChain[mipLevel].BindlessTextureID;
        }

        NODISCARD FORCEINLINE const auto& GetDescription() const noexcept { return m_Description; }
        NODISCARD FORCEINLINE vk::RenderingAttachmentInfo GetRenderingAttachmentInfo(const vk::ImageLayout imageLayout,
                                                                                     const vk::ClearValue& clearValue,
                                                                                     const vk::AttachmentLoadOp loadOp,
                                                                                     const vk::AttachmentStoreOp storeOp,
                                                                                     const u32 mipLevel = 0) const noexcept
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
            std::optional<u32> BindlessImageID{std::nullopt};
            std::optional<u32> BindlessTextureID{std::nullopt};
        };
        std::vector<MipInfo> m_MipChain{};
        VmaAllocation m_Allocation{};

        constexpr GfxTexture() noexcept = delete;
        void Invalidate() noexcept;
        void CreateMipChainAndSubmitToBindlessPool() noexcept;
        void Destroy() noexcept;
    };

}  // namespace Radiant
