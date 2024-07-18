#pragma once

#include <Core/Core.hpp>

namespace Radiant
{

    class GfxBuffer;
    struct StaticMesh
    {
        StaticMesh(const std::string& meshPath) noexcept;

        Shared<GfxBuffer> VertexBuffer;
        Shared<GfxBuffer> IndexBuffer;
        glm::vec4 BaseColor;
    };

}  // namespace Radiant
