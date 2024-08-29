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
        GLFWWindow(const WindowDescription& windowDesc) noexcept : m_Description(windowDesc)
        {
            LOG_INFO("Created window: \"{}\", [{}x{}].", m_Description.Name, m_Description.Extent.x, m_Description.Extent.y);
            Init();
        }
        ~GLFWWindow() noexcept { Destroy(); }

        NODISCARD FORCEINLINE GLFWwindow* Get() const noexcept { return m_Handle; }
        NODISCARD FORCEINLINE const auto& GetDescription() const noexcept { return m_Description; }

        void WaitEvents() const noexcept;
        NODISCARD std::vector<const char*> GetRequiredExtensions() const noexcept;

        void PollInput() noexcept;

        NODISCARD FORCEINLINE bool IsMinimized() const noexcept { return m_Description.Extent.x == 0 || m_Description.Extent.y == 0; }
        bool IsRunning() const noexcept;

        void SetTitle(const std::string_view& title) noexcept;

        bool IsMouseButtonPressed(const i32 glfwKey) const noexcept;
        bool IsMouseButtonReleased(const i32 glfwKey) const noexcept;
        bool IsKeyPressed(const i32 glfwKey) const noexcept;
        bool IsKeyReleased(const i32 glfwKey) const noexcept;
        glm::vec2 GetCursorPos() const noexcept;

        FORCEINLINE void SubscribeToResizeEvents(std::function<void(const WindowResizeData&)>&& func) noexcept
        {
            m_ResizeFuncQueue.emplace_back(std::forward<std::function<void(const WindowResizeData&)>>(func));
        }

      private:
        WindowDescription m_Description{};
        GLFWwindow* m_Handle{nullptr};

        std::deque<std::function<void(const WindowResizeData&)>> m_ResizeFuncQueue;

        void Init() noexcept;
        void Destroy() noexcept;
    };

}  // namespace Radiant
