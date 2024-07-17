#pragma once

#include <Render/CoreDefines.hpp>
#include <Render/GfxDevice.hpp>

template <> struct ankerl::unordered_dense::hash<vk::ShaderStageFlags>
{
    using is_avalanching = void;

    [[nodiscard]] auto operator()(const vk::ShaderStageFlags& x) const noexcept -> std::uint64_t
    {
        return detail::wyhash::hash(x.operator unsigned int());
    }
};

namespace Radiant
{

    struct GfxShaderDescription
    {
        // TODO: Per shader-stage defines
        std::string Path;
    };

    class GfxShader final : private Uncopyable, private Unmovable
    {
      public:
        GfxShader(const Unique<GfxDevice>& device, const GfxShaderDescription& shaderDesc) noexcept;
        ~GfxShader() noexcept = default;

        void ClearModules() noexcept { m_Modules.clear(); }

      private:
        const Unique<GfxDevice>& m_Device;
        GfxShaderDescription m_Description{};
        UnorderedMap<vk::ShaderStageFlags, vk::UniqueShaderModule> m_Modules;

        constexpr GfxShader() noexcept = delete;
        void Init() noexcept;
    };

}  // namespace Radiant
