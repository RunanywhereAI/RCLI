#pragma once

#include <cstdio>
#include <atomic>

namespace rastack {

enum class LogLevel : int { SILENT = 0, ERROR = 1, WARN = 2, INFO = 3, DEBUG = 4, TRACE = 5 };

inline std::atomic<int>& log_level_ref() {
    static std::atomic<int> level{static_cast<int>(LogLevel::DEBUG)};
    return level;
}

inline void set_log_level(LogLevel lvl) {
    log_level_ref().store(static_cast<int>(lvl), std::memory_order_relaxed);
}

inline LogLevel get_log_level() {
    return static_cast<LogLevel>(log_level_ref().load(std::memory_order_relaxed));
}

} // namespace rastack

#define RASTACK_LOG(lvl, tag, fmt, ...) \
    do { \
        if (static_cast<int>(lvl) <= rastack::log_level_ref().load(std::memory_order_relaxed)) \
            fprintf(stderr, "[" tag "] " fmt "\n" __VA_OPT__(,) __VA_ARGS__); \
    } while (0)

#define LOG_ERROR(tag, fmt, ...) RASTACK_LOG(rastack::LogLevel::ERROR, tag, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)  RASTACK_LOG(rastack::LogLevel::WARN,  tag, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_INFO(tag, fmt, ...)  RASTACK_LOG(rastack::LogLevel::INFO,  tag, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_DEBUG(tag, fmt, ...) RASTACK_LOG(rastack::LogLevel::DEBUG, tag, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOG_TRACE(tag, fmt, ...) RASTACK_LOG(rastack::LogLevel::TRACE, tag, fmt __VA_OPT__(,) __VA_ARGS__)
