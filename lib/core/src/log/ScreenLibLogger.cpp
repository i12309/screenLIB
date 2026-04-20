#include "log/ScreenLibLogger.h"

#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#else
#include <chrono>
#endif

namespace screenlib::log {

namespace {

Level g_level = Level::Info;
Logger::Sink g_sink = nullptr;
void* g_sinkUserData = nullptr;

uint32_t monotonic_ms() {
#if defined(ARDUINO)
    return millis();
#else
    using clock = std::chrono::steady_clock;
    static const auto kStart = clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - kStart);
    return static_cast<uint32_t>(elapsed.count());
#endif
}

char level_char(Level level) {
    switch (level) {
        case Level::Error:
            return 'E';
        case Level::Warn:
            return 'W';
        case Level::Info:
            return 'I';
        case Level::Debug:
            return 'D';
        case Level::Trace:
            return 'T';
        default:
            return '?';
    }
}

void emit_default(const char* line) {
    if (line == nullptr) {
        return;
    }

#if defined(ARDUINO)
    if (!Serial) {
        return;
    }
    Serial.println(line);
#else
    std::printf("%s\n", line);
#endif
}

void trim_trailing_newline(char* text) {
    if (text == nullptr) {
        return;
    }

    size_t len = std::strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
        text[len - 1] = '\0';
        len--;
    }
}

} // namespace

void Logger::init(Level level) {
    g_level = level;
}

void Logger::setLevel(Level level) {
    g_level = level;
}

Level Logger::level() {
    return g_level;
}

void Logger::setSink(Sink sink, void* userData) {
    g_sink = sink;
    g_sinkUserData = userData;
}

bool Logger::enabled(Level level) {
    return static_cast<uint8_t>(level) <= static_cast<uint8_t>(g_level);
}

void Logger::log(Level level, const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(level, tag, fmt, args);
    va_end(args);
}

void Logger::vlog(Level level, const char* tag, const char* fmt, va_list args) {
    if (!enabled(level) || fmt == nullptr) {
        return;
    }

    char msg[320] = {};
    std::vsnprintf(msg, sizeof(msg), fmt, args);
    trim_trailing_newline(msg);

    const char* safeTag = (tag != nullptr && tag[0] != '\0') ? tag : "log";
    char line[420] = {};
    std::snprintf(line,
                  sizeof(line),
                  "[%10lu][%c][%s] %s",
                  static_cast<unsigned long>(monotonic_ms()),
                  level_char(level),
                  safeTag,
                  msg);

    if (g_sink != nullptr) {
        g_sink(line, g_sinkUserData);
        return;
    }

    emit_default(line);
}

} // namespace screenlib::log
