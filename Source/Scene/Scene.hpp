#pragma once

#include <Core/Core.hpp>
#include <Scene/Mesh.hpp>

namespace Radiant
{

    class Scene final : private Uncopyable, private Unmovable
    {
      public:
        Scene(const std::string_view& name) noexcept : m_Name(name) {}
        ~Scene() = default;

        void LoadMesh(const Unique<GfxContext>& gfxContext, const std::string& meshPath) noexcept
        {
            m_StaticMeshes.emplace_back(gfxContext, meshPath);
        }

        // VertexPosBuffer, VertexAttribBuffer, IndexBuffer
        using IterateFunc = std::function<void(const Unique<GfxBuffer>&, const Unique<GfxBuffer>&, const Unique<GfxBuffer>&)>;
        void IterateObjects(IterateFunc&& func) noexcept
        {
            for (const auto& staticMesh : m_StaticMeshes)
            {
                for (const auto& staticSubmesh : staticMesh.Submeshes)
                {
                    RDNT_ASSERT(staticSubmesh.VertexPosBuffer && staticSubmesh.VertexAttribBuffer && staticSubmesh.IndexBuffer,
                                "Static submesh is invalid!");

                    func(staticSubmesh.VertexPosBuffer, staticSubmesh.VertexAttribBuffer, staticSubmesh.IndexBuffer);
                }
            }
        }

      private:
        std::string m_Name{s_DEFAULT_STRING};
        std::vector<StaticMesh> m_StaticMeshes;
    };

}  // namespace Radiant
