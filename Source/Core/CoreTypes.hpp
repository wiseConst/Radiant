#pragma once

#include <ankerl/unordered_dense.h>  // For hash map/sets
#include <memory>                    // For Shared/Unique

#include <chrono>  // For timer impl

namespace Radiant
{

#define NODISCARD [[nodiscard]]
#if _MSC_VER
#define FORCEINLINE __forceinline
#else
#define FORCEINLINE __attribute__((always_inline))
#endif
#define BIT(x) (1 << (x))
#define FALLTHROUGH [[fallthrough]]

// NOTE: In case you want to suppress the compiler warnings.
#define MAYBE_UNUSED [[maybe_unused]]

    static constexpr std::string_view s_DEFAULT_STRING = "NONE";

    template <class Key, class T, class Hash = ankerl::unordered_dense::hash<Key>, class KeyEqual = std::equal_to<Key>>
    using UnorderedMap = ankerl::unordered_dense::map<Key, T, Hash, KeyEqual>;

    template <class Key, class Hash = ankerl::unordered_dense::hash<Key>, class KeyEqual = std::equal_to<Key>>
    using UnorderedSet = ankerl::unordered_dense::set<Key, Hash, KeyEqual>;

    template <class T> using Shared = std::shared_ptr<T>;
    template <class T, class... Args> NODISCARD FORCEINLINE Shared<T> MakeShared(Args&&... args) noexcept
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

    template <class T> using Unique = std::unique_ptr<T>;
    template <class T, class... Args> NODISCARD FORCEINLINE Unique<T> MakeUnique(Args&&... args) noexcept
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

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

    class Uncopyable
    {
      public:
        constexpr Uncopyable(const Uncopyable&) noexcept            = delete;
        constexpr Uncopyable& operator=(const Uncopyable&) noexcept = delete;

      protected:
        constexpr Uncopyable() noexcept  = default;
        constexpr ~Uncopyable() noexcept = default;
    };

    class Unmovable
    {
      public:
        constexpr Unmovable(Unmovable&&) noexcept            = delete;
        constexpr Unmovable& operator=(Unmovable&&) noexcept = delete;

      protected:
        constexpr Unmovable() noexcept  = default;
        constexpr ~Unmovable() noexcept = default;
    };

    using PoolID = std::uint64_t;
    template <typename T> class Pool
    {
      public:
        constexpr Pool() noexcept = default;
        ~Pool() noexcept          = default;

        void Release(PoolID& poolID) noexcept
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

}  // namespace Radiant
