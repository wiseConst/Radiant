#pragma once

#include <Core/CoreTypes.hpp>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

namespace Radiant
{

    class Log final
    {
      public:
        static void Init() noexcept;
        FORCEINLINE static void Shutdown() noexcept { spdlog::shutdown(); }

        NODISCARD FORCEINLINE static auto& GetLogger() noexcept { return s_Logger; }

      private:
        static inline std::shared_ptr<spdlog::logger> s_Logger = nullptr;

        Log()  = delete;
        ~Log() = default;
    };

}  // namespace Radiant

#define LOG_TRACE(...) ::Radiant::Log::GetLogger()->trace(__VA_ARGS__)
#define LOG_INFO(...) ::Radiant::Log::GetLogger()->info(__VA_ARGS__)
#define LOG_WARN(...) ::Radiant::Log::GetLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...) ::Radiant::Log::GetLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::Radiant::Log::GetLogger()->critical(__VA_ARGS__)
