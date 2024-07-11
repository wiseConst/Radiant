#pragma once

#include <Core/CoreTypes.hpp>
#include <Core/Log.hpp>
#include <Core/Math.hpp>

#include <chrono>

namespace Radiant
{

#ifdef RDNT_DEBUG
#define RDNT_ASSERT(cond, ...)                                                                                                             \
    {                                                                                                                                      \
        LOG_ERROR(__VA_ARGS__);                                                                                                            \
        std::terminate();                                                                                                                  \
    }
#endif

#ifdef RDNT_RELEASE
#define RDNT_ASSERT(cond, ...)                                                                                                             \
    {                                                                                                                                      \
        LOG_ERROR(__VA_ARGS__);                                                                                                            \
        std::terminate();                                                                                                                  \
    }
#endif

    class Timer final
    {
      public:
        Timer() noexcept : m_StartTime(Now()) {}
        ~Timer() = default;

        NODISCARD FORCEINLINE double GetElapsedMilliseconds() const noexcept
        {
            const auto elapsed = std::chrono::duration<double, std::milli>(Now() - m_StartTime);
            return elapsed.count();
        }

        NODISCARD FORCEINLINE double GetElapsedSeconds() const noexcept
        {
            const auto elapsed = std::chrono::duration<double>(Now() - m_StartTime);
            return elapsed.count();
        }

        FORCEINLINE void Reset() noexcept { m_StartTime = Now(); }
        NODISCARD FORCEINLINE static std::chrono::high_resolution_clock::time_point Now() noexcept
        {
            return std::chrono::high_resolution_clock::now();
        }

      private:
        MAYBE_UNUSED std::chrono::time_point<std::chrono::high_resolution_clock> m_StartTime = {};
    };

}  // namespace Radiant
