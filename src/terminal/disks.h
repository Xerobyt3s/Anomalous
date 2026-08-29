#pragma once

#include "core/types.h"

#include <string_view>

namespace anom {

enum DiskId : i32 {
    DISK_MASTER = 0,
    DISK_FIELD_NOTES,
    DISK_SCRATCH,
    DISK_RESCUE,
    DISK_ARCADE,
    DISK_COUNT,
};

std::string_view disk_label(i32 disk);

} // namespace anom
