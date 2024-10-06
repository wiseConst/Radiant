#include <pch.hpp>
#include "GfxBuffer.hpp"

#include <Render/GfxContext.hpp>
#include <Render/GfxDevice.hpp>

namespace Radiant
{
    void GfxBuffer::RG_Finalize(VmaAllocation& allocation) noexcept
    {
        if (m_Description.ExtraFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_ADDRESSABLE_BIT)
            m_BDA = m_Device->GetLogicalDevice()->getBufferAddress(vk::BufferDeviceAddressInfo().setBuffer(*m_Handle));

        if (m_Description.ExtraFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT) m_Mapped = m_Device->Map(allocation);

        // NOTE: Storing allocation, but not relying on it!
        m_Allocation = allocation;
    }

    void GfxBuffer::Invalidate() noexcept
    {
        Destroy();

        const auto bufferCI = vk::BufferCreateInfo()
                                  .setSharingMode(vk::SharingMode::eExclusive)
                                  .setUsage(m_Description.UsageFlags)
                                  .setSize(m_Description.Capacity);

        if ((m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_RENDER_GRAPH_MEMORY_CONTROLLED_BIT) &&
            !(m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_FORCE_NO_RESOURCE_MEMORY_ALIASING_BIT))
        {
            m_Handle = m_Device->GetLogicalDevice()->createBuffer(bufferCI);
            return;
        }

        m_Handle = vk::Buffer();
        m_Device->AllocateBuffer(m_Description.ExtraFlags, bufferCI, (VkBuffer&)*m_Handle, m_Allocation);

        if (m_Description.ExtraFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_ADDRESSABLE_BIT)
            m_BDA = m_Device->GetLogicalDevice()->getBufferAddress(vk::BufferDeviceAddressInfo().setBuffer(*m_Handle));

        if (m_Description.ExtraFlags & EExtraBufferFlagBits::EXTRA_BUFFER_FLAG_HOST_BIT) m_Mapped = m_Device->Map(m_Allocation);
    }

    void GfxBuffer::Destroy() noexcept
    {
        if (!m_Handle.has_value()) return;

        if (m_Mapped)
        {
            m_Device->Unmap(m_Allocation);
            m_Mapped = nullptr;
        }
        m_BDA = std::nullopt;

        if ((m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_RENDER_GRAPH_MEMORY_CONTROLLED_BIT) &&
            !(m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_FORCE_NO_RESOURCE_MEMORY_ALIASING_BIT))
        {
            m_Device->PushObjectToDelete([movedHandle = std::move(*m_Handle)]() noexcept
                                         { GfxContext::Get().GetDevice()->GetLogicalDevice()->destroyBuffer(movedHandle); });
        }
        else
            m_Device->PushObjectToDelete(std::move(*m_Handle), std::move(m_Allocation));

        m_Handle = std::nullopt;
    }

}  // namespace Radiant
