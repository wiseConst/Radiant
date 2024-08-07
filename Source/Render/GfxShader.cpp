#include <pch.h>
#include "GfxShader.hpp"

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

    // NOTE: Current method(2)
    // TODO: Test what's faster and better:
    // (1) I can compile shaders by composing huge slang shader module and then pulling out needed entryPointCode(i);
    // (2) I can compile single shader module and entryPointCode(0)
    void GfxShader::Invalidate() noexcept
    {
        RDNT_ASSERT(!m_Description.Path.empty(), "Shader path is invalid!");

        using Slang::ComPtr;

        // Many Slang API functions return detailed diagnostic information
        // (error messages, warnings, etc.) as a "blob" of data, or return
        // a null blob pointer instead if there were no issues.
        //
        // For convenience, we define a subroutine that will dump the information
        // in a diagnostic blob if one is produced, and skip it otherwise.
        //
        const auto diagnoseSlangBlob = [](slang::IBlob* diagnosticsBlob) noexcept
        {
            if (diagnosticsBlob) LOG_ERROR("{}", (const char*)diagnosticsBlob->getBufferPointer());
        };

        // First we need to create slang global session with work with the Slang API.
        ComPtr<slang::IGlobalSession> slangGlobalSession;
        auto slangResult = slang::createGlobalSession(slangGlobalSession.writeRef());
        RDNT_ASSERT(SLANG_SUCCEEDED(slangResult), "SLANG: Failed to create global session!");

        // Next we create a compilation session to generate SPIRV code from Slang source.
        const slang::TargetDesc targetDesc = {.format                      = SLANG_SPIRV,
                                              .profile                     = slangGlobalSession->findProfile("glsl_460"),
                                              .flags                       = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY,
                                              .floatingPointMode           = SLANG_FLOATING_POINT_MODE_FAST,
                                              .forceGLSLScalarBufferLayout = true};

        std::vector<slang::CompilerOptionEntry> compileOptions = {};

        {
            auto& compileOption           = compileOptions.emplace_back(slang::CompilerOptionName::GLSLForceScalarLayout);
            compileOption.value.intValue0 = 1;
        }

        {
            auto& compileOption              = compileOptions.emplace_back(slang::CompilerOptionName::DisableWarning);
            compileOption.value.kind         = slang::CompilerOptionValueKind::String;
            compileOption.value.stringValue0 = "39001";  // NOTE: vulkan bindings aliasing
        }

        if constexpr (RDNT_DEBUG)
        {
            auto& compileOption           = compileOptions.emplace_back(slang::CompilerOptionName::Optimization);
            compileOption.value.kind      = slang::CompilerOptionValueKind::Int;
            compileOption.value.intValue0 = SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_DEFAULT;
        }
        else
        {
            auto& compileOption           = compileOptions.emplace_back(slang::CompilerOptionName::Optimization);
            compileOption.value.kind      = slang::CompilerOptionValueKind::Int;
            compileOption.value.intValue0 = SlangOptimizationLevel::SLANG_OPTIMIZATION_LEVEL_MAXIMAL;
        }

        const slang::SessionDesc sessionDesc = {.targets                  = &targetDesc,
                                                .targetCount              = 1,
                                                .defaultMatrixLayoutMode  = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
                                                .compilerOptionEntries    = compileOptions.data(),
                                                .compilerOptionEntryCount = static_cast<u32>(compileOptions.size())};

        ComPtr<slang::ISession> localSession;
        slangResult = slangGlobalSession->createSession(sessionDesc, localSession.writeRef());
        RDNT_ASSERT(SLANG_SUCCEEDED(slangResult), "SLANG: Failed to create local session!");

        // Once the session has been obtained, we can start loading code into it.
        //
        // The simplest way to load code is by calling `loadModule` with the name of a Slang
        // module. A call to `loadModule("hello-world")` will behave more or less as if you
        // wrote:
        //
        //      import hello_world;
        //
        // In a Slang shader file. The compiler will use its search paths to try to locate
        // `hello-world.slang`, then compile and load that file. If a matching module had
        // already been loaded previously, that would be used directly.
        //
        // Loading the `hello-world` module will compile and check all the shader code in it,
        // including the shader entry points we want to use. Now that the module is loaded
        // we can look up those entry points by name.
        //
        // Note: If you are using this `loadModule` approach to load your shader code it is
        // important to tag your entry point functions with the `[shader("...")]` attribute
        // (e.g., `[shader("compute")] void computeMain(...)`). Without that information there
        // is no umambiguous way for the compiler to know which functions represent entry
        // points when it parses your code via `loadModule()`.
        ComPtr<slang::IModule> slangModule;
        {
            ComPtr<slang::IBlob> diagnosticBlob;
            slangModule = localSession->loadModule(m_Description.Path.data(), diagnosticBlob.writeRef());
            diagnoseSlangBlob(diagnosticBlob);
            RDNT_ASSERT(slangModule, "SLANG: Failed to load slang shader!");
        }

        for (u32 i{}; i < slangModule->getDefinedEntryPointCount(); ++i)
        {
            // At this point we have a few different Slang API objects that represent
            // pieces of our code: `module`, `vertexEntryPoint`, and `fragmentEntryPoint`.
            //
            // A single Slang module could contain many different entry points (e.g.,
            // four vertex entry points, three fragment entry points, and two compute
            // shaders), and before we try to generate output code for our target API
            // we need to identify which entry points we plan to use together.
            //
            // Modules and entry points are both examples of *component types* in the
            // Slang API. The API also provides a way to build a *composite* out of
            // other pieces, and that is what we are going to do with our module
            // and entry points.
            //
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

            // Actually creating the composite component type is a single operation
            // on the Slang session, but the operation could potentially fail if
            // something about the composite was invalid (e.g., you are trying to
            // combine multiple copies of the same module), so we need to deal
            // with the possibility of diagnostic output.
            ComPtr<slang::IComponentType> composedProgram;
            {
                ComPtr<slang::IBlob> diagnosticBlob;
                slangResult = localSession->createCompositeComponentType(shaderComponents.data(), shaderComponents.size(),
                                                                         composedProgram.writeRef(), diagnosticBlob.writeRef());
                diagnoseSlangBlob(diagnosticBlob);
                RDNT_ASSERT(SLANG_SUCCEEDED(slangResult), "SLANG: Failed to compose shader program!");
            }

            // TODO: Reflection and push constants size assertion on 128 bytes!
#if 0
            auto composedProgramLayout = composedProgram->getLayout();
            RDNT_ASSERT(composedProgramLayout, "SLANG: ComposedProgramLayout isn't valid!"); 

            for (u32 i{}; i < composedProgramLayout->getParameterCount(); ++i)
            {
                slang::VariableLayoutReflection* shaderParam = composedProgramLayout->getParameterByIndex(i);
                RDNT_ASSERT(shaderParam, "SLANG: ShaderParam isn't valid!");

                if (shaderParam->getCategory() == slang::ParameterCategory::PushConstantBuffer)
                {
                    // query the number of bytes of constant-buffer storage used by a type layout
                    slang::TypeLayoutReflection* typeLayout = shaderParam->getTypeLayout();

                    size_t sizeInBytes = typeLayout->getSize(SLANG_PARAMETER_CATEGORY_PUSH_CONSTANT_BUFFER);
                    //      RDNT_ASSERT(sizeInBytes == 128, "PushConstantBuffer should be 128 bytes size for bindless purposes!");

                    if (typeLayout->getKind() == slang::TypeReflection::Kind::ConstantBuffer)
                    {
                        for (u32 ff{}; ff < typeLayout->getFieldCount(); ff++)
                        {
                            slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(ff);
                            RDNT_ASSERT(field, "SLANG: field isn't valid!");

                            LOG_INFO("{}", field->getName());
                        }
                    }
                }
                LOG_INFO("{}", shaderParam->getName());
            }
#endif

            // Now we can call `composedProgram->getEntryPointCode()` to retrieve the
            // compiled SPIRV code that we will use to create a vulkan compute pipeline.
            // This will trigger the final Slang compilation and spirv code generation.
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
