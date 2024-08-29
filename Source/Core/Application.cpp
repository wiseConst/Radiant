#include <pch.h>
#include "Application.hpp"

#include <Render/Renderers/Combined/CombinedRenderer.hpp>
#include <Render/Renderers/Particle/ParticleRenderer.hpp>
#include <Render/Renderers/SSGI/SSGIRenderer.hpp>

namespace Radiant
{
    void Application::Init() noexcept
    {
        RDNT_ASSERT(!s_Instance, "Application instance already init!");
        s_Instance = this;

        Radiant::Log::Init();
        m_ThreadPool = MakeUnique<ThreadPool>();
        LOG_INFO("{}", __FUNCTION__);
        LOG_CRITICAL("Current working directory: {}", std::filesystem::current_path().string());

        m_MainWindow = MakeUnique<GLFWWindow>(WindowDescription{.Name = m_Description.Name, .Extent = m_Description.WindowExtent});

        m_Renderer = MakeUnique<CombinedRenderer>();
        //   m_Renderer = MakeUnique<ParticleRenderer>();
        // m_Renderer = MakeUnique<SSGIRenderer>();
    }

    void Application::Run() noexcept
    {
        RDNT_ASSERT(m_Renderer, "Renderer isn't setup!");

        LOG_INFO("{}", __FUNCTION__);
        m_bIsRunning = true;

        auto lastTime = Timer::Now();
        u32 frameCount{0};
        f32 accumulatedDeltaTime{0.f};

        while (m_bIsRunning)
        {
            if (!m_MainWindow->IsRunning())
            {
                m_bIsRunning = false;
                break;
            }

            if (m_MainWindow->IsMinimized())
            {
                m_MainWindow->WaitEvents();  // don't burn cpu
                continue;
            }

            if (!m_Renderer->BeginFrame()) continue;

            m_MainWindow->PollInput();
            m_Renderer->UpdateMainCamera(m_DeltaTime);
            m_Renderer->RenderFrame();

            ++frameCount;

            auto currentTime = Timer::Now();
            m_DeltaTime      = std::chrono::duration<f32, std::chrono::seconds::period>(currentTime - lastTime).count();
            lastTime         = currentTime;

            accumulatedDeltaTime += m_DeltaTime;
            if (accumulatedDeltaTime >= 1.f || m_Description.FPSLimit != 0 && frameCount == m_Description.FPSLimit)
            {
                frameCount           = 0;
                accumulatedDeltaTime = 0.f;

                if (m_Description.FPSLimit != 0)
                {
                    const auto diff = 1.f / m_Description.FPSLimit - accumulatedDeltaTime;
                    std::this_thread::sleep_for(std::chrono::duration<f32, std::chrono::seconds::period>(diff));
                }
            }

            m_Renderer->EndFrame();
        }
    }

    void Application::Shutdown() noexcept
    {
        m_ThreadPool.reset();
        m_Renderer.reset();
        m_MainWindow.reset();

        LOG_INFO("{}", __FUNCTION__);
        Log::Shutdown();

        s_Instance = nullptr;
    }

}  // namespace Radiant
