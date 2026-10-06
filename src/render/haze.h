#pragma once

#include "core/types.h"
#include "math/vmath.h"

namespace anom {

struct HazeSpan {
    f32 start = 0.0f;
    f32 end = 0.0f;

    bool empty() const { return !(end > start); }
};

HazeSpan haze_window_span(const Vec4* spheres, u32 count, Vec3 eye, f32 near_d, f32 far_d);
bool haze_sphere_touches(Vec4 sphere, Vec3 eye, HazeSpan span);
f32 haze_optical_depth(const Vec4* spheres, u32 count, f32 margin, f32 density, Vec3 ro, Vec3 rd, f32 dist, f32 t_start,
                       f32 t_end);

}
