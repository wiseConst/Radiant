#include <pch.h>
#include "ForwardRenderer.hpp"

namespace Radiant
{
    bool ForwardBlinnPhongRenderer::BeginFrame() noexcept
    {
       return m_RenderSystem->BeginFrame();
    }

    void ForwardBlinnPhongRenderer::EndFrame() noexcept
    {
        m_RenderSystem->EndFrame();
    }

}  // namespace Radiant
