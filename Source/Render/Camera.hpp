#pragma once

#include <Render/CoreDefines.hpp>

namespace Radiant
{

    static constexpr float s_MouseSensitivity      = 15.f;
    static constexpr float s_CameraSpeed           = 0.1f;
    
    class Camera final : private Uncopyable, private Unmovable
    {
      public:
        Camera() noexcept  = default;
        ~Camera() noexcept = default;

        Camera(const float zoom /* expect FOV */, const float ar, const float zNear = 0.0001f, const float zFar = 1000.0f) noexcept
            : m_Zoom(zoom), m_AR(ar), m_zNear(zNear), m_zFar(zFar)
        {
            RecalculateProjectionMatrix();
            RecalculateViewMatrix();
        }

        NODISCARD FORCEINLINE const auto& GetProjectionMatrix() const noexcept { return m_ProjectionMatrix; }
        NODISCARD FORCEINLINE const auto& GetViewMatrix() const noexcept { return m_ViewMatrix; }

        void OnResized(const glm::uvec2& dimensions) noexcept
        {
            if (dimensions.y > 0) m_AR = static_cast<float>(dimensions.x) / static_cast<float>(dimensions.y);

            RecalculateProjectionMatrix();
        }

        void SetVelocity(const glm::vec3& velocity) noexcept { m_Velocity = velocity; }
        void Move(const float deltaTime) noexcept
        {
            if (m_Velocity == glm::vec3{0.f}) return;

            m_Position += glm::vec3(GetRotationMatrix() * glm::vec4(m_Velocity, 1.0f)) * deltaTime * s_CameraSpeed;
            RecalculateViewMatrix();
        }

        void UpdateMousePos(const glm::vec2& mousePos) noexcept { m_LastMousePos = mousePos; }
        void Rotate(const float deltaTime, const glm::vec2& mousePos) noexcept
        {
            const auto deltaMousePos = m_LastMousePos - mousePos;

            m_Yaw += s_MouseSensitivity * deltaMousePos.x * deltaTime;
            m_Pitch += s_MouseSensitivity * deltaMousePos.y * deltaTime;
            m_Pitch = std::clamp(m_Pitch, -89.0f, 89.0f);

            m_LastMousePos = mousePos;
            RecalculateViewMatrix();
        }

        void RecalculateViewMatrix() noexcept
        {
            // to create a correct model view, we need to move the world in opposite
            // direction to the camera
            //  so we will create the camera model matrix and invert
            const glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_Position);
            const glm::mat4 rotation    = GetRotationMatrix();
            m_ViewMatrix                = glm::inverse(translation * rotation);
        }

        void RecalculateProjectionMatrix() noexcept
        {
            // NOTE: Flipping viewport for VULKAN
            m_ProjectionMatrix = glm::perspective(glm::radians(m_Zoom), m_AR, m_zNear, m_zFar) * glm::scale(glm::vec3(1.0f, -1.0f, 1.0f));
        }

        NODISCARD FORCEINLINE const auto& GetShaderData() noexcept
        {
            m_InternalData = {.ProjectionMatrix     = m_ProjectionMatrix,
                              .ViewMatrix           = m_ViewMatrix,
                              .ViewProjectionMatrix = GetViewProjectionMatrix(),
                              .Position             = m_Position,
                              .zNear                = m_zNear,
                              .zFar                 = m_zFar};
            return m_InternalData;
        }

        NODISCARD FORCEINLINE glm::mat4 GetViewProjectionMatrix() const noexcept { return m_ProjectionMatrix * m_ViewMatrix; }

      private:
        CameraData m_InternalData{};
        glm::vec3 m_Velocity{1.0f};
        glm::vec3 m_Position{0.f};
        float m_Zoom{90.f};
        float m_AR{1.f};
        float m_Yaw{0.f};
        float m_Pitch{0.f};
        float m_zNear{0.001f};
        float m_zFar{1000.0f};
        glm::vec2 m_LastMousePos{0.0f};

        glm::mat4 m_ProjectionMatrix{1.f};
        glm::mat4 m_ViewMatrix{1.f};

        NODISCARD glm::mat4 GetRotationMatrix() const noexcept
        {
            // fairly typical FPS style camera. we join the pitch and yaw rotations into
            // the final rotation matrix

            glm::quat pitchRotation = glm::angleAxis(glm::radians(m_Pitch), glm::vec3(1.f, 0.f, 0.f));  // Rotating around X axis
            glm::quat yawRotation   = glm::angleAxis(glm::radians(m_Yaw), glm::vec3(0.f, 1.f, 0.f));    // Rotation around -Y axis

            return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
        }
    };

}  // namespace Radiant
