#include <pch.h>
#include "GfxTexture.hpp"

#include <Render/GfxContext.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Radiant
{
    void GfxTexture::Invalidate() noexcept
    {
        const auto imageCI = vk::ImageCreateInfo()
                                 .setInitialLayout(vk::ImageLayout::eUndefined)
                                 .setArrayLayers(m_Description.LayerCount)
                                 .setImageType(m_Description.Type)
                                 .setExtent(vk::Extent3D()
                                                .setWidth(m_Description.Dimensions.x)
                                                .setHeight(m_Description.Dimensions.y)
                                                .setDepth(m_Description.Dimensions.z))
                                 .setFormat(m_Description.Format)
                                 .setMipLevels(1)
                                 .setTiling(vk::ImageTiling::eOptimal)
                                 .setUsage(m_Description.UsageFlags)
                                 .setSharingMode(vk::SharingMode::eExclusive);

        m_Device->AllocateTexture(imageCI, *(VkImage*)&m_Image, m_Allocation);

        // Base mip level
        {
            const auto imageViewCI =
                vk::ImageViewCreateInfo()
                    .setComponents(vk::ComponentMapping()
                                       .setR(vk::ComponentSwizzle::eR)
                                       .setG(vk::ComponentSwizzle::eG)
                                       .setB(vk::ComponentSwizzle::eB)
                                       .setA(vk::ComponentSwizzle::eA))
                    .setFormat(m_Description.Format)
                    .setImage(m_Image)
                    .setViewType(vk::ImageViewType::e2D)
                    .setSubresourceRange(vk::ImageSubresourceRange()
                                             .setAspectMask(IsDepthFormat(m_Description.Format) ? vk::ImageAspectFlagBits::eDepth
                                                                                                : vk::ImageAspectFlagBits::eColor)
                                             .setBaseArrayLayer(0)
                                             .setBaseMipLevel(0)
                                             .setLayerCount(m_Description.LayerCount)
                                             .setLevelCount(1));
            m_MipChain.emplace_back(m_Device->GetLogicalDevice()->createImageViewUnique(imageViewCI));
        }

        // Other(optional mips)
        if (m_Description.bExposeMips)
        {
            const auto mipLevelCount = std::floor(
                std::log2(std::max(m_Description.Dimensions.x, m_Description.Dimensions.y)));  // NOTE: This doesn't include base mip level!

            for (std::uint32_t baseMipLevel{1}; baseMipLevel <= mipLevelCount; ++baseMipLevel)
            {
                const auto imageViewCI =
                    vk::ImageViewCreateInfo()
                        .setComponents(vk::ComponentMapping()
                                           .setR(vk::ComponentSwizzle::eR)
                                           .setG(vk::ComponentSwizzle::eG)
                                           .setB(vk::ComponentSwizzle::eB)
                                           .setA(vk::ComponentSwizzle::eA))
                        .setFormat(m_Description.Format)
                        .setImage(m_Image)
                        .setViewType(vk::ImageViewType::e2D)
                        .setSubresourceRange(vk::ImageSubresourceRange()
                                                 .setAspectMask(IsDepthFormat(m_Description.Format) ? vk::ImageAspectFlagBits::eDepth
                                                                                                    : vk::ImageAspectFlagBits::eColor)
                                                 .setBaseArrayLayer(0)
                                                 .setBaseMipLevel(baseMipLevel)
                                                 .setLayerCount(m_Description.LayerCount)
                                                 .setLevelCount(1));
                m_MipChain.emplace_back(m_Device->GetLogicalDevice()->createImageViewUnique(imageViewCI));
            }
        }
    }

    void GfxTexture::Resize(const glm::uvec3& dimensions) noexcept
    {
        if (m_Description.Dimensions == dimensions) return;

        m_Description.Dimensions = dimensions;

        m_Device->PushObjectToDelete(
            [movedMipChain = std::move(m_MipChain), movedImage = std::move(m_Image), movedAllocation = std::move(m_Allocation)]()
            { GfxContext::Get().GetDevice()->DeallocateTexture(*(VkImage*)&movedImage, *(VmaAllocation*)&movedAllocation); });

        Invalidate();
    }

    void GfxTexture::Destroy() noexcept
    {
        m_Device->PushObjectToDelete(
            [movedMipChain = std::move(m_MipChain), movedImage = std::move(m_Image), movedAllocation = std::move(m_Allocation)]()
            { GfxContext::Get().GetDevice()->DeallocateTexture(*(VkImage*)&movedImage, *(VmaAllocation*)&movedAllocation); });
    }

}  // namespace Radiant
