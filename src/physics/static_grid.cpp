#include "physics/static_grid.h"
#include "core/arena.h"
#include "core/log.h"

namespace anom {

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

} // namespace anom
