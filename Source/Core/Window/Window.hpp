#pragma once

#include <Core/Core.hpp>

namespace TestBed
{
    struct WindowSpecification
    {
        std::string Name{s_DEFAULT_STRING};
        glm::uvec2 Extent{0};
    };

    class Window : public Handle<Window>
    {
      public:
        static Window Create(const WindowSpecification& windowSpec);
        void Destroy();
    };

}  // namespace TestBed
