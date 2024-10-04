#pragma once

#include <Render/CoreDefines.hpp>
#include <vulkan/vulkan.hpp>

#define VK_NO_PROTOTYPES
#include <vk_mem_alloc.h>

#include <nvtt/nvtt.h>

namespace Radiant
{

    namespace GfxTextureUtils
    {

        struct TextureCompressor final : private Uncopyable, private Unmovable
        {
          public:
            TextureCompressor() noexcept  = default;
            ~TextureCompressor() noexcept = default;

            struct TextureInfo final
            {
                glm::uvec2 Dimensions;
                std::vector<u8> Data;
            };

            void PushTextureIntoBatchList(const std::string& texturePath, const vk::Format format) noexcept;
            void CompressAndCache() noexcept;

            NODISCARD static std::vector<TextureCompressor::TextureInfo> LoadTextureCache(const std::string& texturePath,
                                                                                          const vk::Format format) noexcept;
            NODISCARD static std::vector<TextureCompressor::TextureInfo> CompressSingle(
                const std::string& texturePath, const vk::Format format, const bool bBuildMips = false,
                const nvtt::Quality compressionQuality = nvtt::Quality::Quality_Fastest) noexcept;

          private:
            UnorderedMap<vk::Format, std::vector<std::string>> m_TexturesToLoad{};

            // NOTE: MipCount, vk::Format will be added as needed.
            struct TextureHeader
            {
                glm::uvec2 Dimensions{};
                u32 MipCount{};
            };

            struct RadiantTextureFileWriter : nvtt::OutputHandler
            {
              public:
                RadiantTextureFileWriter(const std::string& path, const glm::uvec2& dimensions, const u32 mipCount)
                    : m_RawDataFile(path, std::ios::binary | std::ios::trunc)
                {
                    if (!m_RawDataFile.is_open()) LOG_ERROR("Failed to open file {}", path);

                    const auto textureHeader = TextureHeader{.Dimensions{dimensions}, .MipCount = mipCount};

                    m_RawDataFile.write((const char*)&textureHeader, sizeof(textureHeader));
                }

                ~RadiantTextureFileWriter() { m_RawDataFile.close(); }

                virtual void beginImage(const i32 size, const i32 width, const i32 height, const i32 depth, const i32 face,
                                        const i32 miplevel) override
                {
                }
                virtual void endImage() override {}

                virtual bool writeData(const void* data, const i32 size) override
                {
                    m_RawDataFile.write((const char*)&size, sizeof(size));
                    m_RawDataFile.write((const char*)data, size);
                    return true;
                }

              private:
                std::ofstream m_RawDataFile;
            };

            NODISCARD static const std::string DetermineTextureCachePath(const std::string& texturePath, const vk::Format format) noexcept;
            NODISCARD static bool IsCacheExist(const std::string& texturePath, const vk::Format format) noexcept;
        };

        void* LoadImage(const std::string_view& imagePath, i32& width, i32& height, i32& channels, const i32 requestedChannels = 4,
                        const bool bFlipOnLoad = false) noexcept;

        void* LoadImage(const void* rawImageData, const u64 rawImageDataSize, i32& width, i32& height, i32& channels,
                        const i32 requestedChannels = 4, const bool bFlipOnLoad = false) noexcept;

        void UnloadImage(void* imageData) noexcept;

        u32 GetMipLevelCount(const u32 width, const u32 height) noexcept;

    }  // namespace GfxTextureUtils

    struct GfxTextureDescription
    {
        GfxTextureDescription(const vk::ImageType type, const glm::uvec3& dimensions, const vk::Format format,
                              const vk::ImageUsageFlags usageFlags,
                              const std::optional<vk::SamplerCreateInfo> samplerCreateInfo = std::nullopt, const u32 layerCount = 1,
                              const vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,
                              const ResourceCreateFlags createFlags = {}) noexcept
            : Type(type), Dimensions(dimensions), Format(format), UsageFlags(usageFlags), SamplerCreateInfo(samplerCreateInfo),
              LayerCount(layerCount), Samples(samples), CreateFlags(createFlags)
        {
            UsageFlags |= vk::ImageUsageFlagBits::eSampled;

            if (CreateFlags & EResourceCreateBits::RESOURCE_CREATE_GENERATE_MIPS_BIT)
                UsageFlags |= vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
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
        ResourceCreateFlags CreateFlags{};

        // NOTE: We don't care about dimensions cuz we can resize wherever we want.
        FORCEINLINE constexpr bool operator!=(const GfxTextureDescription& other) const noexcept
        {
            return std::tie(Type, CreateFlags, LayerCount, Format, UsageFlags, SamplerCreateInfo, Samples) !=
                   std::tie(other.Type, other.CreateFlags, other.LayerCount, other.Format, other.UsageFlags, other.SamplerCreateInfo,
                            other.Samples);
        }
    };

    class GfxDevice;
    class GfxTexture final : private Uncopyable, private Unmovable
    {
      public:
        GfxTexture(const Unique<GfxDevice>& device, const GfxTextureDescription& textureDesc) noexcept
            : m_Device(device), m_Description(textureDesc)
        {
            m_UUID                   = ankerl::unordered_dense::detail::wyhash::hash(this, sizeof(GfxTexture));
            const bool bExposeMips   = m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_EXPOSE_MIPS_BIT;
            const bool bGenerateMips = m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_GENERATE_MIPS_BIT;
            RDNT_ASSERT((bExposeMips && bGenerateMips) == false, "GfxTexture can't have both bExposeMips && bGenerateMips specified!");
            Invalidate();
        }
        ~GfxTexture() noexcept { Destroy(); }

        void Invalidate() noexcept;
        void RG_Finalize() noexcept;
        operator vk::Image&() noexcept
        {
            RDNT_ASSERT(m_Image.has_value(), "Image is invalid!");
            return *m_Image;
        }

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
                .setImageView(m_MipChain[mipLevel].ImageView)
                .setImageLayout(imageLayout)
                .setClearValue(clearValue)
                .setLoadOp(loadOp)
                .setStoreOp(storeOp);
        }

      private:
        const Unique<GfxDevice>& m_Device;
        u64 m_UUID{0};
        GfxTextureDescription m_Description{};
        std::optional<vk::Image> m_Image{std::nullopt};

        struct MipInfo
        {
            vk::ImageView ImageView{};
            std::optional<u32> BindlessImageID{std::nullopt};
            std::optional<u32> BindlessTextureID{std::nullopt};
        };
        std::vector<MipInfo> m_MipChain{};
        VmaAllocation m_Allocation{VK_NULL_HANDLE};

        constexpr GfxTexture() noexcept = delete;
        void CreateMipChainAndSubmitToBindlessPool() noexcept;
        void Destroy() noexcept;
    };

}  // namespace Radiant
