#include "core/log.h"
#include "core/types.h"

#include <cstdarg>
#include <cstdio>

#include <windows.h>

namespace anom {
namespace {

const char* level_prefix(LogLevel level)
{
    switch (level) {
    case LogLevel::Info: return "[info]  ";
    case LogLevel::Warn: return "[warn]  ";
    case LogLevel::Error: return "[error] ";
    }
    return "[?]     ";
}

} // namespace

void log_msg(LogLevel level, const char* fmt, ...)
{
    char message[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    char line[1100];
    std::snprintf(line, sizeof(line), "%s%s\n", level_prefix(level), message);
    std::fputs(line, stdout);
    std::fflush(stdout);
    OutputDebugStringA(line);
}

} // namespace anom
