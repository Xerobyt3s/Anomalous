#include "terminal/disks.h"

namespace anom {
namespace {

constexpr std::string_view kLabels[DISK_COUNT] = {
    "DR-OS MASTER", "FIELD NOTES", "SCRATCH DISK", "RC-AV RESCUE", "RADSHACK 12",
};

} // namespace

std::string_view disk_label(i32 disk)
{
    if (disk < 0 || disk >= DISK_COUNT) {
        return "UNKNOWN MEDIA";
    }
    return kLabels[disk];
}

} // namespace anom
