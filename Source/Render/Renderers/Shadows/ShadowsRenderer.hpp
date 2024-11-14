#pragma once

#include <Render/Renderers/Renderer.hpp>
#include <shadows/csm_defines.hpp>

namespace Radiant
{

    class ShadowsRenderer final : public Renderer
    {
      public:
        ShadowsRenderer() noexcept;
        ~ShadowsRenderer() noexcept final override = default;

        void RenderFrame() noexcept final override;

      private:
        Unique<GfxPipeline> m_DepthPrePassPipeline{nullptr};

        Unique<GfxPipeline> m_DepthBoundsComputePipeline{nullptr};
        Unique<GfxPipeline> m_ShadowsSetupPipeline{nullptr};
        Unique<GfxPipeline> m_CSMPipeline{nullptr};

        Unique<GfxPipeline> m_MainLightingPassPipeline{nullptr};

        Unique<GfxPipeline> m_FinalPassPipeline{nullptr};

        RenderGraphStatistics m_RenderGraphStats = {};
        Unique<Shaders::LightData> m_LightData{MakeUnique<Shaders::LightData>()};

        static Shaders::CascadedShadowMapsData UpdateCSMData(const f32 cameraFovY, const f32 cameraAR, const f32 zNear, const f32 zFar,
                                                             const glm::mat4& cameraView, const glm::vec3& L) noexcept;
    };

}  // namespace Radiant
