#pragma once

#include "core/types.h"

#include <string_view>

namespace anom {

struct GlfwVersion {
    i32 major;
    i32 minor;
    i32 revision;
};

GlfwVersion glfw_version();
std::string_view glfw_version_string();

} // namespace anom
