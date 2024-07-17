#pragma once

#include <Core/Core.hpp>

namespace Radiant
{

    class GfxBuffer;
    struct StaticMesh
    {


        Shared<GfxBuffer> VertexBuffer;
        Shared<GfxBuffer> IndexBuffer;
        glm::vec4 BaseColor;
    };

}  // namespace Radiant
