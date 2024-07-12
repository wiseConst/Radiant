#include <pch.h>
#include "RenderSystem.hpp"

#include <Render/RHI/Vulkan/VulkanRenderSystem.hpp>

namespace Radiant
{
    NODISCARD Unique<RenderSystem> RenderSystem::Create(const ERHI rhi) noexcept
    {
        switch (rhi)
        {
            case ERHI::RHI_VULKAN: return MakeUnique<VulkanRenderSystem>(rhi);
            default: RDNT_ASSERT(false, "Unknown RHI!");
        }

        return nullptr;
    }

}  // namespace Radiant
