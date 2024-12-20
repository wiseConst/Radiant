#include <Core/Application.hpp>

Radiant::i32 main(Radiant::u32 argc, char** argv) noexcept
{
    auto app = Radiant::Application::Create(Radiant::ApplicationDescription{.Name = "Radiant", .CmdArgs{.Argc = argc, .Argv = argv}});
    app->Run();

    return 0;
}
