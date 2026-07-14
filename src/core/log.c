#include "core/log.h"

#include <stdio.h>
#include <stdarg.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static const char* s_level_prefix[] = {
    "[info]  ",
    "[warn]  ",
    "[error] ",
};

void log_msg(LogLevel level, const char* fmt, ...)
{
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    char line[1100];
    snprintf(line, sizeof(line), "%s%s\n", s_level_prefix[level], message);
    fputs(line, stdout);
    if (level == LOG_ERROR) {
        fflush(stdout);
    }
    OutputDebugStringA(line);
}
