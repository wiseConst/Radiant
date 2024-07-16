#pragma once

#include <Core/Core.hpp>
#include <Render/CoreDefines.hpp>

namespace Radiant
{

    class Camera final : private Uncopyable, private Unmovable
    {
      public:
        Camera() noexcept  = default;
        ~Camera() noexcept = default;

      private:
        glm::vec3 m_Position{0.f};
        float m_Zoom{90.f};

        glm::mat4 m_ProjectionMatrix;
        glm::mat4 m_ViewMatrix;

    };

}  // namespace Radiant
