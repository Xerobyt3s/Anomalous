#include "physics/heightfield.h"
#include "core/arena.h"
#include "core/rng.h"

namespace anom {
namespace {

constexpr f32 kRayEps = 1e-4f;
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

bool Heightfield::raycast(Ray ray, f32 max_t, f32* out_t, Vec3* out_normal) const
{
    const Aabb box = bounds();

    f32 t_cur = 0.0f;
    if (!contains(box, ray.origin)) {
        if (!ray_vs_aabb(ray, box, max_t, &t_cur)) {
            return false;
        }
        t_cur += kRayEps;
    }

    const Vec3 p = ray.origin + ray.dir * t_cur;
    const i32 max_ix = static_cast<i32>(size_x_) - 2;
    const i32 max_iz = static_cast<i32>(size_z_) - 2;
    i32 ix = static_cast<i32>(f_clamp((p.x - origin_.x) / cell_size_, 0.0f,
                                      static_cast<f32>(max_ix)));
    i32 iz = static_cast<i32>(f_clamp((p.z - origin_.z) / cell_size_, 0.0f,
                                      static_cast<f32>(max_iz)));

    const i32 step_x = ray.dir.x > 0.0f ? 1 : -1;
    const i32 step_z = ray.dir.z > 0.0f ? 1 : -1;
    f32 t_max_x = kInf;
    f32 t_max_z = kInf;
    f32 t_delta_x = kInf;
    f32 t_delta_z = kInf;

    if (f_abs(ray.dir.x) > 1e-9f) {
        const f32 boundary = origin_.x + static_cast<f32>(ix + (step_x > 0 ? 1 : 0)) * cell_size_;
        t_max_x = t_cur + (boundary - p.x) / ray.dir.x;
        t_delta_x = cell_size_ / f_abs(ray.dir.x);
    }
    if (f_abs(ray.dir.z) > 1e-9f) {
        const f32 boundary = origin_.z + static_cast<f32>(iz + (step_z > 0 ? 1 : 0)) * cell_size_;
        t_max_z = t_cur + (boundary - p.z) / ray.dir.z;
        t_delta_z = cell_size_ / f_abs(ray.dir.z);
    }

    while (ix >= 0 && ix <= max_ix && iz >= 0 && iz <= max_iz && t_cur <= max_t) {
        Vec3 tris[6];
        cell_triangles(static_cast<u32>(ix), static_cast<u32>(iz), tris);

        RayHitTri hit{};
        f32 best_t = kInf;
        Vec3 best_normal{0.0f, 1.0f, 0.0f};
        for (i32 tri = 0; tri < 2; tri++) {
            const Vec3 a = tris[tri * 3 + 0];
            const Vec3 b = tris[tri * 3 + 1];
            const Vec3 c = tris[tri * 3 + 2];
            if (ray_vs_triangle(ray, a, b, c, max_t, &hit) && hit.t < best_t) {
                best_t = hit.t;
                best_normal = triangle_normal_up(a, b, c);
            }
        }
        if (best_t < kInf) {
            if (out_t) { *out_t = best_t; }
            if (out_normal) { *out_normal = best_normal; }
            return true;
        }
        if (t_max_x < t_max_z) {
            t_cur = t_max_x;
            t_max_x += t_delta_x;
            ix += step_x;
        } else {
            t_cur = t_max_z;
            t_max_z += t_delta_z;
            iz += step_z;
        }
    }
    return false;
}

} // namespace anom
