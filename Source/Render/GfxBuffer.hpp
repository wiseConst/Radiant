#pragma once

#include <Render/CoreDefines.hpp>
#include <Render/GfxDevice.hpp>

#define VK_NO_PROTOTYPES
#include "vma/vk_mem_alloc.h"

namespace Radiant
{

    struct GfxBufferDescription
    {
        std::size_t Capacity{};
        std::size_t ElementSize{};
        vk::BufferUsageFlags UsageFlags;
        EExtraBufferFlag ExtraBufferFlag{};
    };

    class GfxBuffer final : private Uncopyable, private Unmovable
    {
      public:
        GfxBuffer(const Unique<GfxDevice>& device, const GfxBufferDescription& bufferDesc) noexcept
            : m_Device(device), m_Description(bufferDesc)
        {
            if (m_Description.ExtraBufferFlag == EExtraBufferFlag::EXTRA_BUFFER_FLAG_DEVICE_LOCAL)
            {
                m_Description.UsageFlags |= vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst;
            }
            else if (m_Description.ExtraBufferFlag == EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED)
            {
                m_Description.UsageFlags |= vk::BufferUsageFlagBits::eTransferSrc;
            }
            else
                RDNT_ASSERT(false, "Unknown extra buffer flag! THIS SHOULDN'T HAPPEN!");

            Invalidate();
        }
        ~GfxBuffer() noexcept { Destroy(); }

        NODISCARD FORCEINLINE const vk::DeviceAddress& GetBDA() const noexcept
        {
            RDNT_ASSERT(m_BDA.has_value(), "BDA is invalid!");
            return m_BDA.value();
        }

        NODISCARD std::size_t GetElementCount() const noexcept
        {
            RDNT_ASSERT(m_Description.ElementSize > 0, "Division by zero!");
            return m_Description.Capacity / m_Description.ElementSize;
        }

        operator const vk::Buffer&() const noexcept { return m_Handle; }

      private:
        const Unique<GfxDevice>& m_Device;
        GfxBufferDescription m_Description{};
        vk::Buffer m_Handle{nullptr};
        VmaAllocation m_Allocation{};
        std::optional<vk::DeviceAddress> m_BDA{std::nullopt};
        void* m_Mapped{nullptr};

        constexpr GfxBuffer() noexcept = delete;
        void Invalidate() noexcept;
        void Destroy() noexcept;
    };

}  // namespace Radiant
