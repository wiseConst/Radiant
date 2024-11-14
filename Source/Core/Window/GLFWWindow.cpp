#include "GLFWWindow.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace Radiant
{

    namespace GLFWUtils
    {
        static bool s_bIsGLFWInit = false;
        static std::atomic<u32> s_GLFWActiveWindowCount{0};
        static void InitGLFW() noexcept
        {
            if (s_bIsGLFWInit) return;

            if (glfwInit() != GLFW_TRUE)
            {
                LOG_ERROR("Failed to initialize glfw!");
                return;
            }

            glfwSetErrorCallback([](i32 error, const char* message) { LOG_ERROR("GLFW error[{}]: {}\n", error, message); });

            RDNT_ASSERT(glfwVulkanSupported() == GLFW_TRUE, "GLFW: Vulkan is not supported!");

            s_bIsGLFWInit = true;
        }

    }  // namespace GLFWUtils

    void GLFWWindow::WaitEvents() const noexcept
    {
        glfwWaitEvents();
    }

    NODISCARD std::vector<const char*> GLFWWindow::GetRequiredExtensions() const noexcept
    {
        RDNT_ASSERT(GLFWUtils::s_bIsGLFWInit, "GLFW is not init!");

        u32 glfwRequiredExtensionCount{0};
        const char** glfwRequiredExtensions = glfwGetRequiredInstanceExtensions(&glfwRequiredExtensionCount);

        RDNT_ASSERT(glfwRequiredExtensionCount > 0 && glfwRequiredExtensions, "GLFW_VK: Failed to retrieve required extensions!");

        std::vector<const char*> requiredExtensions;
        for (u32 i{}; i < glfwRequiredExtensionCount; ++i)
            requiredExtensions.emplace_back(glfwRequiredExtensions[i]);

        return requiredExtensions;
    }

    void GLFWWindow::PollInput() noexcept
    {
        glfwPollEvents();
    }

    bool GLFWWindow::IsRunning() const noexcept
    {
        return glfwWindowShouldClose(m_Handle) == GLFW_FALSE;
    }

    void GLFWWindow::SetTitle(const std::string_view& title) noexcept
    {
        assert(!title.empty());

        m_Description.Name = title;
        glfwSetWindowTitle(m_Handle, title.data());
    }

    bool GLFWWindow::IsMouseButtonPressed(const i32 glfwKey) const noexcept
    {
        RDNT_ASSERT(glfwKey < GLFW_KEY_LAST, "Unknown glfw key!");
        return glfwGetMouseButton(m_Handle, glfwKey) == GLFW_PRESS;
    }

    bool GLFWWindow::IsMouseButtonReleased(const i32 glfwKey) const noexcept
    {
        RDNT_ASSERT(glfwKey < GLFW_KEY_LAST, "Unknown glfw key!");
        return glfwGetMouseButton(m_Handle, glfwKey) == GLFW_RELEASE;
    }

    bool GLFWWindow::IsKeyPressed(const i32 glfwKey) const noexcept
    {
        RDNT_ASSERT(glfwKey < GLFW_KEY_LAST, "Unknown glfw key!");
        return glfwGetKey(m_Handle, glfwKey) == GLFW_PRESS;
    }

    bool GLFWWindow::IsKeyReleased(const i32 glfwKey) const noexcept
    {
        RDNT_ASSERT(glfwKey < GLFW_KEY_LAST, "Unknown glfw key!");
        return glfwGetKey(m_Handle, glfwKey) == GLFW_RELEASE;
    }

    glm::vec2 GLFWWindow::GetCursorPos() const noexcept
    {
        f64 xPos{0.}, yPos{0.};
        glfwGetCursorPos(m_Handle, &xPos, &yPos);
        return glm::vec2(xPos, yPos);
    }

    void GLFWWindow::Init() noexcept
    {
        GLFWUtils::InitGLFW();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        m_Handle = glfwCreateWindow(m_Description.Extent.x, m_Description.Extent.y, m_Description.Name.data(), nullptr, nullptr);
        RDNT_ASSERT(m_Handle, "Failed to create GLFW window!");

        // NOTE: Works only when cursor is disabled.
        //  if (glfwRawMouseMotionSupported()) glfwSetInputMode(m_Handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

        GLFWUtils::s_GLFWActiveWindowCount.fetch_add(1);

        glfwSetWindowUserPointer(m_Handle, this);
        glfwSetFramebufferSizeCallback(m_Handle,
                                       [](GLFWwindow* window, i32 width, i32 height)
                                       {
                                           void* data = glfwGetWindowUserPointer(window);
                                           RDNT_ASSERT(data, "glfwGetWindowUserPointer() returned invalid data!");

                                           GLFWWindow& actualWindow          = *(static_cast<GLFWWindow*>(data));
                                           actualWindow.m_Description.Extent = glm::uvec2{width, height};

                                           const WindowResizeData wrd{.Dimensions = actualWindow.m_Description.Extent};
                                           for (const auto& func : actualWindow.m_ResizeFuncQueue)
                                           {
                                               func(wrd);
                                           }
                                       });
    }

    void GLFWWindow::Destroy() noexcept
    {
        glfwDestroyWindow(m_Handle);
        GLFWUtils::s_GLFWActiveWindowCount.fetch_sub(1);

        if (GLFWUtils::s_GLFWActiveWindowCount.load(std::memory_order_relaxed) == 0u)
        {
            glfwTerminate();
            GLFWUtils::s_bIsGLFWInit = false;
        }
    }

}  // namespace Radiant
