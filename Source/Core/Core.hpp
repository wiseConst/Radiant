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
        __debugbreak();                                                                                                                    \
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

    struct WindowResizeData
    {
        glm::uvec2 Dimensions;
    };

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

    namespace CoreUtils
    {

        template <typename T> static std::vector<T> LoadData(const std::string_view& dataPath) noexcept
        {
            static_assert(std::is_trivial<T>::value, "T must be a trivial type");
            RDNT_ASSERT(!dataPath.empty(), "Data path is invalid!");

            std::ifstream input(dataPath.data(), std::ios::in | std::ios::binary | std::ios::ate);
            RDNT_ASSERT(input.is_open(), "Failed to open: {}", dataPath);

            const auto fileSize = input.tellg();
            input.seekg(0, std::ios::beg);

            std::vector<T> loadedData(fileSize / sizeof(T));
            input.read(reinterpret_cast<char*>(loadedData.data()), fileSize);

            input.close();
            return loadedData;
        }

        template <typename T> static void SaveData(const std::string_view& dataPath, const std::vector<T>& data) noexcept
        {
            static_assert(std::is_trivial<T>::value, "T must be a trivial type");
            RDNT_ASSERT(!dataPath.empty(), "Data path is invalid!");

            std::ofstream output(dataPath.data(), std::ios::out | std::ios::binary | std::ios::trunc);
            RDNT_ASSERT(output.is_open(), "Failed to open: {}", dataPath);

            output.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(data[0]));
            output.close();
        }

    }  // namespace CoreUtils

}  // namespace Radiant
