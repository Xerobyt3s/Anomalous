#include "core/log.h"
#include "core/types.h"

#include <spdlog/spdlog.h>

#include <cstdarg>
#include <cstdio>

namespace anom {

void log_msg(LogLevel level, const char* fmt, ...)
{
    char message[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    switch (level) {
    case LogLevel::Info: spdlog::info("{}", message); break;
    case LogLevel::Warn: spdlog::warn("{}", message); break;
    case LogLevel::Error: spdlog::error("{}", message); break;
    }
}

} // namespace anom
