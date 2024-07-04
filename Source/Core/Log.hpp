#pragma once

#include <Core/CoreTypes.hpp>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

namespace TestBed
{

class Log final
{
  public:
    static void Init();

    NODISCARD FORCEINLINE static auto& GetLogger() { return s_Logger; }

  private:
    static inline std::shared_ptr<spdlog::logger> s_Logger = nullptr;

    Log()  = delete;
    ~Log() = default;
};

}  // namespace TestBed

#define TB_TRACE(...) ::TestBed::Log::GetLogger()->trace(__VA_ARGS__)
#define TB_INFO(...) ::TestBed::Log::GetLogger()->info(__VA_ARGS__)
#define TB_WARN(...) ::TestBed::Log::GetLogger()->warn(__VA_ARGS__)
#define TB_ERROR(...) ::TestBed::Log::GetLogger()->error(__VA_ARGS__)
#define TB_CRITICAL(...) ::TestBed::Log::GetLogger()->critical(__VA_ARGS__)