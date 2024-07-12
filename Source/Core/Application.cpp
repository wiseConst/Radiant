#include <pch.h>
#include "Application.hpp"

#if 0

#endif

namespace Radiant
{
    void Application::Init() noexcept
    {
        Radiant::Log::Init();
        LOG_INFO("{}", __FUNCTION__);

        m_MainWindow   = MakeUnique<GLFWWindow>(WindowDescription{.Name = m_Description.Name, .Extent = m_Description.WindowExtent});
        m_RenderSystem = RenderSystem::Create(m_Description.RHI);
    }

    void Application::Run() noexcept
    {
        LOG_INFO("{}", __FUNCTION__);
        m_bIsRunning = true;

        auto lastTime = Timer::Now();
        uint32_t frameCount{0};
        float accumulatedDeltaTime{0.f};

        while (m_bIsRunning)
        {
            if (m_MainWindow->IsRunning() && !m_MainWindow->IsMinimized())
            {
                m_MainWindow->PollInput();

                ++m_FrameCounter;
                ++frameCount;

                auto currentTime = Timer::Now();
                m_DeltaTime      = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
                lastTime         = currentTime;

                accumulatedDeltaTime += m_DeltaTime;
                if (accumulatedDeltaTime >= 1.f || m_Description.FPSLimit != 0 && frameCount == m_Description.FPSLimit)
                {
                    std::stringstream ss;
                    ss << m_Description.Name;
                    ss << " / Frame: " << m_FrameCounter;
                    ss << " dt: " << m_DeltaTime;
                    ss << " FPS: " << frameCount;

                    m_MainWindow->SetTitle(ss.str());

                    frameCount           = 0;
                    accumulatedDeltaTime = 0.f;

                    const auto diff = 1.f / m_Description.FPSLimit - accumulatedDeltaTime;
                    std::this_thread::sleep_for(std::chrono::duration<float, std::chrono::seconds::period>(diff));
                }
            }

            if (!m_MainWindow->IsRunning()) m_bIsRunning = false;
        }
    }

    void Application::Destroy() noexcept
    {
        LOG_INFO("{}", __FUNCTION__);

        Log::Shutdown();
    }

}  // namespace Radiant
