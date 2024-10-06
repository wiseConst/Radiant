#include <pch.hpp>
#include "Renderer.hpp"

#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

#include <GLFW/glfw3.h>

namespace Radiant
{
    Renderer::Renderer() noexcept
        : m_GfxContext(MakeUnique<GfxContext>()), m_RenderGraphResourcePool(MakeUnique<RenderGraphResourcePool>(m_GfxContext->GetDevice())),
          m_UIRenderer(MakeUnique<ImGuiRenderer>(m_GfxContext)), m_DebugRenderer(MakeUnique<DebugRenderer>(m_GfxContext))
    {
        Application::Get().GetMainWindow()->SubscribeToResizeEvents([=](const WindowResizeData& wrd)
                                                                    { m_MainCamera->OnResized(wrd.Dimensions); });

        m_ViewportExtent = m_GfxContext->GetSwapchainExtent();
    }

    Renderer::~Renderer() noexcept
    {
        m_GfxContext->GetDevice()->WaitIdle();
    }

    bool Renderer::BeginFrame() noexcept
    {
        m_RenderGraphResourcePool->Tick();
        m_RenderGraph = MakeUnique<RenderGraph>(m_GfxContext, s_ENGINE_NAME, m_RenderGraphResourcePool);

        const auto bImageAcquired = m_GfxContext->BeginFrame();
        m_ViewportExtent          = m_GfxContext->GetSwapchainExtent();  // Update extents after swapchain been recreated if needed.

        return bImageAcquired;
    }

    void Renderer::EndFrame() noexcept
    {
        m_GfxContext->EndFrame();
    }

    void Renderer::UpdateMainCamera(const f32 deltaTime) noexcept
    {
        auto& mainWindow = Application::Get().GetMainWindow();
        if (mainWindow->IsMouseButtonPressed(GLFW_MOUSE_BUTTON_2)) m_MainCamera->Rotate(deltaTime, mainWindow->GetCursorPos());

        RDNT_ASSERT(m_MainCamera, "MainCamera is invalid!");
        m_MainCamera->UpdateMousePos(mainWindow->GetCursorPos());

        glm::vec3 velocity{0.f};
        if (mainWindow->IsKeyPressed(GLFW_KEY_W)) velocity.z += -1.f;
        if (mainWindow->IsKeyPressed(GLFW_KEY_S)) velocity.z += 1.f;

        if (mainWindow->IsKeyPressed(GLFW_KEY_A)) velocity.x += -1.f;
        if (mainWindow->IsKeyPressed(GLFW_KEY_D)) velocity.x += 1.f;

        if (mainWindow->IsKeyPressed(GLFW_KEY_SPACE)) velocity.y += 1.f;
        if (mainWindow->IsKeyPressed(GLFW_KEY_LEFT_CONTROL)) velocity.y += -1.f;

        m_MainCamera->SetVelocity(velocity);
        m_MainCamera->Move(deltaTime);
        m_MainCamera->OnResized(glm::uvec2{m_ViewportExtent.width, m_ViewportExtent.height});
    }

}  // namespace Radiant
