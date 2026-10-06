#include "render/haze.h"

#include <cmath>

namespace anom {

namespace {

constexpr f32 kHazeFadeSpan = 0.75f;
constexpr f32 kHazeInside = 0.4f;

Vec3 centre_of(Vec4 s)
{
    return Vec3{s.x, s.y, s.z};
}

f32 smooth_step(f32 lo, f32 hi, f32 x)
{
    if (!(hi > lo)) {
        return x < lo ? 0.0f : 1.0f;
    }
    const f32 t = f_clamp01((x - lo) / (hi - lo));
    return t * t * (3.0f - 2.0f * t);
}

}

HazeSpan haze_window_span(const Vec4* spheres, u32 count, Vec3 eye, f32 near_d, f32 far_d)
{
    f32 reach = 0.0f;
    for (u32 i = 0; i < count; i++) {
        reach = f_max(reach, length(centre_of(spheres[i]) - eye) + spheres[i].w);
    }
    HazeSpan span;
    span.start = f_max(near_d, 0.0f);
    span.end = far_d < reach ? far_d : reach;
    return span;
}

bool haze_sphere_touches(Vec4 sphere, Vec3 eye, HazeSpan span)
{
    const f32 d = length(centre_of(sphere) - eye);
    return d + sphere.w > span.start && f_max(d - sphere.w, 0.0f) < span.end;
}

f32 haze_optical_depth(const Vec4* spheres, u32 count, f32 margin, f32 density, Vec3 ro, Vec3 rd, f32 dist, f32 t_start,
                       f32 t_end)
{
    f32 depth = 0.0f;
    for (u32 i = 0; i < count; i++) {
        const Vec3 c = centre_of(spheres[i]);
        const f32 r = f_max(spheres[i].w, 1e-3f);
        const Vec3 oc = ro - c;
        const f32 core = f_max(r - margin, 0.0f);
        const f32 outside = f_lerp(kHazeInside, 1.0f, smooth_step(core, core + margin * kHazeFadeSpan, length(oc)));
        const f32 b = dot(oc, rd);
        f32 h = b * b - (dot(oc, oc) - r * r);
        if (h <= 0.0f) {
            continue;
        }
        h = std::sqrt(h);
        const f32 t0 = f_max(-b - h, 0.0f);
        const f32 t1 = f_min(-b + h, dist);
        if (t1 - t0 <= 1e-4f) {
            continue;
        }
        const f32 closest = length(oc + rd * f_clamp(-b, t0, t1)) / r;
        const f32 total = (t1 - t0) * (1.0f - closest * closest * 0.7f) * outside;
        const f32 share = f_max(f_min(t1, t_end) - f_max(t0, t_start), 0.0f) / (t1 - t0);
        depth += total * share;
    }
    return depth * density;
}

}
