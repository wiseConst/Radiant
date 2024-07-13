#include <pch.h>
#include "VulkanRenderSystem.hpp"

#define VMA_IMPLEMENTATION
#define VK_NO_PROTOTYPES
#include "vma/vk_mem_alloc.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Radiant
{

    void VulkanRenderSystem::Init() noexcept
    {
        LOG_INFO("{}", __FUNCTION__);

        // Initialize minimal set of function pointers.
        VULKAN_HPP_DEFAULT_DISPATCHER.init();

        std::vector<const char*> enabledInstanceLayers;
        std::vector<const char*> enabledInstanceExtensions;

        if constexpr (RDNT_DEBUG || s_bForceGfxValidation)
        {
            enabledInstanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            enabledInstanceLayers.emplace_back("VK_LAYER_KHRONOS_validation");
        }

        // Simple safety check if our layers/extensions are supported.
        const auto instanceExtensions = vk::enumerateInstanceExtensionProperties();
        for (const auto& eie : enabledInstanceExtensions)
        {
            bool bExtensionSupported{false};
            for (const auto& ie : instanceExtensions)
            {
                if (strcmp(eie, ie.extensionName.data()) != 0) continue;

                bExtensionSupported = true;
                break;
            }
            RDNT_ASSERT(bExtensionSupported, "Unsupported extension: {} ", eie);
        }

        const auto instanceLayers = vk::enumerateInstanceLayerProperties();
        for (const auto& eil : enabledInstanceLayers)
        {
            bool bLayerSupported{false};
            for (const auto& il : instanceLayers)
            {
                LOG_INFO("{}", il.layerName.data());
                if (strcmp(eil, il.layerName.data()) != 0) continue;

                bLayerSupported = true;
                break;
            }
            RDNT_ASSERT(bLayerSupported, "Unsupported layer: {} ", eil);
        }

        const uint32_t apiVersion = vk::enumerateInstanceVersion();
        RDNT_ASSERT(apiVersion >= VK_API_VERSION_1_3, "Old vulkan API version! Required at least 1.3!");
        const auto appInfo = vk::ApplicationInfo()
                                 .setPApplicationName(s_ENGINE_NAME)
                                 .setApplicationVersion(VK_MAKE_VERSION(1, 0, 0))
                                 .setPEngineName(s_ENGINE_NAME)
                                 .setEngineVersion(VK_MAKE_VERSION(1, 0, 0))
                                 .setApiVersion(apiVersion);

        m_Instance = vk::createInstanceUnique(vk::InstanceCreateInfo()
                                                  .setPApplicationInfo(&appInfo)
                                                  .setEnabledExtensionCount(enabledInstanceExtensions.size())
                                                  .setPEnabledExtensionNames(enabledInstanceExtensions)
                                                  .setEnabledLayerCount(enabledInstanceLayers.size())
                                                  .setPEnabledLayerNames(enabledInstanceLayers));

        LOG_TRACE("VkInstance {}.{}.{} created.", VK_VERSION_MAJOR(apiVersion), VK_VERSION_MINOR(apiVersion), VK_VERSION_PATCH(apiVersion));

        // Load other set of function pointers.
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*m_Instance);

        // Creating debug utils messenger.
        if constexpr (RDNT_DEBUG || s_bForceGfxValidation)
        {
            constexpr auto debugCallback =
                [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                   const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, MAYBE_UNUSED void* pUserData) noexcept -> VkBool32
            {
                switch (messageSeverity)
                {
                    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: LOG_TRACE("{}", pCallbackData->pMessage); break;
                    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: LOG_INFO("{}", pCallbackData->pMessage); break;
                    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: LOG_WARN("{}", pCallbackData->pMessage); break;
                    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: LOG_ERROR("{}", pCallbackData->pMessage); break;
                }

                return VK_FALSE;
            };

            const auto dumCI =
                vk::DebugUtilsMessengerCreateInfoEXT()
                    .setPfnUserCallback(debugCallback)
                    .setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                                        vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose)
                    .setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                                    vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                                    vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding);
            m_DebugUtilsMessenger = m_Instance->createDebugUtilsMessengerEXTUnique(dumCI, nullptr, VULKAN_HPP_DEFAULT_DISPATCHER);
        }

        auto gpus = m_Instance->enumeratePhysicalDevices();
        LOG_TRACE("{} gpus present.", gpus.size());
        for (auto& gpu : gpus)
        {
            const auto props = gpu.getProperties();
            LOG_TRACE("{}", props.deviceName.data());
        }

        VmaAllocator testAllocator{};
        VmaAllocatorCreateInfo allocatorCI = {};
        RDNT_ASSERT(vmaCreateAllocator(&allocatorCI, &testAllocator) == VK_SUCCESS, "Failed to create VMA!");
    }

    void VulkanRenderSystem::Shutdown() noexcept
    {
        LOG_INFO("{}", __FUNCTION__);
    }

}  // namespace Radiant
