#include <pch.h>
#include "GfxShader.hpp"

#include <Render/GfxDevice.hpp>

#include <slang.h>
#include <slang-com-ptr.h>

namespace Radiant
{

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

    NODISCARD std::vector<vk::PipelineShaderStageCreateInfo> GfxShader::GetShaderStages() const noexcept
    {
        std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;

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
        RDNT_ASSERT(!m_Description.Path.empty(), "Shader path is invalid!");

        using Slang::ComPtr;
        constexpr auto diagnoseSlangBlob = [](slang::IBlob* diagnosticsBlob) noexcept
        {
            if (diagnosticsBlob) LOG_ERROR("{}", (const char*)diagnosticsBlob->getBufferPointer());
        };

        ComPtr<slang::IGlobalSession> slangGlobalSession;
        auto slangResult = slang::createGlobalSession(slangGlobalSession.writeRef());
        RDNT_ASSERT(SLANG_SUCCEEDED(slangResult), "SLANG: Failed to create global session!");

        const slang::TargetDesc targetDesc = {.format                      = SLANG_SPIRV,
                                              .profile                     = slangGlobalSession->findProfile("glsl_460"),
                                              .flags                       = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY,
                                              .floatingPointMode           = SLANG_FLOATING_POINT_MODE_FAST,
                                              .forceGLSLScalarBufferLayout = true};

        std::vector<slang::CompilerOptionEntry> compileOptions = {};
        compileOptions.emplace_back(slang::CompilerOptionName::Capability,
                                    slang::CompilerOptionValue{.intValue0 = slangGlobalSession->findCapability("spirv_1_6")});
        compileOptions.emplace_back(slang::CompilerOptionName::GLSLForceScalarLayout, slang::CompilerOptionValue{.intValue0 = 1});
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
            // compileOptions.emplace_back(slang::CompilerOptionName::DebugInformation,
            //                             slang::CompilerOptionValue{.intValue0 = SLANG_DEBUG_INFO_LEVEL_MAXIMAL});
            // compileOptions.emplace_back(slang::CompilerOptionName::DebugInformationFormat,
            //                             slang::CompilerOptionValue{.intValue0 = SLANG_DEBUG_INFO_FORMAT_C7});
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

            shaderComponents.emplace_back(entryPoint);

            auto entryPointLayout = entryPoint->getLayout();
            RDNT_ASSERT(entryPointLayout, "SLANG: EntryPointLayout isn't valid!");

            auto reflectedEntryPoint = entryPointLayout->getEntryPointByIndex(0);
            RDNT_ASSERT(reflectedEntryPoint, "SLANG: ReflectedEntryPoint isn't valid!");

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

            m_ModuleMap.emplace(SlangUtils::SlangShaderStageToVulkan(reflectedEntryPoint->getStage()),
                                m_Device->GetLogicalDevice()->createShaderModuleUnique(
                                    vk::ShaderModuleCreateInfo()
                                        .setPCode(static_cast<const uint32_t*>(spirvCode->getBufferPointer()))
                                        .setCodeSize(spirvCode->getBufferSize())));
        }
    }

}  // namespace Radiant
