#ifndef TASER_LOGGING_HPP
#define TASER_LOGGING_HPP

#ifdef ENABLE_LOGGING
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <vector>
#include <memory>
#include <string_view>
#define LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)

inline spdlog::level::level_enum parse_level(const std::string_view& level)
{
    if (level == "trace")    return spdlog::level::trace;
    if (level == "debug")    return spdlog::level::debug;
    if (level == "info")     return spdlog::level::info;
    if (level == "warn")     return spdlog::level::warn;
    if (level == "error")    return spdlog::level::err;
    if (level == "critical") return spdlog::level::critical;
    if (level == "off")      return spdlog::level::off;

    return spdlog::level::info;
}

inline void init_logging(std::string_view log_level, bool console_log) {
    //spdlog::init_thread_pool(
    //    8192,  // queue size
    //    1      // worker threads
    //);

    std::vector<spdlog::sink_ptr> sinks;
    if (console_log) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    constexpr std::size_t max_file_size = 10 * 1024 * 1024; // 10 MB
    constexpr std::size_t  max_files = 3; // rotate through 3 files

    sinks.push_back(
        std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                "taser.log", max_file_size, max_files));

    auto logger = std::make_shared<spdlog::logger>(
            "TASER",
            sinks.begin(),
            sinks.end());

    /**auto logger = std::make_shared<spdlog::async_logger>(
        "turnserver",
        sinks.begin(),
        sinks.end(),
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::block);**/

    auto level = parse_level(log_level);

    logger->set_level(level);
    logger->flush_on(spdlog::level::warn);

    logger->set_pattern(
            "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

    spdlog::set_default_logger(logger);
    spdlog::set_level(level);
    spdlog::flush_on(spdlog::level::warn);

    logger->info("Logging initialized");
}

#else
#define LOG_INFO(...)  (void)0
#define LOG_WARN(...)  (void)0
#define LOG_ERROR(...) (void)0
#define LOG_CRITICAL(...) (void)0
#define LOG_DEBUG(...) (void)0
#define LOG_TRACE(...) (void)0
#endif

#endif //TASER_LOGGING_HPP
