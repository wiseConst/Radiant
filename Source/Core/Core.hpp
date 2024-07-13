#pragma once

#include <Core/CoreTypes.hpp>
#include <Core/Log.hpp>
#include <Core/Math.hpp>
#include <Core/PlatformDetection.hpp>

#include <thread>  // For threadpool impl
#include <future>
#include <mutex>
#include <deque>

namespace Radiant
{

    static constexpr const char* s_ENGINE_NAME = "RADIANT";

#ifdef RDNT_DEBUG
#define RDNT_ASSERT(cond, ...)                                                                                                             \
    if (!(cond))                                                                                                                           \
    {                                                                                                                                      \
        LOG_ERROR(__VA_ARGS__);                                                                                                            \
        std::terminate();                                                                                                                  \
    }
#endif

#ifdef RDNT_RELEASE
#define RDNT_ASSERT(cond, ...)                                                                                                             \
    if (!(cond))                                                                                                                           \
    {                                                                                                                                      \
        LOG_ERROR(__VA_ARGS__);                                                                                                            \
        std::terminate();                                                                                                                  \
    }
#endif

    class ThreadPool final
    {
      public:
        ThreadPool() noexcept { m_Workers.resize(std::thread::hardware_concurrency()); }
        ThreadPool(const std::uint16_t workerCount) noexcept
        {
            RDNT_ASSERT(workerCount > 0, "Worker count should be > 0!");
            m_Workers.resize(workerCount);
        }
        ~ThreadPool() noexcept
        {
            {
                std::scoped_lock lock(m_Mtx);
                m_bShutdownRequested = true;
            }
            m_Cv.notify_all();
        }

      private:
        std::deque<std::move_only_function<void()>> m_WorkQueue;
        std::vector<std::jthread> m_Workers;
        std::condition_variable m_Cv{};
        std::mutex m_Mtx{};
        bool m_bShutdownRequested{false};
    };

}  // namespace Radiant
