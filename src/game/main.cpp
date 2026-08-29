// Entry point.
//
// P0 skeleton: this exists to prove the three-target wiring (anomalous -> anomalous_core
// -> anomalous_external). The game loop lands in P1/P2 as App and Game, replacing the
// C original's 2,940-line main.c and its 119 file-static globals.

#include "core/version.h"
#include "platform/platform_info.h"

#include <cstdio>

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    const std::string_view version = anom::version_string();
    const std::string_view config = anom::build_config();
    const std::string_view glfw = anom::glfw_version_string();

    std::printf("anomalous %.*s (%.*s)\n",
                static_cast<int>(version.size()), version.data(),
                static_cast<int>(config.size()), config.data());
    std::printf("  glfw %.*s\n", static_cast<int>(glfw.size()), glfw.data());

    return 0;
}
