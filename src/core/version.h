#pragma once

#include "core/types.h"

#include <string_view>

namespace anom {

struct Version {
    u32 major;
    u32 minor;
    u32 patch;
};

// Defined in version.cpp so that both anomalous and anomalous_tests are proven to link
// against the same anomalous_core, rather than each header-inlining its own copy.
Version version();
std::string_view version_string();
std::string_view build_config();

} // namespace anom
