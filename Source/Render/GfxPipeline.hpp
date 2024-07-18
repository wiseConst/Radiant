#pragma once

#include <Render/CoreDefines.hpp>
#include <Render/GfxDevice.hpp>

#include <variant>

namespace Radiant
{

    // NOTE: Use vulkan type_traits to fill in
    struct VertexBufferLayout
    {
      public:
        struct VertexBufferElement
        {
            std::uint32_t Offset{0};
            std::uint32_t Size{0};
        };

      private:
        std::uint32_t m_Stride{0};
        std::vector<VertexBufferElement> m_Elements{};

        void CalculateStride() noexcept;
    };

    struct GfxGraphicsPipelineOptions
    {
        // TODO: std::vector<BufferLayout> VertexStreams;
        std::vector<vk::Format> RenderingFormats{};
        UnorderedSet<vk::DynamicState> DynamicStates{};

        vk::CullModeFlags CullMode{vk::CullModeFlagBits::eNone};
        vk::FrontFace FrontFace{vk::FrontFace::eCounterClockwise};
        vk::PrimitiveTopology PrimitiveTopology{vk::PrimitiveTopology::eTriangleList};
        vk::PolygonMode PolygonMode{vk::PolygonMode::eFill};

        bool bMeshShading{false};
        bool bDepthClamp{false};
        bool bDepthTest{false};
        bool bDepthWrite{false};
        vk::CompareOp DepthCompareOp{vk::CompareOp::eNever};

        bool bStencilTest{false};
        vk::StencilOp Back{vk::StencilOp::eZero};
        vk::StencilOp Front{vk::StencilOp::eZero};

        bool bBlendEnable{false};
        // TODO: Blend flags
    };

    struct GfxComputePipelineOptions
    {
    };

    struct GfxRayTracingPipelineOptions
    {
        std::uint32_t MaxRayRecursionDepth{1};
    };

    class GfxShader;
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
        GfxPipeline(const Unique<GfxDevice>& device, const vk::UniquePipelineLayout& bindlessPipelineLayout,
                    const GfxPipelineDescription& pipelineDesc) noexcept
            : m_Device(device), m_BindlessPipelineLayout(bindlessPipelineLayout), m_Description(pipelineDesc)
        {
            if (auto* gpo = std::get_if<GfxGraphicsPipelineOptions>(&m_Description.PipelineOptions); gpo)
            {
                gpo->DynamicStates.insert(vk::DynamicState::eViewport);
                gpo->DynamicStates.insert(vk::DynamicState::eScissor);
            }
            Invalidate();
        }
        ~GfxPipeline() noexcept = default;

        operator const vk::Pipeline&() const noexcept { return *m_Handle; }

        void HotReload() noexcept;

      private:
        const Unique<GfxDevice>& m_Device;
        const vk::UniquePipelineLayout& m_BindlessPipelineLayout;

        GfxPipelineDescription m_Description{};
        vk::UniquePipeline m_Handle{};

        constexpr GfxPipeline() noexcept = delete;
        void Invalidate() noexcept;
    };

}  // namespace Radiant
