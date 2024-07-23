#pragma once

#include <Render/CoreDefines.hpp>
#include <Render/GfxDevice.hpp>
#include <Render/GfxBuffer.hpp>

namespace Radiant
{

    struct TextureUploadRequest
    {
    };

    struct BufferUploadRequest
    {
    };

    struct StreamerDescription
    {
        std::size_t MaxHostBufferSize{268'435'456};  // 256 MB default.
    };

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
