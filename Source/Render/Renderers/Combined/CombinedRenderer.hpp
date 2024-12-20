#pragma once

#include <Render/Renderers/Renderer.hpp>

#include <shadows/csm_defines.hpp>

namespace Radiant
{

    class CombinedRenderer final : public Renderer
    {
      public:
        CombinedRenderer() noexcept;
        ~CombinedRenderer() noexcept final override = default;

        void RenderFrame() noexcept final override;

      private:
        // clustered light culling
        Unique<GfxPipeline> m_LightClustersBuildPipeline{nullptr};
        Unique<GfxPipeline> m_LightClustersDetectActivePipeline{nullptr};
        Unique<GfxPipeline> m_LightClustersAssignmentPipeline{nullptr};

        Unique<GfxPipeline> m_DepthPrePassPipeline{nullptr};
        Unique<GfxPipeline> m_CSMPipeline{nullptr};
        Unique<GfxPipeline> m_MainLightingPassPipeline{nullptr};
        Unique<GfxPipeline> m_FinalPassPipeline{nullptr};

        Unique<GfxPipeline> m_DepthBoundsComputePipeline{nullptr};
        Unique<GfxPipeline> m_ShadowsSetupPipeline{nullptr};

        // Screen-space shadows
        Unique<GfxPipeline> m_SSSPipeline{nullptr};

        // Screen-space ambient occlusion
        Unique<GfxPipeline> m_SSAOPipelineGraphics{nullptr};
        Unique<GfxPipeline> m_SSAOPipelineCompute{nullptr};  // better normal reconstruction
        Unique<GfxPipeline> m_SSAOBoxBlurPipelineGraphics{nullptr};
        Unique<GfxPipeline> m_SSAOBoxBlurPipelineCompute{nullptr};

        // Bloom from Call of Duty Advanced Warfare, ACM Siggraph '14,
        Unique<GfxPipeline> m_BloomDownsamplePipelineGraphics{nullptr};
        Unique<GfxPipeline> m_BloomUpsampleBlurPipelineGraphics{nullptr};
        // compute version is optimized
        Unique<GfxPipeline> m_BloomDownsamplePipelineCompute{nullptr};
        Unique<GfxPipeline> m_BloomUpsampleBlurPipelineCompute{nullptr};

        Unique<GfxBuffer> m_CubeIndexBuffer{nullptr};
        Unique<GfxPipeline> m_EnvMapSkyboxPipeline{nullptr};
        Unique<GfxTexture> m_IrradianceCubemapTexture{nullptr};
        Unique<GfxTexture> m_PrefilteredCubemapTexture{nullptr};
        Unique<GfxTexture> m_BrdfLutTexture{nullptr};

        RenderGraphStatistics m_RenderGraphStats = {};
        Unique<Shaders::LightData> m_LightData{MakeUnique<Shaders::LightData>()};

        static Shaders::CascadedShadowMapsData UpdateCSMData(const f32 cameraFovY, const f32 cameraAR, const f32 zNear, const f32 zFar,
                                                             const glm::mat4& cameraView, const glm::vec3& L) noexcept;
    };

}  // namespace Radiant
