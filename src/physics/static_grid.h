#pragma once

#include "core/types.h"
#include "math/vmath.h"

#include <span>

namespace anom {

class Arena;

struct StaticTri {
    Vec3 a;
    Vec3 b;
    Vec3 c;
};

class StaticGrid {
public:
    static constexpr f32 kCellSize = 8.0f;

    void reserve(Arena& arena, u32 max_tris);
    void add(Vec3 a, Vec3 b, Vec3 c);
    void build(Arena& arena);
    void clear();

    bool raycast(Ray ray, f32 max_t, f32* out_t, Vec3* out_normal) const;

    std::span<const u32> cell_tris(i32 x, i32 z) const;
    bool cell_range(Vec3 lo, Vec3 hi, i32& x0, i32& x1, i32& z0, i32& z1) const;

    const StaticTri& tri(u32 index) const { return tris_[index]; }
    std::span<const StaticTri> tris() const { return {tris_, tri_count_}; }
    u32 tri_count() const { return tri_count_; }
    u32 dropped() const { return dropped_; }
    bool built() const { return built_; }
    Aabb bounds() const;

private:
    void tri_cell_range(const StaticTri& tri, i32& x0, i32& x1, i32& z0, i32& z1) const;

    StaticTri* tris_ = nullptr;
    u32 tri_count_ = 0;
    u32 tri_capacity_ = 0;
    u32 dropped_ = 0;
    u32* cell_first_ = nullptr;
    u32* cell_tris_ = nullptr;
    u32 cells_x_ = 0;
    u32 cells_z_ = 0;
    Vec3 origin_{0.0f, 0.0f, 0.0f};
    f32 cell_size_ = kCellSize;
    f32 min_y_ = 0.0f;
    f32 max_y_ = 0.0f;
    bool built_ = false;
};

} // namespace anom
