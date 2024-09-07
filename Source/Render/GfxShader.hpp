#pragma once

#include <vulkan/vulkan.hpp>

namespace Radiant
{

    struct GfxShaderDescription
    {
        // TODO: Per shader-stage defines
        std::string Path{s_DEFAULT_STRING};
    };

    class GfxDevice;
    class GfxShader final : private Uncopyable, private Unmovable
    {
      public:
        GfxShader(const Unique<GfxDevice>& device, const GfxShaderDescription& shaderDesc) noexcept
            : m_Device(device), m_Description(shaderDesc)
        {
            Invalidate();
        }
        ~GfxShader() noexcept = default;

        NODISCARD std::vector<vk::PipelineShaderStageCreateInfo> GetShaderStages() const noexcept;
        FORCEINLINE void HotReload() noexcept
        {
            m_ModuleMap.clear();
            Invalidate();
        }

      private:
        const Unique<GfxDevice>& m_Device;
        GfxShaderDescription m_Description{};
        UnorderedMap<vk::ShaderStageFlagBits, vk::UniqueShaderModule> m_ModuleMap;

        constexpr GfxShader() noexcept = delete;
        void Invalidate() noexcept;
        bool TryLoadCache() noexcept;
    };

}  // namespace Radiant
