#pragma once

#include <Render/CoreDefines.hpp>
#include <Render/GfxDevice.hpp>
#include <Render/GfxBuffer.hpp>

namespace Radiant
{

    // NOTE: Highly inspired by NVidia's implementation.

    struct TextureUploadRequest final
    {                               // Data to upload
        const void* Data{nullptr};  // pointer must be valid until "CopyStreamerUpdateRequests" call
        uint32_t dataRowPitch;
        uint32_t dataSlicePitch;

        // Destination (mandatory)
        // NRI_NAME(Texture) * dstTexture;
        // NRI_NAME(TextureRegionDesc) dstRegionDesc;
        // std::size_t offset;
    };

    struct BufferUploadRequest final
    {                               // Data to upload
        const void* Data{nullptr};  // pointer must be valid until "CopyStreamerUpdateRequests" call
        std::uint64_t DataSize{0};

        // Destination (optional, ignored for constants)
        // NRI_NAME(Buffer) * dstBuffer;
        // uint64_t dstBufferOffset;
        // std::size_t offset;
    };

    // struct StreamerDescription
    //{
    //     std::size_t MaxHostBufferSize{268'435'456};  // 256 MB default.
    //                                                             // Statically allocated ring-buffer for dynamic constants (optional)
    //     NRI_NAME(MemoryLocation) constantBufferMemoryLocation;  // UPLOAD or DEVICE_UPLOAD
    //     uint64_t constantBufferSize;

    //    // Dynamically (re)allocated ring-buffer for copying and rendering (mandatory)
    //    NRI_NAME(MemoryLocation) dynamicBufferMemoryLocation;  // UPLOAD or DEVICE_UPLOAD
    //    NRI_NAME(BufferUsageBits) dynamicBufferUsageBits;
    //    uint32_t frameInFlightNum;
    //};

    class Streamer final : private Uncopyable, private Unmovable
    {
      public:
        Streamer(Unique<GfxDevice>& gfxDevice) noexcept : m_GfxDevice(gfxDevice) {}
        ~Streamer() noexcept = default;

      private:
        Unique<GfxDevice>& m_GfxDevice;

        constexpr Streamer() noexcept = delete;
    };

}  // namespace Radiant
