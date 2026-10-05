#pragma once

#include <spdlog/spdlog.h>

namespace ghost::engine {
void initLogging();

}

#define GHOST_TRACE(...) ::spdlog::trace(__VA_ARGS__)
#define GHOST_DEBUG(...) ::spdlog::debug(__VA_ARGS__)
#define GHOST_INFO(...) ::spdlog::info(__VA_ARGS__)
#define GHOST_WARN(...) ::spdlog::warn(__VA_ARGS__)
#define GHOST_ERROR(...) ::spdlog::error(__VA_ARGS__)
