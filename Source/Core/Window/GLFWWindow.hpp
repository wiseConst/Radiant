#pragma once

#include <Core/Core.hpp>

struct GLFWwindow;

namespace Radiant
{
    struct WindowDescription
    {
        std::string Name{s_DEFAULT_STRING};
        glm::uvec2 Extent{0};
    };

    class GLFWWindow final : private Uncopyable, private Unmovable
    {
      public:
        GLFWWindow(const WindowDescription& windowDesc) noexcept : m_Description(windowDesc) { Init(); }
        ~GLFWWindow() noexcept { Destroy(); }

        NODISCARD FORCEINLINE GLFWwindow* Get() const noexcept { return m_Handle; }
        NODISCARD FORCEINLINE const auto& GetDescription() const noexcept { return m_Description; }

        void PollInput() noexcept;

        NODISCARD FORCEINLINE bool IsMinimized() const noexcept { return m_Description.Extent == glm::uvec2(0); }
        bool IsRunning() const noexcept;

        void SetTitle(const std::string_view& title) noexcept;

      private:
        WindowDescription m_Description{};
        GLFWwindow* m_Handle{nullptr};

        void Init() noexcept;
        void Destroy() noexcept;
    };

}  // namespace Radiant
