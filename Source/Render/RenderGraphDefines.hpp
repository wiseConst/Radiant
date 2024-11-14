#pragma once

#include <Render/CoreDefines.hpp>

#include <Render/GfxContext.hpp>
#include <Render/GfxDevice.hpp>
#include <Render/GfxTexture.hpp>
#include <Render/GfxBuffer.hpp>

namespace Radiant
{
    class RenderGraph;
    class RenderGraphPass;
    class RenderGraphResourcePool;
    class RenderGraphResourceScheduler;

    static constexpr u8 s_MaxColorRenderTargets = 8;  // NOTE: Defined across all GAPI's AFAIK.

    using MipRange = std::pair<u32, std::optional<u32>>;  // NOTE: In case lastMip not specified, it means unbound(till the end).
    struct MipSet final
    {
        NODISCARD FORCEINLINE static MipSet Explicit(const u32 mipLevel) noexcept
        {
            return MipSet{.Combination = std::make_optional<MipVariant>(mipLevel)};
        }
        NODISCARD FORCEINLINE static MipSet FirstMip() noexcept
        {
            return MipSet{.Combination = std::make_optional<MipVariant>(std::numeric_limits<u32>::min())};
        }
        NODISCARD FORCEINLINE static MipSet LastMip() noexcept
        {
            return MipSet{.Combination = std::make_optional<MipVariant>(std::numeric_limits<u32>::max())};
        }
        NODISCARD FORCEINLINE static MipSet AllMips() noexcept { return Range(0, std::nullopt); }
        NODISCARD FORCEINLINE static MipSet Range(const u32 firstMip, const std::optional<u32>& lastMip) noexcept
        {
            if (lastMip.has_value()) RDNT_ASSERT(firstMip < *lastMip, "Range should be like this: firstMip < lastMip!");
            return MipSet{.Combination = std::make_optional<MipVariant>(MipRange(firstMip, lastMip))};
        }

        using MipVariant = std::variant<MipRange, u32>;  // NOTE: u32::max() means LastMip, u32::min() means FirstMip
        std::optional<MipVariant> Combination{std::nullopt};
    };

    struct RenderGraphDetectedQueue
    {
        RenderGraphDetectedQueue(const ECommandQueueType commandQueueType, const u8 commandQueueIndex) noexcept
            : CommandQueueType(commandQueueType), CommandQueueIndex(commandQueueIndex)
        {
        }
        RenderGraphDetectedQueue() noexcept = default;

        ECommandQueueType CommandQueueType{ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL};
        u8 CommandQueueIndex{0};

        FORCEINLINE auto IsCompetent() const noexcept { return CommandQueueType == ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL; }
        FORCEINLINE bool operator==(const RenderGraphDetectedQueue& other) const noexcept
        {
            return std::tie(CommandQueueType, CommandQueueIndex) == std::tie(other.CommandQueueType, other.CommandQueueIndex);
        }
    };

    struct RenderGraphDetectedQueueEqual
    {
        FORCEINLINE bool operator()(const RenderGraphDetectedQueue& lhs, const RenderGraphDetectedQueue& rhs) const noexcept
        {
            return lhs == rhs;
        }
    };

    using RenderGraphSetupFunc   = std::function<void(RenderGraphResourceScheduler&)>;
    using RenderGraphExecuteFunc = std::function<void(const RenderGraphResourceScheduler&, const vk::CommandBuffer&)>;

    // RenderGraphResourcePool
    struct RenderGraphBufferHandle
    {
        u64 ID{0};
        ExtraBufferFlags BufferFlags{EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT};
    };

    using RGTextureHandle         = u64;
    using RGBufferHandle          = RenderGraphBufferHandle;
    using RGResourceHandleVariant = std::variant<RGTextureHandle, RGBufferHandle>;

    // RenderGraph
    using RGResourceID = u64;  // Unique resource ID

    struct RenderGraphStatistics
    {
        f32 BuildTime{0.0f};  // CPU build time(milliseconds).
        u32 BarrierBatchCount{0};
        u32 BarrierCount{0};
    };

    struct RenderGraphSubresourceID
    {
        RenderGraphSubresourceID(const std::string& resourceName, const RGResourceID& resourceID, const u16 resourceMipIndex,
                                 const u16 resourceLayerIndex) noexcept
            : ResourceName(resourceName), ResourceID(resourceID), ResourceMipIndex(resourceMipIndex), ResourceLayerIndex(resourceLayerIndex)
        {
        }
        ~RenderGraphSubresourceID() noexcept = default;

        FORCEINLINE bool operator==(const RenderGraphSubresourceID& other) const noexcept
        {
            return std::tie(ResourceName, ResourceID, ResourceMipIndex, ResourceLayerIndex) ==
                   std::tie(other.ResourceName, other.ResourceID, other.ResourceMipIndex, other.ResourceLayerIndex);
        }

        std::string ResourceName{s_DEFAULT_STRING};
        RGResourceID ResourceID{};
        u16 ResourceMipIndex{};  // Up to 65k resolution images.
        // NOTE: In future if I'll need case of writing into multiple layers of texture, I'll add kind of LayerSet.
        u16 ResourceLayerIndex{};  // Up to 65k layers. (actual HW limit is 2048 on RTX 4090)
    };

}  // namespace Radiant

template <> struct ankerl::unordered_dense::hash<Radiant::RenderGraphSubresourceID>
{
    using is_avalanching = void;

    [[nodiscard]] auto operator()(const Radiant::RenderGraphSubresourceID& x) const noexcept -> std::uint64_t
    {
        return detail::wyhash::hash(x.ResourceID) + detail::wyhash::hash(x.ResourceLayerIndex) + detail::wyhash::hash(x.ResourceMipIndex);
    }
};

template <> struct ankerl::unordered_dense::hash<Radiant::RenderGraphDetectedQueue>
{
    using is_avalanching = void;

    [[nodiscard]] auto operator()(const Radiant::RenderGraphDetectedQueue& x) const noexcept -> std::uint64_t
    {
        const auto commandQueueTypeHash = detail::wyhash::hash((std::uint64_t)x.CommandQueueType);
        const auto commandQueueIndex    = detail::wyhash::hash(x.CommandQueueIndex);
        return detail::wyhash::hash(commandQueueTypeHash + commandQueueIndex);
    }
};
