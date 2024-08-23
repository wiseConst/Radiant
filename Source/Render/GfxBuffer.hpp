#pragma once

#include <Render/CoreDefines.hpp>
#include <Render/GfxDevice.hpp>

#define VK_NO_PROTOTYPES
#include <vk_mem_alloc.h>

namespace Radiant
{

    struct GfxBufferDescription
    {
        GfxBufferDescription(const u64 capacity, const u64 elementSize, const vk::BufferUsageFlags usageFlags,
                             const ExtraBufferFlags extraFlags, const bool bControlledByRenderGraph = false) noexcept
            : Capacity(capacity), ElementSize(elementSize), UsageFlags(usageFlags), ExtraFlags(extraFlags),
              bControlledByRenderGraph(bControlledByRenderGraph)
        {
            if ((ExtraFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_ADDRESSABLE) == EExtraBufferFlag::EXTRA_BUFFER_FLAG_ADDRESSABLE)
            {
                UsageFlags |= vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst;
            }

            if ((ExtraFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_HOST) == EExtraBufferFlag::EXTRA_BUFFER_FLAG_HOST)
            {
                UsageFlags |= vk::BufferUsageFlagBits::eTransferSrc;
            }
        }
        constexpr GfxBufferDescription() noexcept =
            default;  // NOTE: NEVER USE IT, IT'S NOT DELETED ONLY FOR COMPATIBILITY WITH maps/other containers!
        ~GfxBufferDescription() noexcept = default;

        u64 Capacity{};
        u64 ElementSize{};
        vk::BufferUsageFlags UsageFlags;
        ExtraBufferFlags ExtraFlags{};
        bool bControlledByRenderGraph{false};

        // NOTE: We don't care about capacity cuz we can resize wherever we want.
        bool operator!=(const GfxBufferDescription& other) const noexcept
        {
            return std::tie(UsageFlags, ExtraFlags, bControlledByRenderGraph) !=
                   std::tie(other.UsageFlags, other.ExtraFlags, other.bControlledByRenderGraph);
        }
    };

    class GfxBuffer final : private Uncopyable, private Unmovable
    {
      public:
        GfxBuffer(const Unique<GfxDevice>& device, const GfxBufferDescription& bufferDesc) noexcept
            : m_Device(device), m_Description(bufferDesc)
        {
            RDNT_ASSERT(m_Description.ExtraFlags > 0, "Unknown extra buffer usage flags!");

            Invalidate();
        }
        ~GfxBuffer() noexcept { Destroy(); }

        void RenderGraph_Finalize(VmaAllocation& allocation) noexcept;
        operator vk::Buffer&() noexcept { return m_Handle; }

        NODISCARD FORCEINLINE const auto& GetDescription() const noexcept { return m_Description; }
        NODISCARD FORCEINLINE const auto& GetMemorySize() const noexcept { return m_MemorySize; }

        NODISCARD FORCEINLINE const vk::DeviceAddress& GetBDA() const noexcept
        {
            RDNT_ASSERT(m_BDA.has_value(), "BDA is invalid!");
            return m_BDA.value();
        }

        void SetData(const void* data, const std::size_t dataSize) noexcept
        {
            if (!m_Mapped) return;

            if (m_Description.Capacity < dataSize) Resize(dataSize);
            std::memcpy(m_Mapped, data, dataSize);
        }

        bool Resize(const u64 newCapacity, const u64 newElementSize = std::numeric_limits<u64>::max()) noexcept
        {
            if (newCapacity == m_Description.Capacity && newElementSize == m_Description.ElementSize) return false;
            if (newElementSize != std::numeric_limits<u64>::max()) m_Description.ElementSize = newElementSize;

            m_Description.Capacity = newCapacity;
            Destroy();
            Invalidate();
            return true;
        }

        NODISCARD u64 GetElementCount() const noexcept
        {
            RDNT_ASSERT(m_Description.ElementSize > 0, "Division by zero!");
            return m_Description.Capacity / m_Description.ElementSize;
        }

      private:
        const Unique<GfxDevice>& m_Device;
        GfxBufferDescription m_Description{};

        vk::Buffer m_Handle{nullptr};
        VmaAllocation m_Allocation{};
        vk::DeviceSize m_MemorySize{0};
        std::optional<vk::DeviceAddress> m_BDA{std::nullopt};
        void* m_Mapped{nullptr};

        constexpr GfxBuffer() noexcept = delete;
        void Invalidate() noexcept;
        void Destroy() noexcept;
    };

}  // namespace Radiant
