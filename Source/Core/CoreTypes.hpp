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

    static const std::string s_DEFAULT_STRING{"NONE"};

    using i8  = std::int8_t;
    using u8  = std::uint8_t;
    using i16 = std::int16_t;
    using u16 = std::uint16_t;
    using i32 = std::int32_t;
    using u32 = std::uint32_t;
    using i64 = std::int64_t;
    using u64 = std::uint64_t;
    using f32 = std::float_t;
    using f64 = std::double_t;

    template <class Key, class T, class Hash = ankerl::unordered_dense::hash<Key>, class KeyEqual = std::equal_to<Key>>
    using UnorderedMap = ankerl::unordered_dense::map<Key, T, Hash, KeyEqual>;

    template <class Key, class Hash = ankerl::unordered_dense::hash<Key>, class KeyEqual = std::equal_to<Key>>
    using UnorderedSet = ankerl::unordered_dense::set<Key, Hash, KeyEqual>;

    template <class T> using WeakPtr = std::weak_ptr<T>;

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

        NODISCARD FORCEINLINE f64 GetElapsedMilliseconds() const noexcept
        {
            const auto elapsed = std::chrono::duration<f64, std::milli>(Now() - m_StartTime);
            return elapsed.count();
        }

        NODISCARD FORCEINLINE f64 GetElapsedSeconds() const noexcept
        {
            const auto elapsed = std::chrono::duration<f64>(Now() - m_StartTime);
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

}  // namespace Radiant
