#include <Core/Application.hpp>

// SoundSystem
// RenderSystem
// SceneSystem

int32_t main(uint32_t argc, char** argv) noexcept
{
    Radiant::ApplicationDescription appDesc = {
        .Name = "Radiant", .CmdArgs{.Argc = argc, .Argv = argv}, .FPSLimit = 30, .RHI = Radiant::ERHI::RHI_VULKAN};

    auto app = Radiant::Application::Create(appDesc);
    app->Run();

    return 0;
}
