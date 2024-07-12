#include <pch.h>
#include "VulkanRenderSystem.hpp"

#define VOLK_IMPLEMENTATION
#include <Volk/volk.h>

namespace Radiant
{
    void VulkanRenderSystem::Init() noexcept
    {
        RDNT_ASSERT(volkInitialize() == VK_SUCCESS, "Failed to initialize volk!");

        // Firstly call it to retrieve vkCreateInstance function.

        // m_DispatchLoaderDynamic.init();
        auto appInfo = vk::ApplicationInfo()
                           .setPApplicationName("Radiant")
                           .setApplicationVersion(VK_MAKE_VERSION(-1, 0, 0))
                           .setPEngineName("Radiant engine")
                           .setEngineVersion(VK_MAKE_VERSION(-1, 0, 0))
                           .setApiVersion(VK_API_VERSION_1_3);

        auto instanceCreateInfo = vk::InstanceCreateInfo().setPApplicationInfo(&appInfo);
        m_Instance              = vk::createInstanceUnique(instanceCreateInfo);

        m_DispatchLoaderDynamic.init(*m_Instance, vkGetInstanceProcAddr);
        // Load the Vulkan function pointers using Volk
        volkLoadInstance(m_Instance.get());

        // Then call it to retrieve other functions.
        //     m_DispatchLoaderDynamic.init(m_Instance);
    }

    void VulkanRenderSystem::Shutdown() noexcept
    {
        volkFinalize();
    }

}  // namespace Radiant
