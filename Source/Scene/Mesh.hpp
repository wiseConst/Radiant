#pragma once

#include <Core/Core.hpp>
#include <Render/GfxContext.hpp>
#include <Render/GfxBuffer.hpp>

namespace Radiant
{

    class GfxBuffer;
    struct StaticMesh
    {
        StaticMesh(const Unique<GfxContext>& gfxContext, const std::filesystem::path& meshFilePath) noexcept;

        struct Submesh
        {
            Unique<GfxBuffer> VertexPosBuffer{nullptr};
            Unique<GfxBuffer> VertexAttribBuffer{nullptr};
            Unique<GfxBuffer> IndexBuffer{nullptr};
            //   glm::vec4 BaseColor{1.f};
        };
        std::vector<Submesh> Submeshes;
    };

}  // namespace Radiant
