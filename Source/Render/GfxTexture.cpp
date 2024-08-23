#include <pch.h>
#include "GfxTexture.hpp"

#include <Render/GfxContext.hpp>
#include <Render/GfxDevice.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Radiant
{

    namespace GfxTextureUtils
    {
        void* LoadImage(const std::string_view& imagePath, i32& width, i32& height, i32& channels, const i32 requestedChannels) noexcept
        {
            RDNT_ASSERT(!imagePath.empty(), "Invalid image path!");

            void* imageData = stbi_load(imagePath.data(), &width, &height, &channels, requestedChannels);
            RDNT_ASSERT(imageData, "Failed to load image data!");

            if (channels != 4)
            {
                //   LOG_INFO("Overwriting loaded image's channels to 4! Previous: {}", channels);
                channels = 4;
            }

            return imageData;
        }

        void* LoadImage(const void* rawImageData, const std::size_t rawImageDataSize, i32& width, i32& height, i32& channels,
                        const i32 requestedChannels) noexcept
        {
            RDNT_ASSERT(rawImageData && rawImageDataSize > 0, "Invalid raw image data or size!");

            void* imageData = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(rawImageData), rawImageDataSize, &width, &height,
                                                    &channels, requestedChannels);
            RDNT_ASSERT(imageData, "Failed to load image data!");

            if (channels != 4)
            {
                LOG_INFO("Overwriting loaded image's channels to 4! Previous: {}", channels);
                channels = 4;
            }

            return imageData;
        }

        void UnloadImage(void* imageData) noexcept
        {
            stbi_image_free(imageData);
        }

        u32 GetMipLevelCount(const u32 width, const u32 height) noexcept
        {
            return static_cast<u32>(std::floor(std::log2(std::max(width, height)))) + 1;  // NOTE: +1 for base mip level
        }

    }  // namespace GfxTextureUtils

    void GfxTexture::Invalidate() noexcept
    {
        const auto mipLevelCount = GfxTextureUtils::GetMipLevelCount(m_Description.Dimensions.x, m_Description.Dimensions.y);

        const auto imageCI = vk::ImageCreateInfo()
                                 .setInitialLayout(vk::ImageLayout::eUndefined)
                                 .setArrayLayers(m_Description.LayerCount)
                                 .setImageType(m_Description.Type)
                                 .setSamples(m_Description.Samples)
                                 .setExtent(vk::Extent3D()
                                                .setWidth(m_Description.Dimensions.x)
                                                .setHeight(m_Description.Dimensions.y)
                                                .setDepth(m_Description.Dimensions.z))
                                 .setFormat(m_Description.Format)
                                 .setMipLevels(m_Description.bGenerateMips || m_Description.bExposeMips ? mipLevelCount : 1)
                                 .setTiling(vk::ImageTiling::eOptimal)
                                 .setSharingMode(vk::SharingMode::eExclusive)
                                 .setUsage(m_Description.UsageFlags);

        if (m_Description.bControlledByRenderGraph)
        {
            m_Image = m_Device->GetLogicalDevice()->createImage(imageCI);
            return;
        }
        else
        {
            m_Device->AllocateTexture(imageCI, (VkImage&)m_Image, m_Allocation);
        }

        CreateMipChainAndSubmitToBindlessPool();
    }

    void GfxTexture::CreateMipChainAndSubmitToBindlessPool() noexcept
    {
        const auto mipLevelCount = GfxTextureUtils::GetMipLevelCount(m_Description.Dimensions.x, m_Description.Dimensions.y);

        m_MipChain.resize(m_Description.bExposeMips ? mipLevelCount : 1);
        for (u32 baseMipLevel{}; baseMipLevel < m_MipChain.size(); ++baseMipLevel)
        {
            m_MipChain[baseMipLevel].ImageView = m_Device->GetLogicalDevice()->createImageViewUnique(
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
                                             .setLevelCount(m_Description.bGenerateMips ? mipLevelCount : 1)));

            {
                auto executionContext = GfxContext::Get().CreateImmediateExecuteContext(ECommandBufferType::COMMAND_BUFFER_TYPE_GENERAL);
                executionContext.CommandBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

                executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                    vk::ImageMemoryBarrier2()
                        .setImage(m_Image)
                        .setSubresourceRange(vk::ImageSubresourceRange()
                                                 .setBaseArrayLayer(0)
                                                 .setBaseMipLevel(baseMipLevel)
                                                 .setLevelCount(1)
                                                 .setLayerCount(m_Description.LayerCount)
                                                 .setAspectMask(IsDepthFormat(m_Description.Format) ? vk::ImageAspectFlagBits::eDepth
                                                                                                    : vk::ImageAspectFlagBits::eColor))
                        .setOldLayout(vk::ImageLayout::eUndefined)
                        .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                        .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                        .setDstAccessMask(vk::AccessFlagBits2::eShaderRead)
                        .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader)));

                executionContext.CommandBuffer.end();

                {
                    std::scoped_lock lock(GfxContext::Get().GetMutex());  // Synchronizing access to single queue
                    executionContext.Queue.submit(vk::SubmitInfo().setCommandBuffers(executionContext.CommandBuffer));
                    executionContext.Queue.waitIdle();
                }

                GfxContext::Get().PushBindlessThing(vk::DescriptorImageInfo()
                                                        .setImageView(*m_MipChain[baseMipLevel].ImageView)
                                                        .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                                                        .setSampler(m_Description.SamplerCreateInfo.has_value()
                                                                        ? m_Device->GetSampler(*m_Description.SamplerCreateInfo)
                                                                        : m_Device->GetDefaultSampler()),
                                                    m_MipChain[baseMipLevel].BindlessTextureID, Shaders::s_BINDLESS_TEXTURE_BINDING);
            }

            if ((m_Description.UsageFlags & vk::ImageUsageFlagBits::eStorage) == vk::ImageUsageFlagBits::eStorage)
            {
                auto executionContext = GfxContext::Get().CreateImmediateExecuteContext(ECommandBufferType::COMMAND_BUFFER_TYPE_GENERAL);
                executionContext.CommandBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

                executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                    vk::ImageMemoryBarrier2()
                        .setImage(m_Image)
                        .setSubresourceRange(vk::ImageSubresourceRange()
                                                 .setBaseArrayLayer(0)
                                                 .setBaseMipLevel(baseMipLevel)
                                                 .setLevelCount(1)
                                                 .setLayerCount(m_Description.LayerCount)
                                                 .setAspectMask(IsDepthFormat(m_Description.Format) ? vk::ImageAspectFlagBits::eDepth
                                                                                                    : vk::ImageAspectFlagBits::eColor))
                        .setOldLayout(vk::ImageLayout::eUndefined)
                        .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                        .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                        .setNewLayout(vk::ImageLayout::eGeneral)
                        .setDstAccessMask(vk::AccessFlagBits2::eShaderRead)
                        .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader)));

                executionContext.CommandBuffer.end();

                {
                    std::scoped_lock lock(GfxContext::Get().GetMutex());  // Synchronizing access to single queue
                    executionContext.Queue.submit(vk::SubmitInfo().setCommandBuffers(executionContext.CommandBuffer));
                    executionContext.Queue.waitIdle();
                }

                GfxContext::Get().PushBindlessThing(
                    vk::DescriptorImageInfo().setImageView(*m_MipChain[baseMipLevel].ImageView).setImageLayout(vk::ImageLayout::eGeneral),
                    m_MipChain[baseMipLevel].BindlessImageID, Shaders::s_BINDLESS_IMAGE_BINDING);
            }
        }
    }

    void GfxTexture::RenderGraph_Finalize() noexcept
    {
        CreateMipChainAndSubmitToBindlessPool();
    }

    void GfxTexture::GenerateMipMaps(const vk::CommandBuffer& cmd) const noexcept
    {
        RDNT_ASSERT(m_Description.bGenerateMips, "bGenerateMips is not specified!");

        const auto formatProps = m_Device->GetPhysicalDevice().getFormatProperties(m_Description.Format);
        RDNT_ASSERT((formatProps.optimalTilingFeatures & (vk::FormatFeatureFlagBits::eSampledImageFilterLinear |
                                                          vk::FormatFeatureFlagBits::eBlitSrc | vk::FormatFeatureFlagBits::eBlitDst)) ==
                        (vk::FormatFeatureFlagBits::eSampledImageFilterLinear | vk::FormatFeatureFlagBits::eBlitSrc |
                         vk::FormatFeatureFlagBits::eBlitDst),
                    "Texture image format doesn't support linear blitting!");

        const auto aspectMask = IsDepthFormat(m_Description.Format) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
        auto imageMemoryBarrier =
            vk::ImageMemoryBarrier2().setImage(m_Image).setSubresourceRange(vk::ImageSubresourceRange()
                                                                                .setBaseArrayLayer(0)
                                                                                .setLevelCount(1)
                                                                                .setLayerCount(m_Description.LayerCount)
                                                                                .setAspectMask(aspectMask));
        const auto mipLevelCount = GfxTextureUtils::GetMipLevelCount(m_Description.Dimensions.x, m_Description.Dimensions.y);

        uint32_t mipWidth = m_Description.Dimensions.x, mipHeight = m_Description.Dimensions.y;
        for (u32 baseMipLevel{1}; baseMipLevel < mipLevelCount; ++baseMipLevel)
        {
            imageMemoryBarrier.subresourceRange.baseMipLevel = baseMipLevel - 1;
            imageMemoryBarrier.oldLayout                     = vk::ImageLayout::eTransferDstOptimal;
            imageMemoryBarrier.srcAccessMask                 = vk::AccessFlagBits2::eTransferWrite;
            imageMemoryBarrier.newLayout                     = vk::ImageLayout::eTransferSrcOptimal;
            imageMemoryBarrier.dstAccessMask                 = vk::AccessFlagBits2::eTransferRead;
            imageMemoryBarrier.srcStageMask                  = vk::PipelineStageFlagBits2::eAllTransfer;
            imageMemoryBarrier.dstStageMask                  = vk::PipelineStageFlagBits2::eBlit;
            cmd.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(imageMemoryBarrier));

            const auto previousMipSubresourceLayers = vk::ImageSubresourceLayers()
                                                          .setAspectMask(aspectMask)
                                                          .setLayerCount(m_Description.LayerCount)
                                                          .setBaseArrayLayer(0)
                                                          .setMipLevel(baseMipLevel - 1);
            const auto currentMipSubresourceLayers = vk::ImageSubresourceLayers()
                                                         .setAspectMask(aspectMask)
                                                         .setLayerCount(m_Description.LayerCount)
                                                         .setBaseArrayLayer(0)
                                                         .setMipLevel(baseMipLevel);

            cmd.blitImage2(vk::BlitImageInfo2()
                               .setFilter(vk::Filter::eLinear)
                               .setSrcImage(m_Image)
                               .setSrcImageLayout(vk::ImageLayout::eTransferSrcOptimal)
                               .setDstImage(m_Image)
                               .setDstImageLayout(vk::ImageLayout::eTransferDstOptimal)
                               .setRegions(vk::ImageBlit2()
                                               .setSrcSubresource(previousMipSubresourceLayers)
                                               .setSrcOffsets({vk::Offset3D(), vk::Offset3D(static_cast<int32_t>(mipWidth),
                                                                                            static_cast<int32_t>(mipHeight), 1)})
                                               .setDstSubresource(currentMipSubresourceLayers)
                                               .setDstOffsets({vk::Offset3D(),
                                                               vk::Offset3D(static_cast<int32_t>(mipWidth > 1 ? mipWidth / 2 : 1),
                                                                            static_cast<int32_t>(mipHeight > 1 ? mipHeight / 2 : 1), 1)})));

            imageMemoryBarrier.oldLayout     = vk::ImageLayout::eTransferSrcOptimal;
            imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits2::eTransferRead;
            imageMemoryBarrier.srcStageMask  = vk::PipelineStageFlagBits2::eBlit;
            imageMemoryBarrier.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;
            imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
            imageMemoryBarrier.dstStageMask  = vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader;
            cmd.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(imageMemoryBarrier));

            mipWidth  = mipWidth > 1 ? mipWidth / 2 : 1;
            mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
        }

        // NOTE: Last mip level isn't covered by being the blit SRC!
        imageMemoryBarrier.subresourceRange.baseMipLevel = mipLevelCount - 1;
        imageMemoryBarrier.oldLayout                     = vk::ImageLayout::eTransferDstOptimal;
        imageMemoryBarrier.srcAccessMask                 = vk::AccessFlagBits2::eTransferWrite;
        imageMemoryBarrier.srcStageMask                  = vk::PipelineStageFlagBits2::eAllTransfer;
        imageMemoryBarrier.newLayout                     = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageMemoryBarrier.dstAccessMask                 = vk::AccessFlagBits2::eShaderSampledRead;
        imageMemoryBarrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader;
        cmd.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(imageMemoryBarrier));
    }

    bool GfxTexture::Resize(const glm::uvec3& dimensions) noexcept
    {
        if (m_Description.Dimensions == dimensions) return false;
        m_Description.Dimensions = dimensions;

        Destroy();
        Invalidate();
        return true;
    }

    void GfxTexture::Destroy() noexcept
    {
        m_Device->PushObjectToDelete(
            [bControlledByRenderGraph = m_Description.bControlledByRenderGraph, movedMipChain = std::move(m_MipChain),
             movedImage = std::move(m_Image), movedAllocation = std::move(m_Allocation)]() noexcept
            {
                for (auto& mipInfo : movedMipChain)
                {
                    auto& nonConstMipInfo = const_cast<MipInfo&>(mipInfo);
                    GfxContext::Get().PopBindlessThing(nonConstMipInfo.BindlessTextureID, Shaders::s_BINDLESS_TEXTURE_BINDING);
                    if (mipInfo.BindlessImageID.has_value())
                        GfxContext::Get().PopBindlessThing(nonConstMipInfo.BindlessImageID, Shaders::s_BINDLESS_IMAGE_BINDING);
                }

                if (bControlledByRenderGraph)
                {
                    GfxContext::Get().GetDevice()->GetLogicalDevice()->destroyImage(movedImage);
                }
                else
                {
                    GfxContext::Get().GetDevice()->DeallocateTexture((VkImage&)movedImage, (VmaAllocation&)movedAllocation);
                }
            });
    }

}  // namespace Radiant
