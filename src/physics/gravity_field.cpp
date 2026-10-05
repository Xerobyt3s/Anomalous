#include "physics/gravity_field.h"

#include <cmath>

namespace anom {

bool GravityField::add(const GravityVolume& volume)
{
    if (count_ >= kMaxVolumes) {
        dropped_++;
        return false;
    }
    u32 at = count_;
    while (at > 0 && volumes_[at - 1].priority > volume.priority) {
        volumes_[at] = volumes_[at - 1];
        at--;
    }
    volumes_[at] = volume;
    volumes_[at].rot = normalize(volume.rot);
    count_++;
    return true;
}

bool GravityField::add_path(const GravityPath& path)
{
    if (path_count_ >= kMaxPaths || path.count < 2) {
        dropped_++;
        return false;
    }
    GravityPath& out = paths_[path_count_++];
    out = path;
    Vec3 lo = path.points[0];
    Vec3 hi = path.points[0];
    for (u32 i = 0; i < path.count; i++) {
        lo = Vec3{f_min(lo.x, path.points[i].x), f_min(lo.y, path.points[i].y), f_min(lo.z, path.points[i].z)};
        hi = Vec3{f_max(hi.x, path.points[i].x), f_max(hi.y, path.points[i].y), f_max(hi.z, path.points[i].z)};
        out.ups[i] = normalize(path.ups[i]);
    }
    out.centre = (lo + hi) * 0.5f;
    out.reach = length(hi - lo) * 0.5f + path.half_width + path.height + path.falloff;
    return true;
}

f32 GravityField::path_weight(const GravityPath& path, Vec3 p, Vec3& out_up)
{
    if (path.count < 2 || length_sq(p - path.centre) > path.reach * path.reach) {
        return 0.0f;
    }
    f32 best_out = 1e30f;
    f32 best_key = 1e30f;
    for (u32 i = 0; i + 1 < path.count; i++) {
        const Vec3 a = path.points[i];
        const Vec3 b = path.points[i + 1];
        const Vec3 ab = b - a;
        const f32 len_sq = f_max(dot(ab, ab), 1e-8f);
        const f32 raw_t = dot(p - a, ab) / len_sq;
        const f32 t = f_clamp01(raw_t);
        const Vec3 closest = a + ab * t;
        const Vec3 up = normalize(lerp(path.ups[i], path.ups[i + 1], t));
        const Vec3 tangent = ab * (1.0f / std::sqrt(len_sq));
        Vec3 side = cross(tangent, up);
        side = length_sq(side) > 1e-8f ? normalize(side) : any_perpendicular(up);
        const Vec3 d = p - closest;
        const f32 lateral = f_max(f_abs(dot(d, side)) - path.half_width, 0.0f);
        const f32 h = dot(d, up);
        const f32 vertical = h > path.height ? h - path.height : (h < -path.below ? -path.below - h : 0.0f);
        f32 beyond = 0.0f;
        if (raw_t < 0.0f) {
            beyond = -raw_t * std::sqrt(len_sq);
        } else if (raw_t > 1.0f) {
            beyond = (raw_t - 1.0f) * std::sqrt(len_sq);
        }
        const f32 out = std::sqrt(lateral * lateral + vertical * vertical + beyond * beyond);
        const f32 key = out + 1e-3f * length(d);
        if (key < best_key) {
            best_key = key;
            best_out = out;
            out_up = up;
        }
    }
    if (best_out <= 0.0f) {
        return 1.0f;
    }
    if (path.falloff <= 0.0f || best_out >= path.falloff) {
        return 0.0f;
    }
    const f32 x = best_out / path.falloff;
    return 1.0f - x * x * (3.0f - 2.0f * x);
}

Vec3 GravityField::down(const GravityVolume& volume, Vec3 p)
{
    const Vec3 local_down{0.0f, -1.0f, 0.0f};
    if (volume.mode == GravityMode::Directional) {
        return rotate(volume.rot, local_down);
    }
    const Vec3 local = rotate(conjugate(volume.rot), p - volume.pos);
    if (volume.mode == GravityMode::Point) {
        const f32 len = length(local);
        return len > 1e-4f ? rotate(volume.rot, local * (-1.0f / len)) : rotate(volume.rot, local_down);
    }
    const Vec3 radial{local.x, local.y, 0.0f};
    const f32 len = length(radial);
    return len > 1e-4f ? rotate(volume.rot, radial * (1.0f / len)) : rotate(volume.rot, local_down);
}

f32 GravityField::weight(const GravityVolume& volume, Vec3 p)
{
    const Vec3 local = rotate(conjugate(volume.rot), p - volume.pos);
    f32 outside;
    if (volume.shape == GravityShape::Sphere) {
        outside = f_max(length(local) - volume.half.x, 0.0f);
    } else {
        const Vec3 over{f_max(f_abs(local.x) - volume.half.x, 0.0f),
                        f_max(f_abs(local.y) - volume.half.y, 0.0f),
                        f_max(f_abs(local.z) - volume.half.z, 0.0f)};
        outside = length(over);
    }
    if (volume.mode == GravityMode::Curl) {
        const f32 angle = std::atan2(local.x, -local.y);
        if (angle < 0.0f || angle > volume.sector) {
            return 0.0f;
        }
    }
    if (outside <= 0.0f) {
        return 1.0f;
    }
    if (volume.falloff <= 0.0f || outside >= volume.falloff) {
        return 0.0f;
    }
    const f32 t = outside / volume.falloff;
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

GravitySample GravityField::sample(Vec3 p) const
{
    Vec3 dir{0.0f, -1.0f, 0.0f};
    f32 magnitude = kDefaultGravity;
    f32 presence = 0.0f;
    for (u32 i = 0; i < count_; i++) {
        const GravityVolume& volume = volumes_[i];
        const f32 w = weight(volume, p);
        if (w <= 0.0f) {
            continue;
        }
        const Vec3 target = down(volume, p);
        const Quat full = quat_from_to(dir, target, rotate(volume.rot, Vec3{1.0f, 0.0f, 0.0f}));
        dir = w >= 1.0f ? target : normalize(rotate(slerp(quat_identity(), full, w), dir));
        magnitude = f_lerp(magnitude, volume.strength, w);
        presence = f_max(presence, w);
    }
    for (u32 i = 0; i < path_count_; i++) {
        const GravityPath& path = paths_[i];
        Vec3 up{0.0f, 1.0f, 0.0f};
        const f32 w = path_weight(path, p, up);
        if (w <= 0.0f) {
            continue;
        }
        const Vec3 target = up * -1.0f;
        const Quat full = quat_from_to(dir, target);
        dir = w >= 1.0f ? target : normalize(rotate(slerp(quat_identity(), full, w), dir));
        magnitude = f_lerp(magnitude, path.strength, w);
        presence = f_max(presence, w);
    }
    GravitySample out;
    out.gravity = dir * magnitude;
    out.up = dir * -1.0f;
    out.presence = presence;
    return out;
}

} // namespace anom
