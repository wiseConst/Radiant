#include <Core/Application.hpp>

// KISS

int32_t main(uint32_t argc, char** argv) noexcept
{
    using namespace Radiant;

    const ApplicationDescription appDesc = {.Name = "Radiant", .CmdArgs{.Argc = argc, .Argv = argv}, .FPSLimit = 90};

    auto app = Application::Create(appDesc);
    app->Run();

    return 0;
}
