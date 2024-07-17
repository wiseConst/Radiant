#include <pch.h>
#include "GfxShader.hpp"

#include <slang.h>
#include <slang-com-ptr.h>

namespace Radiant
{

    GfxShader::GfxShader(const Unique<GfxDevice>& device, const GfxShaderDescription& shaderDesc) noexcept
        : m_Device(device), m_Description(shaderDesc)
    {
        Init();
    }

    void GfxShader::Init() noexcept
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
    }

}  // namespace Radiant
