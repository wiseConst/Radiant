#include <pch.h>
#include "GfxBuffer.hpp"

#include <Render/GfxContext.hpp>

namespace Radiant
{
    void GfxBuffer::Invalidate() noexcept
    {
        const auto bufferCI = vk::BufferCreateInfo()
                                  .setSharingMode(vk::SharingMode::eExclusive)
                                  .setUsage(m_Description.UsageFlags)
                                  .setSize(m_Description.Capacity);
        m_Device->AllocateBuffer(m_Description.ExtraBufferFlag, bufferCI, *(VkBuffer*)&m_Handle, m_Allocation);

        m_BDA = m_Device->GetLogicalDevice()->getBufferAddress(vk::BufferDeviceAddressInfo().setBuffer(m_Handle));
    }

    void GfxBuffer::Destroy() noexcept
    {
        m_BDA = std::nullopt;

        m_Device->PushObjectToDelete(
            [movedHandle = std::move(m_Handle), movedAllocation = std::move(m_Allocation)]()
            { GfxContext::Get().GetDevice()->DeallocateBuffer(*(VkBuffer*)&movedHandle, *(VmaAllocation*)&movedAllocation); });
    }

}  // namespace Radiant
