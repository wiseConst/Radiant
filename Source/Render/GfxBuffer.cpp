#include <pch.h>
#include "GfxBuffer.hpp"

#include <Render/GfxContext.hpp>

namespace Radiant
{
    void GfxBuffer::Invalidate() noexcept
    {
        m_Device->AllocateBuffer(m_Description.ExtraFlags,
                                 vk::BufferCreateInfo()
                                     .setSharingMode(vk::SharingMode::eExclusive)
                                     .setUsage(m_Description.UsageFlags)
                                     .setSize(m_Description.Capacity),
                                 (VkBuffer&)m_Handle, m_Allocation);

        if ((m_Description.ExtraFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_ADDRESSABLE) == EExtraBufferFlag::EXTRA_BUFFER_FLAG_ADDRESSABLE)
            m_BDA = m_Device->GetLogicalDevice()->getBufferAddress(vk::BufferDeviceAddressInfo().setBuffer(m_Handle));

        if ((m_Description.ExtraFlags & EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED) == EExtraBufferFlag::EXTRA_BUFFER_FLAG_MAPPED)
            m_Mapped = m_Device->Map(m_Allocation);
    }

    void GfxBuffer::Destroy() noexcept
    {
        if (m_Mapped)
        {
            m_Device->Unmap(m_Allocation);
            m_Mapped = nullptr;
        }
        m_BDA = std::nullopt;

        m_Device->PushObjectToDelete(std::move(m_Handle), std::move(m_Allocation));
    }

}  // namespace Radiant
