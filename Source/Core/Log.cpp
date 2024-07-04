#include <pch.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace TestBed
{

void Log::Init()
{
    std::vector<spdlog::sink_ptr> logSinks;
    logSinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    constexpr bool bTruncate = true;  // Clear on loading.
    logSinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("TestBed.log", bTruncate));

    logSinks[0]->set_pattern("%^[Thread: %t][%T] %n: %v%$");
    logSinks[1]->set_pattern("[Thread: %t][%T] [%l] %n: %v");

    s_Logger = std::make_shared<spdlog::logger>("TESTBED", begin(logSinks), end(logSinks));
    spdlog::register_logger(s_Logger);
    s_Logger->set_level(spdlog::level::trace);
    s_Logger->flush_on(spdlog::level::trace);
}

}  // namespace TestBed
