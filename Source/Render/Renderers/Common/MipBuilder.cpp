#include <pch.h>
#include "MipBuilder.hpp"

#include <Render/GfxContext.hpp>
#include <Render/RenderGraph.hpp>

namespace Radiant
{
    void MipBuilder::Init() noexcept {}

    void MipBuilder::BuildMips(Unique<RenderGraph>& renderGraph) noexcept {}

    MipBuilder::~MipBuilder() noexcept
    {
        m_GfxContext->GetDevice()->WaitIdle();
    }

}  // namespace Radiant
