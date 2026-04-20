#pragma once

#include <cstdarg>
#include <cstdint>

namespace screenlib::log {

enum class Level : uint8_t {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3,
    Trace = 4
};

class Logger {
public:
    using Sink = void (*)(const char* line, void* userData);

    static void init(Level level = Level::Info);
    static void setLevel(Level level);
    static Level level();

    static void setSink(Sink sink, void* userData = nullptr);

    static bool enabled(Level level);
    static void log(Level level, const char* tag, const char* fmt, ...);
    static void vlog(Level level, const char* tag, const char* fmt, va_list args);
};

} // namespace screenlib::log

#define SCREENLIB_LOGE(TAG, FMT, ...) ::screenlib::log::Logger::log(::screenlib::log::Level::Error, TAG, FMT, ##__VA_ARGS__)
#define SCREENLIB_LOGW(TAG, FMT, ...) ::screenlib::log::Logger::log(::screenlib::log::Level::Warn, TAG, FMT, ##__VA_ARGS__)
#define SCREENLIB_LOGI(TAG, FMT, ...) ::screenlib::log::Logger::log(::screenlib::log::Level::Info, TAG, FMT, ##__VA_ARGS__)
#define SCREENLIB_LOGD(TAG, FMT, ...) ::screenlib::log::Logger::log(::screenlib::log::Level::Debug, TAG, FMT, ##__VA_ARGS__)
#define SCREENLIB_LOGT(TAG, FMT, ...) ::screenlib::log::Logger::log(::screenlib::log::Level::Trace, TAG, FMT, ##__VA_ARGS__)
