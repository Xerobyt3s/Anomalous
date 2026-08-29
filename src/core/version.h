#pragma once

#include "core/types.h"

#include <string_view>

namespace anom {

struct Version {
    u32 major;
    u32 minor;
    u32 patch;
};

Version version();
std::string_view version_string();
std::string_view build_config();

} // namespace anom
