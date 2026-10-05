#include "physics/static_grid.h"
#include "core/arena.h"
#include "core/log.h"

namespace anom {
namespace {

constexpr f32 kRayEps = 1e-4f;
constexpr f32 kInf = 1e30f;

} // namespace

void StaticGrid::reserve(Arena& arena, u32 max_tris)
{
    tris_ = arena.push_array<StaticTri>(max_tris);
    tri_capacity_ = max_tris;
    tri_count_ = 0;
    dropped_ = 0;
    built_ = false;
}

void StaticGrid::add(Vec3 a, Vec3 b, Vec3 c)
{
    if (!tris_ || tri_count_ >= tri_capacity_ || built_) {
        dropped_++;
        return;
    }
    tris_[tri_count_++] = StaticTri{a, b, c};
}

void StaticGrid::clear()
{
    tri_count_ = 0;
    dropped_ = 0;
    built_ = false;
}

void StaticGrid::tri_cell_range(const StaticTri& tri, i32& x0, i32& x1, i32& z0, i32& z1) const
{
    const Vec3 lo = vec_min(tri.a, vec_min(tri.b, tri.c));
    const Vec3 hi = vec_max(tri.a, vec_max(tri.b, tri.c));
    const f32 max_x = static_cast<f32>(cells_x_ - 1);
    const f32 max_z = static_cast<f32>(cells_z_ - 1);
    x0 = static_cast<i32>(f_clamp((lo.x - origin_.x) / cell_size_, 0.0f, max_x));
    x1 = static_cast<i32>(f_clamp((hi.x - origin_.x) / cell_size_, 0.0f, max_x));
    z0 = static_cast<i32>(f_clamp((lo.z - origin_.z) / cell_size_, 0.0f, max_z));
    z1 = static_cast<i32>(f_clamp((hi.z - origin_.z) / cell_size_, 0.0f, max_z));
}

void StaticGrid::build(Arena& arena)
{
    if (dropped_ > 0) {
        log_warn("statics: %u triangles dropped past the budget of %u", dropped_, tri_capacity_);
    }
    if (!tri_count_) {
        built_ = false;
        return;
    }

    Aabb box = aabb_empty();
    for (u32 i = 0; i < tri_count_; i++) {
        box = expand(box, tris_[i].a);
        box = expand(box, tris_[i].b);
        box = expand(box, tris_[i].c);
    }

    cell_size_ = kCellSize;
    origin_ = Vec3{box.min.x - 0.5f, 0.0f, box.min.z - 0.5f};
    min_y_ = box.min.y - 0.5f;
    max_y_ = box.max.y + 0.5f;
    cells_x_ = static_cast<u32>((box.max.x - origin_.x) / cell_size_) + 2;
    cells_z_ = static_cast<u32>((box.max.z - origin_.z) / cell_size_) + 2;

    const u32 cell_count = cells_x_ * cells_z_;
    u32* counts = arena.push_array<u32>(cell_count + 1);
    if (!counts) {
        return;
    }

    u64 total_refs = 0;
    for (u32 i = 0; i < tri_count_; i++) {
        i32 x0 = 0, x1 = 0, z0 = 0, z1 = 0;
        tri_cell_range(tris_[i], x0, x1, z0, z1);
        for (i32 z = z0; z <= z1; z++) {
            for (i32 x = x0; x <= x1; x++) {
                counts[static_cast<u32>(z) * cells_x_ + static_cast<u32>(x)]++;
                total_refs++;
            }
        }
    }

    cell_first_ = arena.push_array<u32>(cell_count + 1);
    if (!cell_first_) {
        return;
    }
    u32 running = 0;
    for (u32 i = 0; i < cell_count; i++) {
        cell_first_[i] = running;
        running += counts[i];
        counts[i] = cell_first_[i];
    }
    cell_first_[cell_count] = running;

    cell_tris_ = arena.push_array<u32>(total_refs ? total_refs : 1);
    if (!cell_tris_) {
        return;
    }
    for (u32 i = 0; i < tri_count_; i++) {
        i32 x0 = 0, x1 = 0, z0 = 0, z1 = 0;
        tri_cell_range(tris_[i], x0, x1, z0, z1);
        for (i32 z = z0; z <= z1; z++) {
            for (i32 x = x0; x <= x1; x++) {
                cell_tris_[counts[static_cast<u32>(z) * cells_x_ + static_cast<u32>(x)]++] = i;
            }
        }
    }
    built_ = true;
    log_info("statics: %u tris, %ux%u cells, %llu refs", tri_count_, cells_x_, cells_z_,
             static_cast<unsigned long long>(total_refs));
}

Aabb StaticGrid::bounds() const
{
    return Aabb{Vec3{origin_.x, min_y_, origin_.z},
                Vec3{origin_.x + static_cast<f32>(cells_x_) * cell_size_, max_y_,
                     origin_.z + static_cast<f32>(cells_z_) * cell_size_}};
}

std::span<const u32> StaticGrid::cell_tris(i32 x, i32 z) const
{
    if (!built_ || x < 0 || z < 0 || static_cast<u32>(x) >= cells_x_
        || static_cast<u32>(z) >= cells_z_) {
        return {};
    }
    const u32 cell = static_cast<u32>(z) * cells_x_ + static_cast<u32>(x);
    const u32 begin = cell_first_[cell];
    const u32 end = cell_first_[cell + 1];
    return {cell_tris_ + begin, end - begin};
}

bool StaticGrid::cell_range(Vec3 lo, Vec3 hi, i32& x0, i32& x1, i32& z0, i32& z1) const
{
    if (!built_) {
        return false;
    }
    const f32 max_x = static_cast<f32>(cells_x_ - 1);
    const f32 max_z = static_cast<f32>(cells_z_ - 1);
    x0 = static_cast<i32>(f_clamp((lo.x - origin_.x) / cell_size_, 0.0f, max_x));
    x1 = static_cast<i32>(f_clamp((hi.x - origin_.x) / cell_size_, 0.0f, max_x));
    z0 = static_cast<i32>(f_clamp((lo.z - origin_.z) / cell_size_, 0.0f, max_z));
    z1 = static_cast<i32>(f_clamp((hi.z - origin_.z) / cell_size_, 0.0f, max_z));
    return true;
}

bool StaticGrid::raycast(Ray ray, f32 max_t, f32* out_t, Vec3* out_normal) const
{
    if (!built_) {
        return false;
    }
    const Aabb box = bounds();

    f32 t_cur = 0.0f;
    if (!contains(box, ray.origin)) {
        if (!ray_vs_aabb(ray, box, max_t, &t_cur)) {
            return false;
        }
        t_cur += kRayEps;
    }

    const Vec3 p = ray.origin + ray.dir * t_cur;
    const i32 max_ix = static_cast<i32>(cells_x_) - 1;
    const i32 max_iz = static_cast<i32>(cells_z_) - 1;
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

    f32 best_t = max_t;
    Vec3 best_normal{0.0f, 1.0f, 0.0f};
    bool found = false;

    while (ix >= 0 && ix <= max_ix && iz >= 0 && iz <= max_iz && t_cur <= best_t) {
        for (const u32 index : cell_tris(ix, iz)) {
            const StaticTri& t = tris_[index];
            RayHitTri hit{};
            if (ray_vs_triangle(ray, t.a, t.b, t.c, best_t, &hit) && hit.t < best_t) {
                best_t = hit.t;
                Vec3 n = normalize(cross(t.b - t.a, t.c - t.a));
                if (dot(n, ray.dir) > 0.0f) {
                    n = -n;
                }
                best_normal = n;
                found = true;
            }
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

    if (found) {
        if (out_t) { *out_t = best_t; }
        if (out_normal) { *out_normal = best_normal; }
    }
    return found;
}

} // namespace anom
