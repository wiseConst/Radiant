#include <pch.h>
#include "Renderer.hpp"

#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

#include <GLFW/glfw3.h>

namespace Radiant
{
    Renderer::Renderer() noexcept : m_GfxContext(MakeUnique<GfxContext>())
    {
        Application::Get().GetMainWindow()->SubscribeToResizeEvents([=](const WindowResizeData& wrd)
                                                                    { m_MainCamera->OnResized(wrd.Dimensions); });

        m_ViewportExtent = m_GfxContext->GetSwapchainExtent();
    }

    Renderer::~Renderer() noexcept
    {
        m_GfxContext->GetDevice()->WaitIdle();
    }

    void Renderer::UpdateMainCamera(const float deltaTime) noexcept
    {

        auto& mainWindow = Application::Get().GetMainWindow();
        if (mainWindow->IsMouseButtonPressed(GLFW_MOUSE_BUTTON_1))
            m_MainCamera->Rotate(Application::Get().GetDeltaTime(), mainWindow->GetCursorPos());

        m_MainCamera->UpdateMousePos(mainWindow->GetCursorPos());

        glm::vec3 velocity{0.f};
        if (mainWindow->IsKeyPressed(GLFW_KEY_W)) velocity.z += -1.f;
        if (mainWindow->IsKeyPressed(GLFW_KEY_S)) velocity.z += 1.f;

        if (mainWindow->IsKeyPressed(GLFW_KEY_A)) velocity.x += -1.f;
        if (mainWindow->IsKeyPressed(GLFW_KEY_D)) velocity.x += 1.f;

        if (mainWindow->IsKeyPressed(GLFW_KEY_SPACE)) velocity.y += 1.f;
        if (mainWindow->IsKeyPressed(GLFW_KEY_LEFT_CONTROL)) velocity.y += -1.f;

        m_MainCamera->SetVelocity(velocity);
        m_MainCamera->Move(Application::Get().GetDeltaTime());
    }

}  // namespace Radiant
