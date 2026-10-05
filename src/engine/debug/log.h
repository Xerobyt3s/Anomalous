#pragma once

#include "core/log.h"

#include <format>
#include <string>

#define GHOST_TRACE(...) ((void)0)
#define GHOST_INFO(...) log_info("%s", std::format(__VA_ARGS__).c_str())
#define GHOST_WARN(...) log_warn("%s", std::format(__VA_ARGS__).c_str())
#define GHOST_ERROR(...) log_error("%s", std::format(__VA_ARGS__).c_str())
