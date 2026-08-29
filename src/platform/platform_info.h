#pragma once

#include "core/types.h"

#include <string_view>

namespace anom {

struct GlfwVersion {
    i32 major;
    i32 minor;
    i32 revision;
};

// Queryable without initialising GLFW or creating a context, so this is safe to call
// headless. Used at startup for the log banner, and by the P0 link test that proves
// glfw3.lib resolves against our CRT choice.
GlfwVersion glfw_version();
std::string_view glfw_version_string();

} // namespace anom
