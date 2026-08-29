#pragma once

namespace anom {

enum class LogLevel {
    Info,
    Warn,
    Error,
};

void log_msg(LogLevel level, const char* fmt, ...);

} // namespace anom

#define log_info(...) ::anom::log_msg(::anom::LogLevel::Info, __VA_ARGS__)
#define log_warn(...) ::anom::log_msg(::anom::LogLevel::Warn, __VA_ARGS__)
#define log_error(...) ::anom::log_msg(::anom::LogLevel::Error, __VA_ARGS__)
