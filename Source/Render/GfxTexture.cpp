#include "GfxTexture.hpp"

#include <Render/GfxContext.hpp>
#include <Render/GfxDevice.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Radiant
{

    namespace GfxTextureUtils
    {

        static constexpr const char* s_TextureCacheDir = "texture_cache/";

        NODISCARD static nvtt::Format VulkanFormatToNvttFormat(const vk::Format format) noexcept
        {
            switch (format)
            {
                case vk::Format::eR8G8B8Unorm: return nvtt::Format::Format_RGB;
                case vk::Format::eR8G8B8A8Unorm: return nvtt::Format::Format_RGBA;
                case vk::Format::eBc1RgbUnormBlock: return nvtt::Format::Format_BC1;
                case vk::Format::eBc1RgbaUnormBlock: return nvtt::Format::Format_BC1a;

                case vk::Format::eBc2UnormBlock: return nvtt::Format::Format_BC2;
                case vk::Format::eBc3UnormBlock: return nvtt::Format::Format_BC3;

                case vk::Format::eBc4UnormBlock: return nvtt::Format::Format_BC4;
                case vk::Format::eBc4SnormBlock: return nvtt::Format::Format_BC4S;

                case vk::Format::eBc5UnormBlock: return nvtt::Format::Format_BC5;
                case vk::Format::eBc5SnormBlock: return nvtt::Format::Format_BC5S;

                case vk::Format::eBc6HUfloatBlock: return nvtt::Format::Format_BC6U;
                case vk::Format::eBc6HSfloatBlock: return nvtt::Format::Format_BC6S;

                case vk::Format::eBc7UnormBlock: return nvtt::Format::Format_BC7;
            }

            RDNT_ASSERT(false, "Failed to determine NVTT compression format!");
        }

        void* LoadImage(const std::string_view& imagePath, i32& width, i32& height, i32& channels, const i32 requestedChannels,
                        const bool bFlipOnLoad) noexcept
        {
            RDNT_ASSERT(!imagePath.empty(), "Invalid image path!");

            if (bFlipOnLoad) stbi_set_flip_vertically_on_load(true);

            void* imageData{nullptr};
            if (stbi_is_hdr(imagePath.data()))
                imageData = stbi_loadf(imagePath.data(), &width, &height, &channels, requestedChannels);
            else
                imageData = stbi_load(imagePath.data(), &width, &height, &channels, requestedChannels);

            RDNT_ASSERT(imageData, "Failed to load image data!");
            if (requestedChannels > 0)
            {
                //   LOG_INFO("Overwriting loaded image's channels to 4! Previous: {}", channels);
                channels = requestedChannels;
            }

            if (bFlipOnLoad) stbi_set_flip_vertically_on_load(false);

            return imageData;
        }

        void* LoadImage(const void* rawImageData, const u64 rawImageDataSize, i32& width, i32& height, i32& channels,
                        const i32 requestedChannels, const bool bFlipOnLoad) noexcept
        {
            RDNT_ASSERT(rawImageData && rawImageDataSize > 0, "Invalid raw image data or size!");

            if (bFlipOnLoad) stbi_set_flip_vertically_on_load(true);

            void* imageData = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(rawImageData), rawImageDataSize, &width, &height,
                                                    &channels, requestedChannels);
            RDNT_ASSERT(imageData, "Failed to load image data!");

            if (channels != 4)
            {
                LOG_INFO("Overwriting loaded image's channels to 4! Previous: {}", channels);
                channels = 4;
            }

            if (bFlipOnLoad) stbi_set_flip_vertically_on_load(false);

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

        void TextureCompressor::PushTextureIntoBatchList(const std::string& texturePath, const vk::Format dstFormat) noexcept
        {
            RDNT_ASSERT(!texturePath.empty(), "Texture path is invalid!");
            if (IsCacheExist(texturePath, dstFormat)) return;

            m_TexturesToLoad[dstFormat].emplace_back(texturePath);
        }

        void TextureCompressor::CompressAndCache() noexcept
        {
            if (m_TexturesToLoad.empty()) return;
            if (!std::filesystem::exists(s_TextureCacheDir)) std::filesystem::create_directory(s_TextureCacheDir);

            // Create the compression context; enable CUDA compression, so that
            // CUDA-capable GPUs will use GPU acceleration for compression, with a
            // fallback on other GPUs for CPU compression.
            nvtt::Context context = {};
            context.enableCudaAcceleration(true);

            if (context.isCudaAccelerationEnabled())
                LOG_INFO("[TextureCompressor]: Enjoy the blazingly fast caching process with cuda!");
            else
                LOG_INFO("[TextureCompressor]: No CUDA for you. AMD card or old drivers?");

            static constexpr u64 batchSizeLimitBytes = 128 * 1024 * 1024;  // 128 MB
            u64 currentBatchSize                     = 0;
            u32 currentBatchCount{};

            for (const auto& [format, texturePaths] : m_TexturesToLoad)
            {
                nvtt::CompressionOptions compressionOptions = {};
                compressionOptions.setFormat(VulkanFormatToNvttFormat(format));

                constexpr nvtt::Quality compressionQuality{nvtt::Quality::Quality_Normal};
                compressionOptions.setQuality(compressionQuality);

                // NOTE: Storing array of unique ptrs since surface should be per mip per face.
                std::vector<Unique<nvtt::Surface>> surfaceList;
                surfaceList.reserve(texturePaths.size());

                std::vector<nvtt::OutputOptions> outputOptionList(texturePaths.size());
                std::vector<std::string> outputTexturePaths(texturePaths.size());
                std::vector<Unique<RadiantTextureFileWriter>> textureFileWriters(texturePaths.size());

                // NOTE: Currently hardcoded, will be extended as needed.
                constexpr i32 face = 0;

                u32 i{};
                while (i < texturePaths.size())
                {
                    currentBatchCount         = 0;
                    currentBatchSize          = 0;
                    nvtt::BatchList batchList = {};
                    for (; i < texturePaths.size(); ++i)
                    {
                        const auto& texturePath  = texturePaths[i];
                        const auto texturePathFS = std::filesystem::path(texturePath);
                        RDNT_ASSERT(std::filesystem::exists(texturePathFS), "Texture path: {}, doesn't exist!", texturePath);

                        const auto currentFileSizeBytes = std::filesystem::file_size(texturePathFS);
                        if (currentBatchSize + currentFileSizeBytes > batchSizeLimitBytes && currentBatchSize > 0) break;

                        auto& srcImage = *surfaceList.emplace_back(MakeUnique<nvtt::Surface>());
                        RDNT_ASSERT(srcImage.load(texturePath.data()), "Failed to load: {}", texturePath);

                        const auto dimensions = glm::uvec2(srcImage.width(), srcImage.height());
                        const u32 mipCount    = srcImage.countMipmaps();

                        outputTexturePaths[i] = DetermineTextureCachePath(texturePath, format);
                        textureFileWriters[i] = MakeUnique<RadiantTextureFileWriter>(outputTexturePaths[i], dimensions, mipCount);
                        outputOptionList[i].setOutputHandler(reinterpret_cast<nvtt::OutputHandler*>(textureFileWriters[i].get()));

                        batchList.Append(&srcImage, face, 0, &outputOptionList[i]);
                        for (u32 mip = 1; mip < mipCount; ++mip)
                        {
                            const auto& prevMippedImage = *surfaceList.back();
                            auto& mippedImage           = *surfaceList.emplace_back(MakeUnique<nvtt::Surface>(prevMippedImage));
                            batchList.Append(&mippedImage, face, mip, &outputOptionList[i]);

                            if (mip == mipCount - 1) break;

                            // Prepare the next mip:

                            // Convert to linear premultiplied alpha. Note that toLinearFromSrgb()
                            // will clamp HDR images; consider e.g. toLinear(2.2f) instead.
                            mippedImage.toLinearFromSrgb();
                            mippedImage.premultiplyAlpha();

                            // Resize the image to the next mipmap size.
                            // NVTT has several mipmapping filters; Box is the lowest-quality, but
                            // also the fastest to use.
                            mippedImage.buildNextMipmap(nvtt::MipmapFilter_Box);
                            // For general image resizing. use image.resize().

                            // Convert back to unpremultiplied sRGB.
                            mippedImage.demultiplyAlpha();
                            mippedImage.toSrgb();
                        }

                        currentBatchSize += currentFileSizeBytes;
                        ++currentBatchCount;
                    }

                    const auto compressionBeginTime = Timer::Now();

                    RDNT_ASSERT(context.compress(batchList, compressionOptions), "Failed to compress batch list!");

                    LOG_INFO("Time taken to compress {} [{}] textures: {} seconds", currentBatchCount, vk::to_string(format),
                             Timer::GetElapsedSecondsFromNow(compressionBeginTime));
                }
            }
        }

        NODISCARD std::vector<TextureCompressor::TextureInfo> TextureCompressor::LoadTextureCache(const std::string& texturePath,
                                                                                                  const vk::Format format) noexcept
        {
            RDNT_ASSERT(!texturePath.empty(), "Texture path is invalid!");
            const auto cachedTexturePath = DetermineTextureCachePath(texturePath, format);
            RDNT_ASSERT(std::filesystem::exists(cachedTexturePath), "Texture cache for: {}, doesn't exist!", texturePath);

            auto textureHeader = TextureCompressor::TextureHeader{};

            auto rawData = CoreUtils::LoadData<u8>(cachedTexturePath);
            std::memcpy(&textureHeader, rawData.data(), sizeof(textureHeader));

            u64 rawReadDataOffset{sizeof(textureHeader)};
            u32 mipWidth{textureHeader.Dimensions.x}, mipHeight{textureHeader.Dimensions.y};

            std::vector<TextureCompressor::TextureInfo> textureInfos(textureHeader.MipCount);
            for (u32 mip{}; mip < textureInfos.size(); ++mip)
            {
                textureInfos[mip].Dimensions = {mipWidth, mipHeight};

                u32 sizeBytes{};
                std::memcpy(&sizeBytes, rawData.data() + rawReadDataOffset, sizeof(sizeBytes));
                rawReadDataOffset += sizeof(sizeBytes);

                textureInfos[mip].Data.resize(sizeBytes);
                std::memcpy(textureInfos[mip].Data.data(), rawData.data() + rawReadDataOffset, sizeBytes);
                rawReadDataOffset += sizeBytes;

                mipWidth  = mipWidth > 1 ? mipWidth / 2 : 1;
                mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
            }

            return textureInfos;
        }

        NODISCARD std::vector<TextureCompressor::TextureInfo> TextureCompressor::CompressSingle(const std::string& texturePath,
                                                                                                const vk::Format format,
                                                                                                const bool bBuildMips,
                                                                                                const nvtt::Quality quality) noexcept
        {
            RDNT_ASSERT(!texturePath.empty(), "Texture path is invalid!");
            if (!std::filesystem::exists(s_TextureCacheDir)) std::filesystem::create_directory(s_TextureCacheDir);

            const auto textureCachePath = DetermineTextureCachePath(texturePath, format);
            if (std::filesystem::exists(textureCachePath))
            {
                LOG_INFO("Found texture cache for: {}", texturePath);
                return LoadTextureCache(texturePath, format);
            }

            // Create the compression context; enable CUDA compression, so that
            // CUDA-capable GPUs will use GPU acceleration for compression, with a
            // fallback on other GPUs for CPU compression.
            nvtt::Context context = {};
            context.enableCudaAcceleration(true);

            if (context.isCudaAccelerationEnabled())
                LOG_INFO("[TextureCompressor]: Enjoy the blazingly fast caching process with cuda!");
            else
                LOG_INFO("[TextureCompressor]: No CUDA for you. AMD card or old drivers?");

            nvtt::CompressionOptions compressionOptions = {};
            compressionOptions.setFormat(VulkanFormatToNvttFormat(format));
            compressionOptions.setQuality(quality);

            nvtt::Surface image;
            image.load(texturePath.data());

            const auto dimensions = glm::uvec2(image.width(), image.height());
            const u32 mipCount    = image.countMipmaps();

            const auto compressionBeginTime = Timer::Now();
            {
                // NOTE: Need braces since writer closes file in the d-ctor().

                RadiantTextureFileWriter writer(textureCachePath, dimensions, mipCount);

                nvtt::OutputOptions outputOptions{};
                outputOptions.setOutputHandler(reinterpret_cast<nvtt::OutputHandler*>(&writer));

                // NOTE: Currently hardcoded, will be extended as needed.
                constexpr i32 face = 0;

                for (u32 mip{}; mip < mipCount; ++mip)
                {
                    RDNT_ASSERT(context.compress(image, face, mip, compressionOptions, outputOptions),
                                "Failed to compress {}, mip: {}, face: {}", texturePath, mip, face);

                    if (mip == mipCount - 1 || !bBuildMips) break;

                    // Prepare the next mip:

                    // Convert to linear premultiplied alpha. Note that toLinearFromSrgb()
                    // will clamp HDR images; consider e.g. toLinear(2.2f) instead.
                    image.toLinearFromSrgb();
                    image.premultiplyAlpha();

                    // Resize the image to the next mipmap size.
                    // NVTT has several mipmapping filters; Box is the lowest-quality, but
                    // also the fastest to use.
                    image.buildNextMipmap(nvtt::MipmapFilter_Box);
                    // For general image resizing. use image.resize().

                    // Convert back to unpremultiplied sRGB.
                    image.demultiplyAlpha();
                    image.toSrgb();
                }
            }

            LOG_INFO("Time taken to compress texture {} with {} mips: {} seconds", texturePath, mipCount,
                     Timer::GetElapsedSecondsFromNow(compressionBeginTime));

            return LoadTextureCache(texturePath, format);
        }

        NODISCARD const std::string TextureCompressor::DetermineTextureCachePath(const std::string& texturePath,
                                                                                 const vk::Format format) noexcept
        {
            RDNT_ASSERT(!texturePath.empty(), "Texture path is invalid!");
            std::filesystem::path outputTextureName{s_TextureCacheDir};

            // Find the last occurrence of '/' in the path.
            const u64 lastSlashPosIndex = texturePath.find_last_of('/');

            // Find the position of the penultimate slash.
            const u64 penultimateSlashPosIndex = texturePath.find_last_of('/', lastSlashPosIndex - 1);

            // Extract the substring from the 2nd penultimate slash to the end.
            if (penultimateSlashPosIndex != std::string::npos)
                outputTextureName += texturePath.substr(penultimateSlashPosIndex + 1);
            else
                outputTextureName += texturePath;

            // Create the directories if they don't exist.
            std::filesystem::create_directories(std::filesystem::path(outputTextureName).parent_path());

            switch (format)
            {
                case vk::Format::eBc1RgbUnormBlock: outputTextureName.replace_extension(".bc1"); break;
                case vk::Format::eBc1RgbaUnormBlock: outputTextureName.replace_extension(".bc1a"); break;

                case vk::Format::eBc2UnormBlock: outputTextureName.replace_extension(".bc2"); break;
                case vk::Format::eBc3UnormBlock: outputTextureName.replace_extension(".bc3"); break;

                case vk::Format::eBc4UnormBlock: outputTextureName.replace_extension(".bc4"); break;
                case vk::Format::eBc4SnormBlock: outputTextureName.replace_extension(".bc4s"); break;

                case vk::Format::eBc5UnormBlock: outputTextureName.replace_extension(".bc5"); break;
                case vk::Format::eBc5SnormBlock: outputTextureName.replace_extension(".bc5s"); break;

                case vk::Format::eBc6HUfloatBlock: outputTextureName.replace_extension(".bc6u"); break;
                case vk::Format::eBc6HSfloatBlock: outputTextureName.replace_extension(".bc6s"); break;

                case vk::Format::eBc7UnormBlock: outputTextureName.replace_extension(".bc7"); break;
                default: RDNT_ASSERT(false, "Failed to determine NVTT compression format!");
            }

            return outputTextureName.string();
        }

        NODISCARD bool TextureCompressor::IsCacheExist(const std::string& texturePath, const vk::Format format) noexcept
        {
            const auto textureCachePath = std::filesystem::path(DetermineTextureCachePath(texturePath, format));
            return std::filesystem::exists(textureCachePath);
        }

    }  // namespace GfxTextureUtils

    void GfxTexture::Invalidate() noexcept
    {
        Destroy();

        const auto mipLevelCount = GetMipCount();
        const bool bExposeMips   = m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_EXPOSE_MIPS_BIT;
        const bool bCreateMips   = m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_CREATE_MIPS_BIT;

        const auto imageCI =
            vk::ImageCreateInfo()
                .setInitialLayout(vk::ImageLayout::eUndefined)
                .setArrayLayers(m_Description.LayerCount)
                .setImageType(m_Description.Type)
                .setSamples(m_Description.Samples)
                .setExtent(vk::Extent3D()
                               .setWidth(m_Description.Dimensions.x)
                               .setHeight(m_Description.Dimensions.y)
                               .setDepth(m_Description.Dimensions.z))
                .setFormat(m_Description.Format)
                .setMipLevels(bCreateMips || bExposeMips ? mipLevelCount : 1)
                .setTiling(vk::ImageTiling::eOptimal)
                .setSharingMode(vk::SharingMode::eExclusive)
                .setUsage(m_Description.UsageFlags)
                .setFlags(m_Description.LayerCount == 6 ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlagBits{});

        if ((m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_RENDER_GRAPH_MEMORY_CONTROLLED_BIT) &&
            !(m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_FORCE_NO_RESOURCE_MEMORY_ALIASING_BIT))
        {
            m_Image = m_Device->GetLogicalDevice()->createImage(imageCI);
            return;
        }

        m_Image = vk::Image();
        m_Device->AllocateTexture(imageCI, (VkImage&)*m_Image, m_Allocation);
        CreateMipChainAndSubmitToBindlessPool();
    }

    void GfxTexture::CreateMipChainAndSubmitToBindlessPool() noexcept
    {
        const auto mipLevelCount = GetMipCount();
        const bool bExposeMips   = m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_EXPOSE_MIPS_BIT;
        const bool bCreateMips   = m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_CREATE_MIPS_BIT;
        const bool bDontTouchSampledImageDescriptors =
            m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_DONT_TOUCH_SAMPLED_IMAGES_BIT;

        vk::ImageAspectFlags aspectMask{};
        if (IsDepthFormat(m_Description.Format))
            aspectMask = vk::ImageAspectFlagBits::eDepth;
        else
            aspectMask = vk::ImageAspectFlagBits::eColor;

        if (IsStencilFormat(m_Description.Format)) aspectMask |= vk::ImageAspectFlagBits::eStencil;

        m_MipChain.resize(bExposeMips ? mipLevelCount : 1);
        for (u32 baseMipLevel{}; baseMipLevel < m_MipChain.size(); ++baseMipLevel)
        {
            // NOTE: Base mip level 0 can contain all mips as well I guess.
            const auto currentMipCount         = bCreateMips && (!bExposeMips || baseMipLevel == 0) ? mipLevelCount : 1;
            m_MipChain[baseMipLevel].ImageView = m_Device->GetLogicalDevice()->createImageView(
                vk::ImageViewCreateInfo()
                    .setComponents(vk::ComponentMapping()
                                       .setR(vk::ComponentSwizzle::eR)
                                       .setG(vk::ComponentSwizzle::eG)
                                       .setB(vk::ComponentSwizzle::eB)
                                       .setA(vk::ComponentSwizzle::eA))
                    .setFormat(m_Description.Format)
                    .setImage(*m_Image)
                    .setViewType(m_Description.LayerCount == 1
                                     ? vk::ImageViewType::e2D
                                     : (m_Description.LayerCount == 6 ? vk::ImageViewType::eCube : vk::ImageViewType::e2DArray))
                    .setSubresourceRange(vk::ImageSubresourceRange()
                                             .setAspectMask(aspectMask)
                                             .setBaseArrayLayer(0)
                                             .setBaseMipLevel(baseMipLevel)
                                             .setLayerCount(m_Description.LayerCount)
                                             .setLevelCount(currentMipCount)));

            {
                auto executionContext = GfxContext::Get().CreateImmediateExecuteContext(ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL);
                executionContext.CommandBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

                executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                    vk::ImageMemoryBarrier2()
                        .setImage(*m_Image)
                        .setSubresourceRange(vk::ImageSubresourceRange()
                                                 .setBaseArrayLayer(0)
                                                 .setBaseMipLevel(baseMipLevel)
                                                 .setLevelCount(currentMipCount)
                                                 .setLayerCount(m_Description.LayerCount)
                                                 .setAspectMask(aspectMask))
                        .setOldLayout(vk::ImageLayout::eUndefined)
                        .setSrcAccessMask(vk::AccessFlagBits2::eNone)
                        .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
                        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                        .setDstAccessMask(vk::AccessFlagBits2::eShaderRead)
                        .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader)));

                executionContext.CommandBuffer.end();
                GfxContext::Get().SubmitImmediateExecuteContext(executionContext);

                m_Device->PushBindlessThing(vk::DescriptorImageInfo()
                                                .setImageView(m_MipChain[baseMipLevel].ImageView)
                                                .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                                                .setSampler(m_Description.SamplerCreateInfo.has_value()
                                                                ? m_Device->GetSampler(*m_Description.SamplerCreateInfo).first
                                                                : m_Device->GetDefaultSampler().first),
                                            m_MipChain[baseMipLevel].BindlessTextureID, Shaders::s_BINDLESS_COMBINED_IMAGE_SAMPLER_BINDING);

                if (!bDontTouchSampledImageDescriptors)
                    m_Device->PushBindlessThing(vk::DescriptorImageInfo()
                                                    .setImageView(m_MipChain[baseMipLevel].ImageView)
                                                    .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
                                                m_MipChain[baseMipLevel].BindlessSampledImageID, Shaders::s_BINDLESS_SAMPLED_IMAGE_BINDING);
            }

            if (m_Description.UsageFlags & vk::ImageUsageFlagBits::eStorage)
            {
                auto executionContext = GfxContext::Get().CreateImmediateExecuteContext(ECommandQueueType::COMMAND_QUEUE_TYPE_GENERAL);
                executionContext.CommandBuffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

                executionContext.CommandBuffer.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(
                    vk::ImageMemoryBarrier2()
                        .setImage(*m_Image)
                        .setSubresourceRange(vk::ImageSubresourceRange()
                                                 .setBaseArrayLayer(0)
                                                 .setBaseMipLevel(baseMipLevel)
                                                 .setLevelCount(currentMipCount)
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
                GfxContext::Get().SubmitImmediateExecuteContext(executionContext);

                m_Device->PushBindlessThing(
                    vk::DescriptorImageInfo().setImageView(m_MipChain[baseMipLevel].ImageView).setImageLayout(vk::ImageLayout::eGeneral),
                    m_MipChain[baseMipLevel].BindlessImageID, Shaders::s_BINDLESS_STORAGE_IMAGE_BINDING);
            }
        }
    }

    void GfxTexture::RG_Finalize() noexcept
    {
        CreateMipChainAndSubmitToBindlessPool();
    }

    void GfxTexture::GenerateMipMaps(const vk::CommandBuffer& cmd) const noexcept
    {
        const bool bCreateMips = m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_CREATE_MIPS_BIT;
        RDNT_ASSERT(bCreateMips, "bCreateMips is not specified!");

        const auto formatProps = m_Device->GetPhysicalDevice().getFormatProperties(m_Description.Format);
        RDNT_ASSERT(formatProps.optimalTilingFeatures & (vk::FormatFeatureFlagBits::eSampledImageFilterLinear |
                                                         vk::FormatFeatureFlagBits::eBlitSrc | vk::FormatFeatureFlagBits::eBlitDst),
                    "Texture image format doesn't support linear blitting!");

        vk::ImageAspectFlags aspectMask{};
        if (IsDepthFormat(m_Description.Format))
            aspectMask = vk::ImageAspectFlagBits::eDepth;
        else
            aspectMask = vk::ImageAspectFlagBits::eColor;

        if (IsStencilFormat(m_Description.Format)) aspectMask |= vk::ImageAspectFlagBits::eStencil;

        auto imageMemoryBarrier =
            vk::ImageMemoryBarrier2().setImage(*m_Image).setSubresourceRange(vk::ImageSubresourceRange()
                                                                                 .setBaseArrayLayer(0)
                                                                                 .setLevelCount(1)
                                                                                 .setLayerCount(m_Description.LayerCount)
                                                                                 .setAspectMask(aspectMask));
        const auto mipLevelCount = GetMipCount();

        u32 mipWidth = m_Description.Dimensions.x, mipHeight = m_Description.Dimensions.y;
        for (u32 baseMipLevel{1}; baseMipLevel < mipLevelCount; ++baseMipLevel)
        {
            imageMemoryBarrier.subresourceRange.setBaseMipLevel(baseMipLevel - 1);
            imageMemoryBarrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
                .setSrcStageMask(vk::PipelineStageFlagBits2::eAllTransfer)
                .setDstAccessMask(vk::AccessFlagBits2::eTransferRead)
                .setDstStageMask(vk::PipelineStageFlagBits2::eBlit);
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

            cmd.blitImage2(
                vk::BlitImageInfo2()
                    .setFilter(vk::Filter::eLinear)
                    .setSrcImage(*m_Image)
                    .setSrcImageLayout(vk::ImageLayout::eTransferSrcOptimal)
                    .setDstImage(*m_Image)
                    .setDstImageLayout(vk::ImageLayout::eTransferDstOptimal)
                    .setRegions(
                        vk::ImageBlit2()
                            .setSrcSubresource(previousMipSubresourceLayers)
                            .setSrcOffsets({vk::Offset3D(), vk::Offset3D(static_cast<i32>(mipWidth), static_cast<i32>(mipHeight), 1)})
                            .setDstSubresource(currentMipSubresourceLayers)
                            .setDstOffsets({vk::Offset3D(), vk::Offset3D(static_cast<i32>(mipWidth > 1 ? mipWidth / 2 : 1),
                                                                         static_cast<i32>(mipHeight > 1 ? mipHeight / 2 : 1), 1)})));

            imageMemoryBarrier.setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
                .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                .setSrcAccessMask(vk::AccessFlagBits2::eTransferRead)
                .setSrcStageMask(vk::PipelineStageFlagBits2::eBlit)
                .setDstAccessMask(vk::AccessFlagBits2::eShaderSampledRead)
                .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader);
            cmd.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(imageMemoryBarrier));

            mipWidth  = mipWidth > 1 ? mipWidth / 2 : 1;
            mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
        }

        // NOTE: Last mip level isn't covered by being the blit SRC!
        imageMemoryBarrier.subresourceRange.setBaseMipLevel(mipLevelCount - 1);
        imageMemoryBarrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
            .setSrcStageMask(vk::PipelineStageFlagBits2::eAllTransfer)
            .setDstAccessMask(vk::AccessFlagBits2::eShaderSampledRead)
            .setDstStageMask(vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader);
        cmd.pipelineBarrier2(vk::DependencyInfo().setImageMemoryBarriers(imageMemoryBarrier));
    }

    bool GfxTexture::Resize(const glm::uvec3& dimensions) noexcept
    {
        if (m_Description.Dimensions == dimensions) return false;
        m_Description.Dimensions = dimensions;

        Invalidate();
        return true;
    }

    void GfxTexture::Destroy() noexcept
    {
        if (!m_Image.has_value()) return;

        const bool bResourceMemoryAliasingControlledByRenderGraph =
            (m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_RENDER_GRAPH_MEMORY_CONTROLLED_BIT) &&
            !(m_Description.CreateFlags & EResourceCreateBits::RESOURCE_CREATE_FORCE_NO_RESOURCE_MEMORY_ALIASING_BIT);

        m_Device->PushObjectToDelete(
            [bResourceMemoryAliasingControlledByRenderGraph, movedMipChain = std::move(m_MipChain), movedImage = std::move(*m_Image),
             movedAllocation = std::move(m_Allocation)]() noexcept
            {
                for (auto& mipInfo : movedMipChain)
                {
                    auto& nonConstMipInfo = const_cast<MipInfo&>(mipInfo);
                    GfxContext::Get().GetDevice()->PopBindlessThing(nonConstMipInfo.BindlessTextureID,
                                                                    Shaders::s_BINDLESS_COMBINED_IMAGE_SAMPLER_BINDING);
                    if (mipInfo.BindlessSampledImageID.has_value())
                        GfxContext::Get().GetDevice()->PopBindlessThing(nonConstMipInfo.BindlessSampledImageID,
                                                                        Shaders::s_BINDLESS_SAMPLED_IMAGE_BINDING);

                    if (mipInfo.BindlessImageID.has_value())
                        GfxContext::Get().GetDevice()->PopBindlessThing(nonConstMipInfo.BindlessImageID,
                                                                        Shaders::s_BINDLESS_STORAGE_IMAGE_BINDING);

                    GfxContext::Get().GetDevice()->GetLogicalDevice()->destroyImageView(nonConstMipInfo.ImageView);
                }

                if (bResourceMemoryAliasingControlledByRenderGraph)
                    GfxContext::Get().GetDevice()->GetLogicalDevice()->destroyImage(movedImage);
                else
                    GfxContext::Get().GetDevice()->DeallocateTexture((VkImage&)movedImage, (VmaAllocation&)movedAllocation);
            });
        m_Image = std::nullopt;
    }

}  // namespace Radiant
