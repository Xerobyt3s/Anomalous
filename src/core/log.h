#pragma once

typedef enum LogLevel {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} LogLevel;

void log_msg(LogLevel level, const char* fmt, ...);

#define log_info(...)  log_msg(LOG_INFO, __VA_ARGS__)
#define log_warn(...)  log_msg(LOG_WARN, __VA_ARGS__)
#define log_error(...) log_msg(LOG_ERROR, __VA_ARGS__)
