#include "GfxShader.hpp"

#include <Render/GfxDevice.hpp>

#include <slang.h>
#include <slang-com-ptr.h>

namespace Radiant
{
#if RDNT_DEBUG
    static constexpr const char* s_ShaderCacheDir = "shader_cache_debug/";
#elif RDNT_RELEASE
    static constexpr const char* s_ShaderCacheDir = "shader_cache_optimized/";
#else
#error Unknown build type!
#endif

    namespace SlangUtils
    {
        NODISCARD static vk::ShaderStageFlagBits SlangShaderStageToVulkan(const SlangStage shaderStage) noexcept
        {
            switch (shaderStage)
            {
                case SlangStage::SLANG_STAGE_VERTEX: return vk::ShaderStageFlagBits::eVertex;
                case SlangStage::SLANG_STAGE_HULL: return vk::ShaderStageFlagBits::eTessellationControl;
                case SlangStage::SLANG_STAGE_DOMAIN: return vk::ShaderStageFlagBits::eTessellationEvaluation;
                case SlangStage::SLANG_STAGE_GEOMETRY: return vk::ShaderStageFlagBits::eGeometry;
                case SlangStage::SLANG_STAGE_FRAGMENT: return vk::ShaderStageFlagBits::eFragment;
                case SlangStage::SLANG_STAGE_COMPUTE: return vk::ShaderStageFlagBits::eCompute;
                case SlangStage::SLANG_STAGE_RAY_GENERATION: return vk::ShaderStageFlagBits::eRaygenKHR;
                case SlangStage::SLANG_STAGE_INTERSECTION: return vk::ShaderStageFlagBits::eIntersectionKHR;
                case SlangStage::SLANG_STAGE_ANY_HIT: return vk::ShaderStageFlagBits::eAnyHitKHR;
                case SlangStage::SLANG_STAGE_CLOSEST_HIT: return vk::ShaderStageFlagBits::eClosestHitKHR;
                case SlangStage::SLANG_STAGE_MISS: return vk::ShaderStageFlagBits::eMissKHR;
                case SlangStage::SLANG_STAGE_CALLABLE: return vk::ShaderStageFlagBits::eCallableKHR;
                case SlangStage::SLANG_STAGE_MESH: return vk::ShaderStageFlagBits::eMeshEXT;
                case SlangStage::SLANG_STAGE_AMPLIFICATION: return vk::ShaderStageFlagBits::eTaskEXT;
            }

            RDNT_ASSERT(false, "Unknown slang shader stage!");
            return vk::ShaderStageFlagBits::eVertex;
        }

    }  // namespace SlangUtils

    NODISCARD std::vector<vk::PipelineShaderStageCreateInfo> GfxShader::GetShaderStages() noexcept
    {
        std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
        if (m_ModuleMap.empty())
        {
            LOG_WARN("ModuleMap[{}] is empty, hot reloading...", m_Description.Path);
            HotReload();
        }

        for (auto& [shaderStage, module] : m_ModuleMap)
        {
            RDNT_ASSERT(module, "Shader module isnt' valid!s");

            shaderStages.emplace_back(vk::PipelineShaderStageCreateInfo().setStage(shaderStage).setModule(*module).setPName("main"));
        }

        RDNT_ASSERT(!shaderStages.empty(), "Shaders aren't compiled!");
        return shaderStages;
    }

