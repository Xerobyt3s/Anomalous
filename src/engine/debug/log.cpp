#include "engine/debug/log.h"

#include <spdlog/sinks/stdout_color_sinks.h>

namespace ghost::engine {
void initLogging() {
    auto logger = spdlog::stdout_color_mt("ghost");
    logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
#ifndef NDEBUG
    logger->set_level(spdlog::level::debug);
#endif
    spdlog::set_default_logger(std::move(logger));
}

}
