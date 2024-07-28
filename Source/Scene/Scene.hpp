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
            m_Meshes.emplace_back(gfxContext, meshPath);
        }

        void IterateObjects(DrawContext& drawContext) noexcept
        {
            for (const auto& mesh : m_Meshes)
            {
                for (const auto& rootNode : mesh.RootNodes)
                {
                    rootNode->Iterate(drawContext, mesh.VertexPositionBuffers, mesh.VertexAttributeBuffers, mesh.IndexBuffers,
                                      mesh.MaterialBuffers, rootNode->WorldTransform);
                }
            }
        }

      private:
        std::string m_Name{s_DEFAULT_STRING};
        std::vector<Mesh> m_Meshes;
    };

}  // namespace Radiant
