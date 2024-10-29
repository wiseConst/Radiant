#pragma once

#include <Core/Core.hpp>
#include <variant>

#include <vulkan/vulkan.hpp>

namespace Radiant
{

    class GfxDevice;
    class GfxShader;

    // NOTE: Programmable Vertex Pulling only.
    struct GfxGraphicsPipelineOptions
    {
        std::vector<vk::Format> RenderingFormats{};
        std::vector<vk::DynamicState> DynamicStates{};

        vk::CullModeFlags CullMode{vk::CullModeFlagBits::eNone};
        vk::FrontFace FrontFace{vk::FrontFace::eCounterClockwise};
        vk::PrimitiveTopology PrimitiveTopology{vk::PrimitiveTopology::eTriangleList};
        vk::PolygonMode PolygonMode{vk::PolygonMode::eFill};

        bool bMeshShading{false};
        bool bDepthClamp{false};
        bool bDepthTest{false};
        bool bDepthWrite{false};
        vk::CompareOp DepthCompareOp{vk::CompareOp::eNever};

        glm::vec2 DepthBounds{0.f};  // Range [0.0f, 1.0f] for example.

        vk::StencilOp Back{vk::StencilOp::eZero};
        vk::StencilOp Front{vk::StencilOp::eZero};
        bool bStencilTest{false};
        bool bMultisample{false};

        enum class EBlendMode : u8
        {
            BLEND_MODE_NONE,
            BLEND_MODE_ADDITIVE,
            BLEND_MODE_ALPHA,
        };
        std::vector<EBlendMode> BlendModes;  // Per format, this array should have the same order as "RenderingFormats" if used.
    };

    struct GfxComputePipelineOptions
    {
    };

    struct GfxRayTracingPipelineOptions
    {
        u32 MaxRayRecursionDepth{1};
    };

    struct GfxPipelineDescription
    {
        std::string DebugName{s_DEFAULT_STRING};
        std::variant<std::monostate, GfxGraphicsPipelineOptions, GfxComputePipelineOptions, GfxRayTracingPipelineOptions> PipelineOptions{
            std::monostate{}};
        Shared<GfxShader> Shader{nullptr};
    };

    class GfxPipeline final : private Uncopyable, private Unmovable
    {
      public:
        GfxPipeline(const Unique<GfxDevice>& device, const GfxPipelineDescription& pipelineDesc) noexcept
            : m_Device(device), m_Description(pipelineDesc)
        {
            if (auto* gpo = std::get_if<GfxGraphicsPipelineOptions>(&m_Description.PipelineOptions); gpo)
            {
                gpo->DynamicStates.emplace_back(vk::DynamicState::eViewportWithCount);
                gpo->DynamicStates.emplace_back(vk::DynamicState::eScissorWithCount);
            }
            Invalidate();
            LOG_INFO("Created <{}> pipeline.", m_Description.DebugName);
        }
        ~GfxPipeline() noexcept { Destroy(); }

        NODISCARD FORCEINLINE const auto& GetDescription() const noexcept { return m_Description; }
        operator const vk::Pipeline&() const noexcept;

        void HotReload() noexcept;

      private:
        const Unique<GfxDevice>& m_Device;

        GfxPipelineDescription m_Description{};
        mutable vk::UniquePipeline m_Handle{};
        mutable vk::UniquePipeline m_Dummy{};
        mutable std::atomic<bool> m_bCanSwitchHotReloadedDummy{true};
        mutable std::atomic<bool> m_bIsHotReloadGoing{false};

        constexpr GfxPipeline() noexcept = delete;
        void Invalidate() noexcept;
        void Destroy() noexcept;
    };

}  // namespace Radiant
