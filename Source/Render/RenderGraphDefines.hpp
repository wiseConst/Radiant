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

    // RenderGraphPass
    enum class ERenderGraphPassType : u8
    {
        RENDER_GRAPH_PASS_TYPE_COMPUTE,
        RENDER_GRAPH_PASS_TYPE_TRANSFER,
        RENDER_GRAPH_PASS_TYPE_GRAPHICS,

        // TODO: Multiple queue submission
        RENDER_GRAPH_PASS_TYPE_ASYNC_COMPUTE,
        RENDER_GRAPH_PASS_TYPE_DEDICATED_TRANSFER,
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
        RenderGraphSubresourceID(const std::string& resourceName, const RGResourceID& resourceID, const u32 subresourceIndex) noexcept
            : ResourceName(resourceName), ResourceID(resourceID), SubresourceIndex(subresourceIndex)
        {
        }
        ~RenderGraphSubresourceID() noexcept = default;

        FORCEINLINE bool operator==(const RenderGraphSubresourceID& other) const noexcept
        {
            return std::tie(ResourceName, ResourceID, SubresourceIndex) ==
                   std::tie(other.ResourceName, other.ResourceID, other.SubresourceIndex);
        }

        std::string ResourceName{s_DEFAULT_STRING};
        RGResourceID ResourceID{};
        u32 SubresourceIndex{0};
    };

}  // namespace Radiant
