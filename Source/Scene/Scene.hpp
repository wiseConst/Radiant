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

        void LoadMesh(const std::string& meshPath) noexcept { 
            m_StaticMeshes.emplace_back(meshPath); 
        }

      private:
        std::string m_Name{s_DEFAULT_STRING};
        std::vector<StaticMesh> m_StaticMeshes;
    };

}  // namespace Radiant
