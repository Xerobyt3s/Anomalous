#pragma once

#include "core/types.h"

#include <string_view>

namespace anom {

class Arena;

struct FrameStats {
    f64 mean_r = 0.0;
    f64 mean_g = 0.0;
    f64 mean_b = 0.0;
    f32 nonblack_fraction = 0.0f;
    u32 distinct_buckets = 0;
};

bool capture_frame(Arena& scratch, i32 width, i32 height, std::string_view bmp_path,
                   FrameStats* out_stats);

} // namespace anom