    void GfxShader::Invalidate() noexcept
    {
        if (TryLoadCache()) return;

        using Slang::ComPtr;
        constexpr auto diagnoseSlangBlob = [](slang::IBlob* diagnosticsBlob) noexcept
        {
            if (diagnosticsBlob) LOG_ERROR("{}", (const char*)diagnosticsBlob->getBufferPointer());
        };

        ComPtr<slang::IGlobalSession> slangGlobalSession;
        auto slangResult = slang::createGlobalSession(slangGlobalSession.writeRef());
        RDNT_ASSERT(SLANG_SUCCEEDED(slangResult), "SLANG: Failed to create global session!");

        const slang::TargetDesc targetDesc = {
            .format  = SLANG_SPIRV,
            .profile = slangGlobalSession->findProfile("sm_6_7" /*"glsl460"*/),
            .flags   = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY,
#if RDNT_DEBUG
            .floatingPointMode = SLANG_FLOATING_POINT_MODE_DEFAULT,
#else
            .floatingPointMode = SLANG_FLOATING_POINT_MODE_FAST,
#endif
            .forceGLSLScalarBufferLayout = true
        };

        std::vector<slang::CompilerOptionEntry> compileOptions = {};
        compileOptions.emplace_back(slang::CompilerOptionName::Capability,
                                    slang::CompilerOptionValue{.intValue0 = slangGlobalSession->findCapability("spirv_1_6")});
        compileOptions.emplace_back(slang::CompilerOptionName::DisableWarning,
                                    slang::CompilerOptionValue{.kind         = slang::CompilerOptionValueKind::String,
                                                               .stringValue0 = "39001"});  // NOTE: vulkan bindings aliasing
        compileOptions.emplace_back(slang::CompilerOptionName::DisableWarning,
                                    slang::CompilerOptionValue{.kind         = slang::CompilerOptionValueKind::String,
                                                               .stringValue0 = "41012"});  // NOTE: spvSparseResidency

        if constexpr (RDNT_DEBUG)
        {
            compileOptions.emplace_back(slang::CompilerOptionName::Optimization,
                                        slang::CompilerOptionValue{.intValue0 = SLANG_OPTIMIZATION_LEVEL_NONE});
            //compileOptions.emplace_back(slang::CompilerOptionName::DebugInformation,
            //                            slang::CompilerOptionValue{.intValue0 = SLANG_DEBUG_INFO_LEVEL_MAXIMAL});
            //compileOptions.emplace_back(
            //    slang::CompilerOptionName::DebugInformationFormat,
            //    slang::CompilerOptionValue{.intValue0 = SLANG_DEBUG_INFO_FORMAT_DEFAULT /*SLANG_DEBUG_INFO_FORMAT_C7*/});
            // compileOptions.emplace_back(slang::CompilerOptionName::DumpIntermediates, slang::CompilerOptionValue{.intValue0 = 1});
        }
        else
        {
            compileOptions.emplace_back(slang::CompilerOptionName::Optimization,
                                        slang::CompilerOptionValue{.intValue0 = SLANG_OPTIMIZATION_LEVEL_MAXIMAL});
        }

        const slang::SessionDesc sessionDesc = {.targets                  = &targetDesc,
                                                .targetCount              = 1,
                                                .defaultMatrixLayoutMode  = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
                                                .compilerOptionEntries    = compileOptions.data(),
                                                .compilerOptionEntryCount = static_cast<u32>(compileOptions.size())};

        ComPtr<slang::ISession> localSession;
        slangResult = slangGlobalSession->createSession(sessionDesc, localSession.writeRef());
        RDNT_ASSERT(SLANG_SUCCEEDED(slangResult), "SLANG: Failed to create local session!");

        ComPtr<slang::IModule> slangModule;
        {
            ComPtr<slang::IBlob> diagnosticBlob;
            slangModule = localSession->loadModule(m_Description.Path.data(), diagnosticBlob.writeRef());
            diagnoseSlangBlob(diagnosticBlob);
            RDNT_ASSERT(slangModule, "SLANG: Failed to load slang shader!");
        }

        for (u32 i{}; i < slangModule->getDefinedEntryPointCount(); ++i)
        {
            std::vector<slang::IComponentType*> shaderComponents{slangModule};

            ComPtr<slang::IEntryPoint> entryPoint;
            slangResult = slangModule->getDefinedEntryPoint(i, entryPoint.writeRef());
            RDNT_ASSERT(SLANG_SUCCEEDED(slangResult) && entryPoint, "Failed to retrieve entry point [{}] from shader: {}", i,
                        m_Description.Path);

            auto entryPointLayout = entryPoint->getLayout();
            RDNT_ASSERT(entryPointLayout, "SLANG: EntryPointLayout isn't valid!");

            auto reflectedEntryPoint = entryPointLayout->getEntryPointByIndex(0);
            RDNT_ASSERT(reflectedEntryPoint, "SLANG: ReflectedEntryPoint isn't valid!");

            const auto shaderStageVK = SlangUtils::SlangShaderStageToVulkan(reflectedEntryPoint->getStage());
            if (m_ModuleMap.contains(shaderStageVK)) continue;

            shaderComponents.emplace_back(entryPoint);
            ComPtr<slang::IComponentType> composedProgram;
            {
                ComPtr<slang::IBlob> diagnosticBlob;
                slangResult = localSession->createCompositeComponentType(shaderComponents.data(), shaderComponents.size(),
                                                                         composedProgram.writeRef(), diagnosticBlob.writeRef());
                diagnoseSlangBlob(diagnosticBlob);
                RDNT_ASSERT(SLANG_SUCCEEDED(slangResult), "SLANG: Failed to compose shader program!");
            }

            ComPtr<slang::IBlob> spirvCode;
            {
                ComPtr<slang::IBlob> diagnosticBlob;
                slangResult = composedProgram->getEntryPointCode(0, 0, spirvCode.writeRef(), diagnosticBlob.writeRef());
                diagnoseSlangBlob(diagnosticBlob);
                RDNT_ASSERT(SLANG_SUCCEEDED(slangResult), "SLANG: Failed to compile shader program!");
            }

            m_ModuleMap.emplace(shaderStageVK, m_Device->GetLogicalDevice()->createShaderModuleUnique(
                                                   vk::ShaderModuleCreateInfo()
                                                       .setPCode(static_cast<const u32*>(spirvCode->getBufferPointer()))
                                                       .setCodeSize(spirvCode->getBufferSize())));

            const auto GetStrippedShaderNameFunc = [](const std::string& shaderPath, const std::string& shaderStage) -> std::string
            {
                const std::string suffix(".slang");
                const u64 pos = shaderPath.rfind(suffix);  // Find the last occurrence of ".slang".
                RDNT_ASSERT(pos != std::string::npos, "Shader path doesn't contain <.slang>!");

                // Search for the previous slash to isolate the filename.
                const u64 lastSlashIndex = shaderPath.rfind('/', pos);
                if (lastSlashIndex != std::string::npos)  // Get filename with .slang
                    return shaderPath.substr(lastSlashIndex + 1) + "." + shaderStage;
                else  // If no slash is found, return the whole substring including .slang
                    return shaderPath.substr(0, pos + suffix.length()) + "." + shaderStage;
            };

            const auto strippedShaderName =
                std::string(s_ShaderCacheDir) + GetStrippedShaderNameFunc(m_Description.Path, vk::to_string(shaderStageVK));
            const auto shaderNameSpv = strippedShaderName + ".spv";

            // Save raw SPIR-V shader cache.
            const auto elementsNum = spirvCode->getBufferSize() / sizeof(u32);
            std::vector<u32> cache(elementsNum);

            std::memcpy(cache.data(), spirvCode->getBufferPointer(), spirvCode->getBufferSize());
            CoreUtils::SaveData(shaderNameSpv, cache);

            // Save last write time used for hot-reloading.
            const auto lastWriteTime  = static_cast<u32>(std::filesystem::last_write_time(m_Description.Path).time_since_epoch().count());
            const auto shaderNameMeta = strippedShaderName + ".meta";
            CoreUtils::SaveData(shaderNameMeta, &lastWriteTime, sizeof(lastWriteTime));
        }
    }

