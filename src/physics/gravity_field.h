#pragma once

#include "core/types.h"
#include "math/vmath.h"

namespace anom {

inline constexpr f32 kDefaultGravity = 9.81f;
inline constexpr f32 kHalfPi = 1.57079632679f;

enum class GravityShape : u32 {
    Box,
    Sphere,
};

enum class GravityMode : u32 {
    Directional,
    Curl,
    Point,
};

struct GravityVolume {
    Vec3 pos{};
    Quat rot = quat_identity();
    Vec3 half{1.0f, 1.0f, 1.0f};
    f32 falloff = 4.0f;
    f32 strength = kDefaultGravity;
    i32 priority = 0;
    GravityShape shape = GravityShape::Box;
    GravityMode mode = GravityMode::Directional;
    f32 sector = kHalfPi;
};

inline constexpr u32 kGravityPathSamples = 48;

struct GravityPath {
    Vec3 points[kGravityPathSamples]{};
    Vec3 ups[kGravityPathSamples]{};
    u32 count = 0;
    f32 half_width = 4.0f;
    f32 height = 4.0f;
    f32 below = 1.0f;
    f32 falloff = 2.0f;
    f32 strength = kDefaultGravity;
    Vec3 centre{};
    f32 reach = 0.0f;
};

struct GravitySample {
    Vec3 gravity;
    Vec3 up;
    f32 presence = 0.0f;
};

class GravityField {
public:
    static constexpr u32 kMaxVolumes = 32;
    static constexpr u32 kMaxPaths = 32;

    void clear()
    {
        count_ = 0;
        dropped_ = 0;
        path_count_ = 0;
    }
    u32 dropped() const { return dropped_; }
    bool add(const GravityVolume& volume);
    u32 count() const { return count_; }
    const GravityVolume& volume(u32 i) const { return volumes_[i]; }
    bool add_path(const GravityPath& path);
    u32 path_count() const { return path_count_; }
    const GravityPath& path(u32 i) const { return paths_[i]; }

    GravitySample sample(Vec3 p) const;
    Vec3 gravity_at(Vec3 p) const { return sample(p).gravity; }
    Vec3 up_at(Vec3 p) const { return sample(p).up; }

    static f32 weight(const GravityVolume& volume, Vec3 p);
    static Vec3 down(const GravityVolume& volume, Vec3 p);
    static f32 path_weight(const GravityPath& path, Vec3 p, Vec3& out_up);

private:
    GravityVolume volumes_[kMaxVolumes];
    u32 count_ = 0;
    u32 dropped_ = 0;
    GravityPath paths_[kMaxPaths];
    u32 path_count_ = 0;
};

} // namespace anom
