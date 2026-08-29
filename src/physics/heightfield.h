#pragma once

#include "core/types.h"
#include "math/vmath.h"

#include <span>

namespace anom {

class Arena;

class Heightfield {
public:
    void alloc(Arena& arena, u32 size, f32 cell_size);
    void init_procedural(Arena& arena, u32 size, f32 cell_size, u32 seed, f32 roughness);
    void init_slope(Arena& arena, u32 size, f32 cell_size, f32 grade);

    void set_height(u32 ix, u32 iz, f32 value);
    void recompute_extents();

    f32 height_at(u32 ix, u32 iz) const;
    f32 sample(f32 x, f32 z) const;
    Vec3 normal(f32 x, f32 z) const;
    void cell_triangles(u32 ix, u32 iz, Vec3 out[6]) const;
    bool raycast(Ray ray, f32 max_t, f32* out_t, Vec3* out_normal) const;

    Aabb bounds() const;
    f32 span_x() const { return static_cast<f32>(size_x_ - 1) * cell_size_; }
    f32 span_z() const { return static_cast<f32>(size_z_ - 1) * cell_size_; }

    u32 size_x() const { return size_x_; }
    u32 size_z() const { return size_z_; }
    f32 cell_size() const { return cell_size_; }
    Vec3 origin() const { return origin_; }
    f32 min_height() const { return min_height_; }
    f32 max_height() const { return max_height_; }
    bool valid() const { return heights_ != nullptr; }
    std::span<const f32> heights() const
    {
        return {heights_, static_cast<std::size_t>(size_x_) * size_z_};
    }

private:
    void locate(f32 x, f32 z, u32& ix, u32& iz, f32& u, f32& v) const;

    f32* heights_ = nullptr;
    u32 size_x_ = 0;
    u32 size_z_ = 0;
    f32 cell_size_ = 1.0f;
    Vec3 origin_{0.0f, 0.0f, 0.0f};
    f32 min_height_ = 0.0f;
    f32 max_height_ = 0.0f;
};

} // namespace anom
