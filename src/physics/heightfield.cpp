#include "physics/heightfield.h"
#include "core/arena.h"
#include "core/rng.h"

namespace anom {
namespace {

constexpr f32 kInf = 1e30f;

Vec3 triangle_normal_up(Vec3 a, Vec3 b, Vec3 c)
{
    Vec3 n = normalize(cross(b - a, c - a));
    if (n.y < 0.0f) {
        n = -n;
    }
    return n;
}

} // namespace

void Heightfield::alloc(Arena& arena, u32 size, f32 cell_size)
{
    size_x_ = size;
    size_z_ = size;
    cell_size_ = cell_size;
    const f32 half = static_cast<f32>(size - 1) * cell_size * 0.5f;
    origin_ = Vec3{-half, 0.0f, -half};
    heights_ = arena.push_array<f32>(static_cast<u64>(size) * size);
    min_height_ = kInf;
    max_height_ = -kInf;
}

void Heightfield::set_height(u32 ix, u32 iz, f32 value)
{
    heights_[static_cast<u64>(iz) * size_x_ + ix] = value;
}

void Heightfield::recompute_extents()
{
    min_height_ = kInf;
    max_height_ = -kInf;
    const u64 count = static_cast<u64>(size_x_) * size_z_;
    for (u64 i = 0; i < count; i++) {
        min_height_ = f_min(min_height_, heights_[i]);
        max_height_ = f_max(max_height_, heights_[i]);
    }
}

void Heightfield::init_procedural(Arena& arena, u32 size, f32 cell_size, u32 seed, f32 roughness)
{
    alloc(arena, size, cell_size);

    Rng rng(seed);
    const f32 p1 = rng.range(0.0f, kTau);
    const f32 p2 = rng.range(0.0f, kTau);
    const f32 p3 = rng.range(0.0f, kTau);
    const f32 p4 = rng.range(0.0f, kTau);
    const f32 p5 = rng.range(0.0f, kTau);
    const f32 p6 = rng.range(0.0f, kTau);

    for (u32 iz = 0; iz < size; iz++) {
        for (u32 ix = 0; ix < size; ix++) {
            const f32 x = origin_.x + static_cast<f32>(ix) * cell_size;
            const f32 z = origin_.z + static_cast<f32>(iz) * cell_size;
            const f32 bowl = (x * x + z * z) * 0.006f;
            const f32 waves = 1.6f * std::sin(x * 0.19f + p1) * std::cos(z * 0.16f + p2)
                            + 0.7f * std::sin(x * 0.47f + p3) * std::sin(z * 0.41f + p4)
                            + 0.3f * std::cos(x * 0.83f + p5) * std::cos(z * 0.77f + p6);
            set_height(ix, iz, roughness * (bowl + waves));
        }
    }
    recompute_extents();
}

void Heightfield::init_slope(Arena& arena, u32 size, f32 cell_size, f32 grade)
{
    alloc(arena, size, cell_size);
    for (u32 iz = 0; iz < size; iz++) {
        for (u32 ix = 0; ix < size; ix++) {
            const f32 x = origin_.x + static_cast<f32>(ix) * cell_size;
            set_height(ix, iz, f_max(x, 0.0f) * grade);
        }
    }
    recompute_extents();
}

f32 Heightfield::height_at(u32 ix, u32 iz) const
{
    ix = ix < size_x_ ? ix : size_x_ - 1;
    iz = iz < size_z_ ? iz : size_z_ - 1;
    return heights_[static_cast<u64>(iz) * size_x_ + ix];
}

void Heightfield::locate(f32 x, f32 z, u32& ix, u32& iz, f32& u, f32& v) const
{
    f32 fx = (x - origin_.x) / cell_size_;
    f32 fz = (z - origin_.z) / cell_size_;
    const f32 max_fx = static_cast<f32>(size_x_ - 2);
    const f32 max_fz = static_cast<f32>(size_z_ - 2);
    fx = f_clamp(fx, 0.0f, max_fx + 0.9999f);
    fz = f_clamp(fz, 0.0f, max_fz + 0.9999f);
    ix = static_cast<u32>(f_min(fx, max_fx));
    iz = static_cast<u32>(f_min(fz, max_fz));
    u = fx - static_cast<f32>(ix);
    v = fz - static_cast<f32>(iz);
}

f32 Heightfield::sample(f32 x, f32 z) const
{
    u32 ix = 0;
    u32 iz = 0;
    f32 u = 0.0f;
    f32 v = 0.0f;
    locate(x, z, ix, iz, u, v);

    const f32 h00 = height_at(ix, iz);
    const f32 h10 = height_at(ix + 1, iz);
    const f32 h01 = height_at(ix, iz + 1);
    const f32 h11 = height_at(ix + 1, iz + 1);
    if (u >= v) {
        return h00 + (h10 - h00) * u + (h11 - h10) * v;
    }
    return h00 + (h11 - h01) * u + (h01 - h00) * v;
}

f32 Heightfield::sample_from(const f32* heights, f32 x, f32 z) const
{
    u32 ix = 0;
    u32 iz = 0;
    f32 u = 0.0f;
    f32 v = 0.0f;
    locate(x, z, ix, iz, u, v);
    const auto at = [&](u32 cx, u32 cz) { return heights[static_cast<u64>(cz) * size_x_ + cx]; };
    const f32 h00 = at(ix, iz);
    const f32 h10 = at(ix + 1, iz);
    const f32 h01 = at(ix, iz + 1);
    const f32 h11 = at(ix + 1, iz + 1);
    if (u >= v) {
        return h00 + (h10 - h00) * u + (h11 - h10) * v;
    }
    return h00 + (h11 - h01) * u + (h01 - h00) * v;
}

void Heightfield::cell_triangles(u32 ix, u32 iz, Vec3 out[6]) const
{
    const f32 x0 = origin_.x + static_cast<f32>(ix) * cell_size_;
    const f32 z0 = origin_.z + static_cast<f32>(iz) * cell_size_;
    const f32 x1 = x0 + cell_size_;
    const f32 z1 = z0 + cell_size_;

    const Vec3 p00{x0, height_at(ix, iz), z0};
    const Vec3 p10{x1, height_at(ix + 1, iz), z0};
    const Vec3 p01{x0, height_at(ix, iz + 1), z1};
    const Vec3 p11{x1, height_at(ix + 1, iz + 1), z1};

    out[0] = p00;
    out[1] = p10;
    out[2] = p11;
    out[3] = p00;
    out[4] = p11;
    out[5] = p01;
}

Vec3 Heightfield::normal(f32 x, f32 z) const
{
    u32 ix = 0;
    u32 iz = 0;
    f32 u = 0.0f;
    f32 v = 0.0f;
    locate(x, z, ix, iz, u, v);

    Vec3 tris[6];
    cell_triangles(ix, iz, tris);
    if (u >= v) {
        return triangle_normal_up(tris[0], tris[1], tris[2]);
    }
    return triangle_normal_up(tris[3], tris[4], tris[5]);
}

Aabb Heightfield::bounds() const
{
    return Aabb{Vec3{origin_.x, min_height_ - 0.5f, origin_.z},
                Vec3{origin_.x + span_x(), max_height_ + 0.5f, origin_.z + span_z()}};
}

} // namespace anom
