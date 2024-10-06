#pragma once

#include <Render/CoreDefines.hpp>

namespace Radiant
{

    static constexpr f32 s_MouseSensitivity = 15.f;
    static constexpr f32 s_CameraSpeed      = 5.0f;

    class Camera final : private Uncopyable, private Unmovable
    {
      public:
        Camera() noexcept  = default;
        ~Camera() noexcept = default;

        Camera(const f32 zoom /* expect FOV */, const f32 ar, const f32 zNear = 0.0001f, const f32 zFar = 1000.0f) noexcept
            : m_Zoom(zoom), m_AR(ar), m_zNear(zNear), m_zFar(zFar)
        {
            RecalculateProjectionMatrix();
            RecalculateViewMatrix();
            UpdateShaderData();
        }

        NODISCARD FORCEINLINE const auto& GetProjectionMatrix() const noexcept { return m_ProjectionMatrix; }
        NODISCARD FORCEINLINE const auto& GetViewMatrix() const noexcept { return m_ViewMatrix; }
        NODISCARD FORCEINLINE const auto GetZFar() const noexcept { return m_zFar; }
        NODISCARD FORCEINLINE const auto GetZNear() const noexcept { return m_zNear; }
        NODISCARD FORCEINLINE const auto GetAspectRatio() const noexcept { return m_AR; }

        void OnResized(const glm::uvec2& dimensions) noexcept
        {
            if (dimensions == glm::uvec2{static_cast<u32>(m_FullResolution.x), static_cast<u32>(m_FullResolution.y)}) return;

            if (dimensions.y > 0) m_AR = static_cast<f32>(dimensions.x) / static_cast<f32>(dimensions.y);

            m_FullResolution = dimensions;
            RecalculateProjectionMatrix();
        }

        void SetVelocity(const glm::vec3& velocity) noexcept { m_Velocity = velocity; }
        void Move(const f32 deltaTime) noexcept
        {
            if (m_Velocity == glm::vec3{0.f}) return;

            m_Position += glm::vec3(GetRotationMatrix() * glm::vec4(m_Velocity, 1.0f)) * deltaTime * s_CameraSpeed;
            RecalculateViewMatrix();
        }

        void UpdateMousePos(const glm::vec2& mousePos) noexcept { m_LastMousePos = mousePos; }
        void Rotate(const f32 deltaTime, const glm::vec2& mousePos) noexcept
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
            UpdateShaderData();
            return m_InternalData;
        }

        NODISCARD FORCEINLINE auto GetZoom() const noexcept { return m_Zoom; }
        NODISCARD FORCEINLINE glm::mat4 GetViewProjectionMatrix() const noexcept { return m_ProjectionMatrix * m_ViewMatrix; }

      private:
        Shaders::CameraData m_InternalData{};
        glm::vec3 m_Velocity{1.0f};
        glm::vec3 m_Position{0.f};
        f32 m_Zoom{90.f};
        f32 m_AR{1.f};
        f32 m_Yaw{0.f};
        f32 m_Pitch{0.f};
        f32 m_zNear{0.001f};
        f32 m_zFar{1000.0f};
        glm::vec2 m_LastMousePos{0.0f};

        glm::vec2 m_FullResolution{1.0f, 1.0f};
        glm::mat4 m_ProjectionMatrix{1.f};
        glm::mat4 m_ViewMatrix{1.f};

        NODISCARD glm::mat4 GetRotationMatrix() const noexcept
        {
            glm::quat pitchRotation = glm::angleAxis(glm::radians(m_Pitch), glm::vec3(1.f, 0.f, 0.f));  // Rotating around X axis
            glm::quat yawRotation   = glm::angleAxis(glm::radians(m_Yaw), glm::vec3(0.f, 1.f, 0.f));    // Rotation around -Y axis

            return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
        }

        void UpdateShaderData() noexcept
        {
            const auto viewProjMatrix = GetViewProjectionMatrix();
            m_InternalData            = {.ProjectionMatrix        = m_ProjectionMatrix,
                                         .ViewMatrix              = m_ViewMatrix,
                                         .ViewProjectionMatrix    = viewProjMatrix,
                                         .InvProjectionMatrix     = glm::inverse(m_ProjectionMatrix),
                                         .InvViewProjectionMatrix = glm::inverse(viewProjMatrix),
                                         .FullResolution          = m_FullResolution,
                                         .InvFullResolution       = 1.0f / m_FullResolution,
                                         .Position                = m_Position,
                                         .zNearFar                = {m_zNear, m_zFar},
                                         .Zoom                    = m_Zoom};
        }
    };

}  // namespace Radiant
