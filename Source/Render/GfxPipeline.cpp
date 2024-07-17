#include <pch.h>
#include "GfxPipeline.hpp"

// TODO: remove
#include <slang.h>
#include <slang-com-ptr.h>

// NOTE: Used only for base viewport construction.
#include <Core/Application.hpp>
#include <Core/Window/GLFWWindow.hpp>

namespace Radiant
{

    void GfxPipeline::Invalidate() noexcept
    {
        RDNT_ASSERT(!std::holds_alternative<std::monostate>(m_Description.PipelineOptions), "PipelineOptions aren't setup!");

        if (const auto* gpo = std::get_if<GfxGraphicsPipelineOptions>(&m_Description.PipelineOptions); gpo)
        {
            using Slang::ComPtr;

            // Many Slang API functions return detailed diagnostic information
            // (error messages, warnings, etc.) as a "blob" of data, or return
            // a null blob pointer instead if there were no issues.
            //
            // For convenience, we define a subroutine that will dump the information
            // in a diagnostic blob if one is produced, and skip it otherwise.
            //
            const auto diagnoseSlangBlob = [](slang::IBlob* diagnosticsBlob)
            {
                if (diagnosticsBlob)
                {
                    LOG_ERROR("{}", (const char*)diagnosticsBlob->getBufferPointer());
                }
            };

            // First we need to create slang global session with work with the Slang API.
            ComPtr<slang::IGlobalSession> slangGlobalSession;
            auto slangResult = slang::createGlobalSession(slangGlobalSession.writeRef());

            // Next we create a compilation session to generate SPIRV code from Slang source.
            slang::SessionDesc sessionDesc = {};
            slang::TargetDesc targetDesc   = {};
            targetDesc.format              = SLANG_SPIRV;
            targetDesc.profile             = slangGlobalSession->findProfile("spirv_1_6");
            targetDesc.flags               = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

            sessionDesc.targets     = &targetDesc;
            sessionDesc.targetCount = 1;

            ComPtr<slang::ISession> session;
            slangResult = slangGlobalSession->createSession(sessionDesc, session.writeRef());

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
            slang::IModule* slangModule = nullptr;
            {
                ComPtr<slang::IBlob> diagnosticBlob;
                slangModule = session->loadModule("../Assets/Shaders/shaders.slang", diagnosticBlob.writeRef());
                diagnoseSlangBlob(diagnosticBlob);
                RDNT_ASSERT(slangModule, "Failed to load slang shader!");
            }

            // Loading the `hello-world` module will compile and check all the shader code in it,
            // including the shader entry points we want to use. Now that the module is loaded
            // we can look up those entry points by name.
            //
            // Note: If you are using this `loadModule` approach to load your shader code it is
            // important to tag your entry point functions with the `[shader("...")]` attribute
            // (e.g., `[shader("compute")] void computeMain(...)`). Without that information there
            // is no umambiguous way for the compiler to know which functions represent entry
            // points when it parses your code via `loadModule()`.
            //
            ComPtr<slang::IEntryPoint> vsEntryPoint, fsEntryPoint;
            slangResult = slangModule->findEntryPointByName("vertexMain", vsEntryPoint.writeRef());
            slangResult = slangModule->findEntryPointByName("fragmentMain", fsEntryPoint.writeRef());

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
            const std::vector<slang::IComponentType*> vsComponentTypes{slangModule, vsEntryPoint};
            const std::vector<slang::IComponentType*> fsComponentTypes{slangModule, fsEntryPoint};

            // Actually creating the composite component type is a single operation
            // on the Slang session, but the operation could potentially fail if
            // something about the composite was invalid (e.g., you are trying to
            // combine multiple copies of the same module), so we need to deal
            // with the possibility of diagnostic output.
            //
            ComPtr<slang::IComponentType> vsComposedProgram;
            {
                ComPtr<slang::IBlob> diagnosticBlob;
                slangResult = session->createCompositeComponentType(vsComponentTypes.data(), vsComponentTypes.size(),
                                                                    vsComposedProgram.writeRef(), diagnosticBlob.writeRef());
                diagnoseSlangBlob(diagnosticBlob);
            }

            auto slangReflection = vsComposedProgram->getLayout();

            // Get other shader types that we will use for creating shader objects.

            ComPtr<slang::IComponentType> fsComposedProgram;
            {
                ComPtr<slang::IBlob> diagnosticBlob;
                slangResult = session->createCompositeComponentType(fsComponentTypes.data(), fsComponentTypes.size(),
                                                                    fsComposedProgram.writeRef(), diagnosticBlob.writeRef());
                diagnoseSlangBlob(diagnosticBlob);
            }

            // Now we can call `composedProgram->getEntryPointCode()` to retrieve the
            // compiled SPIRV code that we will use to create a vulkan compute pipeline.
            // This will trigger the final Slang compilation and spirv code generation.
            ComPtr<slang::IBlob> vsSpirvCode;
            {
                ComPtr<slang::IBlob> diagnosticBlob;
                slangResult = vsComposedProgram->getEntryPointCode(0, 0, vsSpirvCode.writeRef(), diagnosticBlob.writeRef());
                diagnoseSlangBlob(diagnosticBlob);
            }

            auto& logicalDevice = m_Device->GetLogicalDevice();
            vk::UniqueShaderModule vsModule =
                logicalDevice->createShaderModuleUnique(vk::ShaderModuleCreateInfo()
                                                            .setPCode(static_cast<const uint32_t*>(vsSpirvCode->getBufferPointer()))
                                                            .setCodeSize(vsSpirvCode->getBufferSize()));

            ComPtr<slang::IBlob> fsSpirvCode;
            {
                ComPtr<slang::IBlob> diagnosticBlob;
                slangResult = fsComposedProgram->getEntryPointCode(0, 0, fsSpirvCode.writeRef(), diagnosticBlob.writeRef());
                diagnoseSlangBlob(diagnosticBlob);
            }

            vk::UniqueShaderModule fsModule =
                logicalDevice->createShaderModuleUnique(vk::ShaderModuleCreateInfo()
                                                            .setPCode(static_cast<const uint32_t*>(fsSpirvCode->getBufferPointer()))
                                                            .setCodeSize(fsSpirvCode->getBufferSize()));

            const std::vector<vk::PipelineShaderStageCreateInfo> shaderStages{
                vk::PipelineShaderStageCreateInfo().setStage(vk::ShaderStageFlagBits::eVertex).setModule(*vsModule).setPName("main"),
                vk::PipelineShaderStageCreateInfo().setStage(vk::ShaderStageFlagBits::eFragment).setModule(*fsModule).setPName("main")};

            const auto dynamicRenderingInfo = vk::PipelineRenderingCreateInfo().setColorAttachmentFormats(gpo->RenderingFormats);
            const auto inputAssemblyStateCI =
                vk::PipelineInputAssemblyStateCreateInfo().setTopology(gpo->PrimitiveTopology).setPrimitiveRestartEnable(vk::False);
            const auto vtxInputStateCI     = vk::PipelineVertexInputStateCreateInfo();
            const auto depthStencilStateCI = vk::PipelineDepthStencilStateCreateInfo()
                                                 .setBack(gpo->Back)
                                                 .setFront(gpo->Front)
                                                 .setStencilTestEnable(gpo->bStencilTest)
                                                 .setDepthBoundsTestEnable(vk::True)
                                                 .setDepthCompareOp(gpo->DepthCompareOp)
                                                 .setDepthTestEnable(gpo->bDepthTest)
                                                 .setDepthWriteEnable(gpo->bDepthWrite)
                                                 .setMaxDepthBounds(1.0f)
                                                 .setMinDepthBounds(0.f);
            const auto colorBlendAttachment =
                vk::PipelineColorBlendAttachmentState().setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                                                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
            const auto blendStateCI         = vk::PipelineColorBlendStateCreateInfo().setAttachments(colorBlendAttachment);
            const auto rasterizationStateCI = vk::PipelineRasterizationStateCreateInfo()
                                                  .setCullMode(gpo->CullMode)
                                                  .setFrontFace(gpo->FrontFace)
                                                  .setPolygonMode(gpo->PolygonMode)
                                                  .setRasterizerDiscardEnable(vk::False)
                                                  .setDepthClampEnable(gpo->bDepthClamp)
                                                  .setLineWidth(1.0f);

            // TODO:
            const auto msaaStateCI = vk::PipelineMultisampleStateCreateInfo().setRasterizationSamples(vk::SampleCountFlagBits::e1);

            const auto& windowExtent = Application::Get().GetMainWindow()->GetDescription().Extent;
            const auto scissor       = vk::Rect2D().setExtent(vk::Extent2D().setWidth(windowExtent.x).setHeight(windowExtent.y));
            const auto viewport      = vk::Viewport()
                                      .setMinDepth(0.0f)
                                      .setMaxDepth(1.0f)
                                      .setWidth(static_cast<float>(windowExtent.x))
                                      .setHeight(static_cast<float>(windowExtent.y));
            const auto viewportStateCI = vk::PipelineViewportStateCreateInfo().setScissors(scissor).setViewports(viewport);

            // NOTE: Unfortunately vulkan.hpp doesn't recognize ankerl's unordered set.
            std::vector<vk::DynamicState> dynamicStates;
            for (const auto& dynamicState : gpo->DynamicStates)
                dynamicStates.emplace_back(dynamicState);

            const auto dynamicStateCI = vk::PipelineDynamicStateCreateInfo().setDynamicStates(dynamicStates);
            auto [result, pipeline]   = logicalDevice->createGraphicsPipelineUnique(
                m_Device->GetPipelineCache(), vk::GraphicsPipelineCreateInfo()
                                                  .setLayout(*m_BindlessPipelineLayout)
                                                  .setStages(shaderStages)
                                                  .setPNext(&dynamicRenderingInfo)
                                                  .setPInputAssemblyState(gpo->bMeshShading ? nullptr : &inputAssemblyStateCI)
                                                  .setPVertexInputState(gpo->bMeshShading ? nullptr : &vtxInputStateCI)
                                                  .setPDepthStencilState(&depthStencilStateCI)
                                                  .setPViewportState(&viewportStateCI)
                                                  .setPColorBlendState(&blendStateCI)
                                                  .setPRasterizationState(&rasterizationStateCI)
                                                  .setPMultisampleState(&msaaStateCI)
                                                  .setPDynamicState(&dynamicStateCI));
            RDNT_ASSERT(result == vk::Result::eSuccess, "Failed to create pipeline!");

            m_Handle = std::move(pipeline);
        }
        else if (const auto* cpo = std::get_if<GfxComputePipelineOptions>(&m_Description.PipelineOptions); cpo)
        {
        }
        else if (const auto* rtpo = std::get_if<GfxRayTracingPipelineOptions>(&m_Description.PipelineOptions); rtpo)
        {
        }
        else
            RDNT_ASSERT(false, "This shouldn't happen! {}", __FUNCTION__);
    }

}  // namespace Radiant
