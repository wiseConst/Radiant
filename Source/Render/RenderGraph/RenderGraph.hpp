#pragma once

#include <Core/Core.hpp>
#include <Render/CoreDefines.hpp>

namespace Radiant
{

    class RenderGraph final : private Uncopyable, private Unmovable
    {
      public:
        explicit RenderGraph(const std::string_view& name) noexcept : m_Name(name) {}
        ~RenderGraph() noexcept { Shutdown(); }

      private:
        std::string m_Name{s_DEFAULT_STRING};

        constexpr RenderGraph() noexcept = delete;
        void Shutdown() noexcept {}
    };

}  // namespace Radiant
