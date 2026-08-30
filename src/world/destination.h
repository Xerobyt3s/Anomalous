#pragma once

#include "core/types.h"

#include <span>
#include <string_view>

namespace anom {

struct Destination {
    std::string_view id;
    std::string_view name;
    std::string_view zone_dir;
    std::string_view note;
    f32 range_km = 0.0f;

    bool surveyed() const { return !zone_dir.empty(); }
};

std::span<const Destination> destinations();
i32 destination_index(std::string_view id);

} // namespace anom
