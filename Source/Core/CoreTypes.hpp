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

#define RDNT_ENUM_BITS(EnumType)                                                                                                           \
    FORCEINLINE constexpr EnumType operator|(EnumType lhs, EnumType rhs) noexcept                                                          \
    {                                                                                                                                      \
        return EnumType(std::to_underlying(lhs) | std::to_underlying(rhs));                                                                \
    }                                                                                                                                      \
    FORCEINLINE constexpr bool operator&(EnumType lhs, EnumType rhs) noexcept                                                              \
    {                                                                                                                                      \
        return static_cast<std::underlying_type_t<EnumType>>(0) != (std::to_underlying(lhs) & std::to_underlying(rhs));                    \
    }

    static constexpr std::string_view s_DEFAULT_STRING = "NONE";

    template <class Key, class T, class Hash = ankerl::unordered_dense::hash<Key>, class KeyEqual = std::equal_to<Key>>
    using UnorderedMap = ankerl::unordered_dense::map<Key, T, Hash, KeyEqual>;

    template <class Key, class Hash = ankerl::unordered_dense::hash<Key>, class KeyEqual = std::equal_to<Key>>
    using UnorderedSet = ankerl::unordered_dense::set<Key, void, Hash, KeyEqual>;

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

    template <typename T> class Handle
    {
      public:
        Handle() noexcept  = default;
        ~Handle() noexcept = default;

        NODISCARD FORCEINLINE T Get() const noexcept { return std::static_pointer_cast<T>(m_Impl); }

      private:
        class Impl;
        friend T;

      protected:
        Shared<Impl> m_Impl = nullptr;
    };

}  // namespace Radiant