    bool GfxShader::TryLoadCache() noexcept
    {
        RDNT_ASSERT(m_Description.Path.ends_with(".slang"), "Shader name doesn't contain <.slang> in the end!");
        RDNT_ASSERT(!m_Description.Path.empty(), "Shader path is invalid!");

        if (!std::filesystem::exists(s_ShaderCacheDir)) std::filesystem::create_directory(s_ShaderCacheDir);

        static constexpr std::array<vk::ShaderStageFlagBits, 16> shaderStagesVK = {vk::ShaderStageFlagBits::eVertex,
                                                                                   vk::ShaderStageFlagBits::eTessellationControl,
                                                                                   vk::ShaderStageFlagBits::eTessellationEvaluation,
                                                                                   vk::ShaderStageFlagBits::eGeometry,
                                                                                   vk::ShaderStageFlagBits::eFragment,
                                                                                   vk::ShaderStageFlagBits::eCompute,
                                                                                   vk::ShaderStageFlagBits::eRaygenKHR,
                                                                                   vk::ShaderStageFlagBits::eAnyHitKHR,
                                                                                   vk::ShaderStageFlagBits::eClosestHitKHR,
                                                                                   vk::ShaderStageFlagBits::eMissKHR,
                                                                                   vk::ShaderStageFlagBits::eIntersectionKHR,
                                                                                   vk::ShaderStageFlagBits::eCallableKHR,
                                                                                   vk::ShaderStageFlagBits::eTaskEXT,
                                                                                   vk::ShaderStageFlagBits::eMeshEXT,
                                                                                   vk::ShaderStageFlagBits::eSubpassShadingHUAWEI,
                                                                                   vk::ShaderStageFlagBits::eClusterCullingHUAWEI};

        bool bEverythingLoaded{true};
        for (const auto shaderStageVK : shaderStagesVK)
        {
            const auto GetCachedShaderNameFunc = [](const std::string& shaderPath, const std::string& shaderStage) -> std::string
            {
                const std::string suffix(".slang");
                const u64 pos = shaderPath.rfind(suffix);  // Find the last occurrence of ".slang".
                RDNT_ASSERT(pos != std::string::npos, "Shader path doesn't contain <.slang>!");

                // Search for the previous slash to isolate the filename.
                const u64 lastSlashIndex = shaderPath.rfind('/', pos);
                if (lastSlashIndex != std::string::npos)  // Get filename with .slang
                    return shaderPath.substr(lastSlashIndex + 1) + "." + shaderStage;
                else  // If no slash is found, return the whole substring including .slang
                    return shaderPath.substr(0, pos + suffix.length()) + "." + shaderStage;
            };

            const auto strippedShaderName =
                std::string(s_ShaderCacheDir) + GetCachedShaderNameFunc(m_Description.Path, vk::to_string(shaderStageVK));

            const auto shaderNameMeta  = strippedShaderName + ".meta";
            const auto shaderCacheName = strippedShaderName + ".spv";
            if (!std::filesystem::exists(shaderCacheName) || !std::filesystem::exists(shaderNameMeta)) continue;

            const std::vector<u32> cacheData = CoreUtils::LoadData<u32>(shaderCacheName);
            RDNT_ASSERT(!cacheData.empty(), "Failed to load shader cache!");

            const std::vector<u32> metaData = CoreUtils::LoadData<u32>(shaderNameMeta);
            RDNT_ASSERT(!metaData.empty(), "Failed to load shader metadata!");

            // Get the last modified time of the whole slang file!
            const auto lastWriteTime = static_cast<u32>(std::filesystem::last_write_time(m_Description.Path).time_since_epoch().count());
            if (metaData[0] != lastWriteTime)
            {
                bEverythingLoaded = false;
                continue;
            }

            m_ModuleMap.emplace(
                shaderStageVK,
                m_Device->GetLogicalDevice()->createShaderModuleUnique(
                    vk::ShaderModuleCreateInfo().setPCode(cacheData.data()).setCodeSize(cacheData.size() * sizeof(cacheData[0]))));
        }

        return bEverythingLoaded && !m_ModuleMap.empty();
    }

}  // namespace Radiant
