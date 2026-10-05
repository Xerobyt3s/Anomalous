#include "physics/gravity_field.h"

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
    GravitySample out;
    out.gravity = dir * magnitude;
    out.up = dir * -1.0f;
    out.presence = presence;
    return out;
}

} // namespace anom
