#include <pch.h>
#include "GLFWWindow.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <Core/Application.hpp>  // To query current RHI

namespace Radiant
{

    namespace GLFWUtils
    {
        static bool s_bIsGLFWInit = false;
        static std::atomic<uint32_t> s_GLFWActiveWindowCount{0};
        static void InitGLFW() noexcept
        {
            if (s_bIsGLFWInit) return;

            if (glfwInit() != GLFW_TRUE)
            {
                LOG_ERROR("Failed to initialize glfw!");
                return;
            }

            glfwSetErrorCallback([](int32_t error, const char* message) { LOG_ERROR("GLFW error[{}]: {}\n", error, message); });

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

        std::uint32_t glfwRequiredExtensionCount{0};
        const char** glfwRequiredExtensions = glfwGetRequiredInstanceExtensions(&glfwRequiredExtensionCount);

        RDNT_ASSERT(glfwRequiredExtensionCount > 0 && glfwRequiredExtensions, "GLFW_VK: Failed to retrieve required extensions!");

        std::vector<const char*> requiredExtensions;
        for (std::uint32_t i{}; i < glfwRequiredExtensionCount; ++i)
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

    void GLFWWindow::Init() noexcept
    {
        GLFWUtils::InitGLFW();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        m_Handle = glfwCreateWindow(m_Description.Extent.x, m_Description.Extent.y, m_Description.Name.data(), nullptr, nullptr);
        if (!m_Handle)
        {
            LOG_ERROR("Failed to create GLFW window!");
            glfwTerminate();
            return;
        }

        GLFWUtils::s_GLFWActiveWindowCount.fetch_add(1);
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
