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

#if _MSC_VER
#define RDNT_DEBUGBREAK __debugbreak()
#else
#define RDNT_DEBUGBREAK
#endif

#if RDNT_DEBUG
#define RDNT_ASSERT(cond, ...)                                                                                                             \
    if (!(cond))                                                                                                                           \
    {                                                                                                                                      \
        LOG_ERROR(__VA_ARGS__);                                                                                                            \
        RDNT_DEBUGBREAK;                                                                                                                   \
        std::terminate();                                                                                                                  \
    }
#endif

#if RDNT_RELEASE
#define RDNT_ASSERT(cond, ...)                                                                                                             \
    if (!(cond))                                                                                                                           \
    {                                                                                                                                      \
        LOG_ERROR(__VA_ARGS__);                                                                                                            \
        std::terminate();                                                                                                                  \
    }
#endif

    struct WindowResizeData final
    {
        glm::uvec2 Dimensions;
    };

    class ThreadPool final : private Uncopyable, private Unmovable
    {
        static constexpr bool s_bSetCPUCoreAffinity = true;

      public:
        ThreadPool() noexcept
        {
            m_Workers.resize(std::thread::hardware_concurrency());
            Init();
        }
        ThreadPool(const u16 workerCount) noexcept
        {
            RDNT_ASSERT(workerCount > 0, "Worker count should be > 0!");
            m_Workers.resize(workerCount);
            Init();
        }
        ~ThreadPool() noexcept
        {
            {
                std::scoped_lock lock(m_Mtx);
                m_bShutdownRequested = true;
            }
            m_Cv.notify_all();
        }

        template <typename Func, typename... Args>
        NODISCARD auto Submit(Func&& func, Args&&... args) noexcept -> std::future<decltype(func(args...))>
        {
            auto task   = std::packaged_task<decltype(func(args...))()>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
            auto future = task.get_future();
            {
                std::scoped_lock lock(m_Mtx);
                m_WorkQueue.emplace_back([movedTask = move(task)]() mutable noexcept { movedTask(); });
            }
            m_Cv.notify_one();
            return future;
        }

      private:
        std::condition_variable m_Cv{};
        std::mutex m_Mtx{};
        std::deque<std::move_only_function<void() noexcept>> m_WorkQueue;
        bool m_bShutdownRequested{false};
        std::vector<std::jthread> m_Workers;

        void Init() noexcept
        {
            // NOTE: Currently thread affinity assigning highly based on that CPU phys cores = CPU logic cores / 2, need to improve further.
            u32 threadIndex = 0;

            LOG_TRACE("Created threadpool with {} workers.", m_Workers.size());
            std::ranges::for_each(m_Workers,
                                  [&](auto& worker)
                                  {
                                      worker = std::jthread(
                                          [&]()
                                          {
                                              while (true)
                                              {
                                                  std::move_only_function<void() noexcept> func;
                                                  {
                                                      std::unique_lock lock(m_Mtx);
                                                      m_Cv.wait(lock,
                                                                [&]() noexcept { return m_bShutdownRequested || !m_WorkQueue.empty(); });
                                                      if (m_bShutdownRequested && m_WorkQueue.empty()) return;

                                                      func = std::move(m_WorkQueue.front());
                                                      m_WorkQueue.pop_front();
                                                  }
                                                  func();
                                              }
                                          });

#if defined(RDNT_WINDOWS)
                                      // Attaching thread to specific CPU
                                      const u32 cpuCoreIndex       = threadIndex / 2;
                                      const HANDLE nativeHandle    = worker.native_handle();
                                      const DWORD_PTR affinityMask = 1ull << cpuCoreIndex;
                                      RDNT_ASSERT(SetThreadAffinityMask(nativeHandle, affinityMask) > 0,
                                                  "Failed to attach the thread to {} CPU core!", cpuCoreIndex);

                                      // Setting high priority to the thread.
                                      // By default, each thread we create is THREAD_PRIORITY_DEFAULT.
                                      // Modifying this could help threads not be overtaken by the operating system by lesser priority
                                      // threads. I've found no way to increase performance with this yet, only decrease it.
                                      RDNT_ASSERT(SetThreadPriority(nativeHandle, THREAD_PRIORITY_HIGHEST) != 0,
                                                  "Failed to set thread priority to THREAD_PRIORITY_HIGHEST");

                                      std::wstringstream wss;
                                      wss << "Worker_Thread_" << threadIndex << "_Core_" << cpuCoreIndex;
                                      RDNT_ASSERT(SUCCEEDED(SetThreadDescription(nativeHandle, wss.str().data())),
                                                  "Failed to set name to thread {}!", threadIndex);

#else
#error Implement thread affinity setup in other systems!
#endif

                                      ++threadIndex;
                                  });
        }
    };

    using PoolID = u64;
    template <typename T> class Pool
    {
      public:
        constexpr Pool() noexcept = default;
        ~Pool() noexcept          = default;

        void Release(const PoolID& poolID) noexcept
        {
            RDNT_ASSERT(poolID < m_Objects.size() && m_bPresentObjects[poolID], "Invalid PoolID!");

            m_bPresentObjects[poolID] = false;
            m_FreeIDs.emplace_back(poolID);
        }

        NODISCARD PoolID Emplace(T&& element) noexcept
        {
            if (m_FreeIDs.empty())
            {
                const PoolID poolID = m_Objects.size();
                m_Objects.emplace_back(std::forward<T>(element));
                m_bPresentObjects.emplace_back(true);
                return poolID;
            }

            const PoolID poolID = m_FreeIDs.back();
            m_FreeIDs.pop_back();
            m_bPresentObjects[poolID] = true;
            m_Objects[poolID]         = std::forward<T>(element);

            return poolID;
        }

        NODISCARD FORCEINLINE T& Get(const PoolID& poolID) noexcept
        {
            RDNT_ASSERT(poolID < m_Objects.size() && m_bPresentObjects[poolID], "Object is not present in pool!");
            return m_Objects[poolID];
        }

        NODISCARD FORCEINLINE const auto GetSize() const noexcept { return m_Objects.size(); }
        NODISCARD FORCEINLINE bool IsPresent(const PoolID& poolID) const noexcept
        {
            return poolID < m_bPresentObjects.size() && m_bPresentObjects[poolID];
        }

        class PoolIterator
        {
          public:
            PoolIterator(Pool<T>& pool, PoolID& poolID) noexcept : m_Pool(pool), m_ID(poolID) { NextPresentElement(); }
            ~PoolIterator() noexcept = default;

            NODISCARD FORCEINLINE T& operator*() noexcept { return m_Pool.Get(m_ID); }
            NODISCARD FORCEINLINE void operator++() noexcept
            {
                ++m_ID;
                NextPresentElement();
            }

            NODISCARD FORCEINLINE bool operator!=(const PoolIterator& other) const noexcept { return m_ID != other.m_ID; }

          private:
            Pool<T>& m_Pool;
            PoolID m_ID{};

            void NextPresentElement() noexcept
            {
                while (m_ID < m_Pool.GetSize() && !m_Pool.IsPresent(m_ID))
                    ++m_ID;
            }
            constexpr PoolIterator() noexcept = delete;
        };

        NODISCARD FORCEINLINE PoolIterator begin() noexcept { return PoolIterator(*this, 0); }
        NODISCARD FORCEINLINE PoolIterator end() noexcept { return PoolIterator(*this, GetSize()); }

      private:
        std::vector<T> m_Objects;
        std::vector<bool> m_bPresentObjects;
        std::vector<PoolID> m_FreeIDs;
    };

    namespace CoreUtils
    {

        FORCEINLINE static u64 AlignSize(const u64 val, const u64 alignment) noexcept
        {
            return (val + alignment - 1) & ~(alignment - 1);
        }

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

        static void SaveData(const std::string_view& dataPath, const std::stringstream& data) noexcept
        {
            RDNT_ASSERT(!dataPath.empty(), "Data path is invalid!");

            std::ofstream output(dataPath.data(), std::ios::out | std::ios::binary | std::ios::trunc);
            RDNT_ASSERT(output.is_open(), "Failed to open: {}", dataPath);

            output.write(reinterpret_cast<const char*>(data.str().c_str()), data.str().size() * sizeof(data.str()[0]));
            output.close();
        }

    }  // namespace CoreUtils

}  // namespace Radiant
