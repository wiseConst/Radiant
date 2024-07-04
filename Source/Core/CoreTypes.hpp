#pragma once

#include <ankerl/unordered_dense.h>  // For hash map/sets
#include <memory>                    // For Shared/Unique

namespace TestBed
{

#define NODISCARD [[nodiscard]]
#define FORCEINLINE __forceinline

// NOTE: In case you want to suppress the compiler warnings.
#define MAYBE_UNUSED [[maybe_unused]]

    static constexpr std::string_view s_DEFAULT_STRING = "NONE";

    template <class Key, class T, class Hash = ankerl::unordered_dense::hash<Key>, class KeyEqual = std::equal_to<Key>>
    using UnorderedMap = ankerl::unordered_dense::map<Key, T, Hash, KeyEqual>;

    template <class Key, class Hash = ankerl::unordered_dense::hash<Key>, class KeyEqual = std::equal_to<Key>>
    using UnorderedSet = ankerl::unordered_dense::set<Key, void, Hash, KeyEqual>;

    template <class T> using Shared = std::shared_ptr<T>;
    template <class T, class... Args> NODISCARD FORCEINLINE Shared<T> MakeShared(Args&&... args) noexcept
    {
        return std::make_shared<T>(std::forward<args>...);
    }

    template <class T> using Unique = std::unique_ptr<T>;
    template <class T, class... Args> NODISCARD FORCEINLINE Unique<T> MakeUnique(Args&&... args) noexcept
    {
        return std::make_unique<T>(std::forward<args>...);
    }

    template <typename T> class Handle
    {
      public:
        Handle()  = default;
        ~Handle() = default;

        T Get() const noexcept { return std::static_pointer_cast<T>(m_Impl); }

      private:
        class Impl;
        friend T;

      protected:
        Shared<Impl> m_Impl = nullptr;
    };

}  // namespace TestBed
