#include <pch.hpp>
#include "ParticleRenderer.hpp"

#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

namespace Radiant
{
    namespace ResourceNames
    {
        const std::string FinalRT{"Resource_FinalRT"};

    }  // namespace ResourceNames

    ParticleRenderer::ParticleRenderer() noexcept {}

    void ParticleRenderer::RenderFrame() noexcept
    {

        m_UIRenderer->RenderFrame(m_ViewportExtent, m_RenderGraph, ResourceNames::FinalRT,
                                  [&]()
                                  {
                                      static bool bShowDemoWindow = true;
                                      if (bShowDemoWindow) ImGui::ShowDemoWindow(&bShowDemoWindow);

                                      if (ImGui::Begin("Application Info"))
                                      {
                                          const auto& io = ImGui::GetIO();
                                          ImGui::Text("Application average [%.3f] ms/frame (%.1f FPS)", 1000.0f / io.Framerate,
                                                      io.Framerate);
                                      }
                                      ImGui::End();
                                  });

        m_RenderGraph->Build();
        m_RenderGraph->Execute();
    }

}  // namespace Radiant
