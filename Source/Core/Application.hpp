#pragma once

#include <Core/Core.hpp>

#include <Systems/RenderSystem.hpp>
#include <Core/Window/GLFWWindow.hpp>

namespace Radiant
{

    struct CommandLineArguments
    {
        uint32_t Argc{0};
        char** Argv{nullptr};
    };

    struct ApplicationDescription
    {
        std::string Name{s_DEFAULT_STRING};
        CommandLineArguments CmdArgs{};
        glm::uvec2 WindowExtent{1280, 720};
        uint32_t FPSLimit{60};
        ERHI RHI{ERHI::RHI_NONE};
    };

    class GLFWWindow;
    class Application final
    {
      public:
        Application(const ApplicationDescription& appDesc) noexcept : m_Description(appDesc) { Init(); }
        ~Application() noexcept { Destroy(); }

        void Run() noexcept;

        NODISCARD FORCEINLINE const auto& GetDescription() const noexcept { return m_Description; }
        NODISCARD FORCEINLINE static Unique<Application> Create(const ApplicationDescription& appDesc) noexcept
        {
            return MakeUnique<Application>(appDesc);
        }

      private:
        Unique<GLFWWindow> m_MainWindow{nullptr};
        Unique<RenderSystem> m_RenderSystem{nullptr};

        ApplicationDescription m_Description{};
        bool m_bIsRunning{false};
        float m_DeltaTime{0.f};
        uint64_t m_FrameCounter{0};

        constexpr Application() noexcept = delete;
        void Init() noexcept;
        void Destroy() noexcept;
    };

}  // namespace Radiant
