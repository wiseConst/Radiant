#include <pch.h>
#include "ForwardRenderer.hpp"

namespace Radiant
{
    bool ForwardBlinnPhongRenderer::BeginFrame() noexcept
    {
        m_RenderGraph = MakeUnique<RenderGraph>(s_ENGINE_NAME);

        return m_RenderSystem->BeginFrame();
    }

    void ForwardBlinnPhongRenderer::RenderFrame() noexcept {}

    void ForwardBlinnPhongRenderer::EndFrame() noexcept
    {
        m_RenderSystem->EndFrame();
    }

}  // namespace Radiant
