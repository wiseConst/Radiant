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

    union FloatBits
    {
        f32 f;
        u32 ui;
    };

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
        ~Timer() noexcept = default;

        NODISCARD FORCEINLINE f64 GetElapsedMilliseconds() const noexcept
        {
            const auto elapsed = std::chrono::duration<f64, std::milli>(Now() - m_StartTime);
            return elapsed.count();
        }

        NODISCARD FORCEINLINE static f64 GetElapsedSecondsFromNow(
            const std::chrono::time_point<std::chrono::high_resolution_clock>& timePoint) noexcept
        {
            return std::chrono::duration<f64>(Now() - timePoint).count();
        }

        NODISCARD FORCEINLINE f64 GetElapsedSeconds() const noexcept { return GetElapsedSecondsFromNow(m_StartTime); }

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

    // https://github.com/Raikiri/LegitProfiler
    namespace Colors
    {
        // https://flatuicolors.com/palette/defo
#define RGBA_LE(col)                                                                                                                       \
    (((col & 0xff000000) >> (24)) + ((col & 0x00ff0000) >> (8)) + ((col & 0x0000ff00) << (8)) + ((col & 0x000000ff) << (24)))

        static constexpr u32 turqoise = RGBA_LE(0x1abc9cffu);
        static constexpr u32 greenSea = RGBA_LE(0x16a085ffu);

        static constexpr u32 emerald   = RGBA_LE(0x2ecc71ffu);
        static constexpr u32 nephritis = RGBA_LE(0x27ae60ffu);

        static constexpr u32 peterRiver = RGBA_LE(0x3498dbffu);  // blue
        static constexpr u32 belizeHole = RGBA_LE(0x2980b9ffu);

        static constexpr u32 amethyst = RGBA_LE(0x9b59b6ffu);
        static constexpr u32 wisteria = RGBA_LE(0x8e44adffu);

        static constexpr u32 sunFlower = RGBA_LE(0xf1c40fffu);
        static constexpr u32 orange    = RGBA_LE(0xf39c12ffu);

        static constexpr u32 carrot  = RGBA_LE(0xe67e22ffu);
        static constexpr u32 pumpkin = RGBA_LE(0xd35400ffu);

        static constexpr u32 alizarin    = RGBA_LE(0xe74c3cffu);
        static constexpr u32 pomegranate = RGBA_LE(0xc0392bffu);

        static constexpr u32 clouds    = RGBA_LE(0xecf0f1ffu);
        static constexpr u32 silver    = RGBA_LE(0xbdc3c7ffu);
        static constexpr u32 imguiText = RGBA_LE(0xF2F5FAFFu);

        static constexpr std::array<u32, 17> ColorArray = {turqoise,    greenSea,   sunFlower, orange,   emerald,  alizarin,
                                                           pomegranate, peterRiver, nephritis, amethyst, carrot,   wisteria,
                                                           pumpkin,     belizeHole, clouds,    silver,   imguiText};

    }  // namespace Colors

    struct ProfilerTask final
    {
        ProfilerTask() noexcept  = default;
        ~ProfilerTask() noexcept = default;

        f64 StartTime{0.0};
        f64 EndTime{0.0};
        std::string Name{s_DEFAULT_STRING};
        u32 Color{0xFFFFFFFF};

        f64 GetLength() const noexcept { return EndTime - StartTime; }
    };

}  // namespace Radiant
