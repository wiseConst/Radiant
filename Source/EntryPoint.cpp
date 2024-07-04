#include <pch.h>

#include <Core/Window/Window.hpp>

int32_t main(int32_t argc, char** argv)
{
    TestBed::Log::Init();
    TB_INFO("{}", __FUNCTION__);

    using namespace TestBed;
    //  Window window = Window::Create({.Name ="TestBed",})

    return 0;
}
