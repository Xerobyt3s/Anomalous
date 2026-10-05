#include "world/islands/surface_nets.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace anom {
namespace {

constexpr i32 kCorner[8][3] = {
    {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1},
};

constexpr i32 kEdge[12][2] = {
    {0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
};

} // namespace

void parallel_for(u32 count, const std::function<void(u32 begin, u32 end)>& body)
{
    const u32 hw = std::max(1u, std::thread::hardware_concurrency());
    const u32 workers = std::min(hw, std::max(1u, count / 4u));
    if (workers <= 1) {
        body(0, count);
        return;
    }
    std::vector<std::thread> threads;
    threads.reserve(workers);
    const u32 chunk = (count + workers - 1) / workers;
    for (u32 w = 0; w < workers; w++) {
        const u32 begin = w * chunk;
        const u32 end = std::min(count, begin + chunk);
        if (begin >= end) {
            break;
        }
        threads.emplace_back([&body, begin, end] { body(begin, end); });
    }
    for (std::thread& t : threads) {
        t.join();
    }
}

SdfGrid sdf_grid_for(Aabb bounds, f32 cell)
{
    SdfGrid grid;
    grid.cell = cell;
    grid.origin = bounds.min - Vec3{cell, cell, cell};
    const Vec3 span = bounds.max - bounds.min;
    grid.nx = static_cast<i32>(std::ceil(span.x / cell)) + 2;
    grid.ny = static_cast<i32>(std::ceil(span.y / cell)) + 2;
    grid.nz = static_cast<i32>(std::ceil(span.z / cell)) + 2;
    return grid;
}

Vec3 sdf_gradient(SdfFn sdf, const void* user, Vec3 p, f32 eps)
{
    const Vec3 g{sdf(p + Vec3{eps, 0.0f, 0.0f}, user) - sdf(p - Vec3{eps, 0.0f, 0.0f}, user),
                 sdf(p + Vec3{0.0f, eps, 0.0f}, user) - sdf(p - Vec3{0.0f, eps, 0.0f}, user),
                 sdf(p + Vec3{0.0f, 0.0f, eps}, user) - sdf(p - Vec3{0.0f, 0.0f, eps}, user)};
    const Vec3 n = normalize(g);
    return length_sq(n) > 0.0f ? n : Vec3{0.0f, 1.0f, 0.0f};
}

void surface_nets(const SdfGrid& grid, SdfFn sdf, SdfFn bound, f32 band, const void* user,
                  SdfMesh& out)
{
    out.positions.clear();
    out.normals.clear();
    out.indices.clear();
    const i32 cx = grid.nx + 1;
    const i32 cy = grid.ny + 1;
    const i32 cz = grid.nz + 1;
    if (grid.nx <= 0 || grid.ny <= 0 || grid.nz <= 0) {
        return;
    }
    const auto corner_index = [cx, cy](i32 x, i32 y, i32 z) {
        return static_cast<size_t>((z * cy + y) * cx + x);
    };
    const auto corner_pos = [&grid](i32 x, i32 y, i32 z) {
        return grid.origin + Vec3{static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(z)} * grid.cell;
    };

    std::vector<f32> values(static_cast<size_t>(cx) * cy * cz);
    parallel_for(static_cast<u32>(cz), [&](u32 z0, u32 z1) {
        for (i32 z = static_cast<i32>(z0); z < static_cast<i32>(z1); z++) {
            for (i32 y = 0; y < cy; y++) {
                for (i32 x = 0; x < cx; x++) {
                    const Vec3 p = corner_pos(x, y, z);
                    f32 v = bound ? bound(p, user) : 0.0f;
                    if (!bound || f_abs(v) <= band) {
                        v = sdf(p, user);
                    }
                    const bool edge = x == 0 || y == 0 || z == 0 || x == cx - 1 || y == cy - 1 || z == cz - 1;
                    values[corner_index(x, y, z)] = edge ? f_max(v, grid.cell * 0.5f) : v;
                }
            }
        }
    });

    const auto cell_index = [&grid](i32 x, i32 y, i32 z) {
        return static_cast<size_t>((z * grid.ny + y) * grid.nx + x);
    };
    std::vector<i32> vertex_of(static_cast<size_t>(grid.nx) * grid.ny * grid.nz, -1);
    for (i32 z = 0; z < grid.nz; z++) {
        for (i32 y = 0; y < grid.ny; y++) {
            for (i32 x = 0; x < grid.nx; x++) {
                f32 v[8];
                u32 mask = 0;
                for (u32 c = 0; c < 8; c++) {
                    v[c] = values[corner_index(x + kCorner[c][0], y + kCorner[c][1], z + kCorner[c][2])];
                    mask |= (v[c] < 0.0f ? 1u : 0u) << c;
                }
                if (mask == 0 || mask == 255) {
                    continue;
                }
                Vec3 sum{};
                u32 crossings = 0;
                for (const auto& e : kEdge) {
                    const f32 a = v[e[0]];
                    const f32 b = v[e[1]];
                    if ((a < 0.0f) == (b < 0.0f)) {
                        continue;
                    }
                    const f32 t = a / (a - b);
                    const Vec3 pa{static_cast<f32>(kCorner[e[0]][0]), static_cast<f32>(kCorner[e[0]][1]),
                                  static_cast<f32>(kCorner[e[0]][2])};
                    const Vec3 pb{static_cast<f32>(kCorner[e[1]][0]), static_cast<f32>(kCorner[e[1]][1]),
                                  static_cast<f32>(kCorner[e[1]][2])};
                    sum += lerp(pa, pb, t);
                    crossings++;
                }
                const Vec3 local = sum * (1.0f / static_cast<f32>(crossings));
                const Vec3 p = grid.origin
                             + (Vec3{static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(z)} + local) * grid.cell;
                vertex_of[cell_index(x, y, z)] = static_cast<i32>(out.positions.size());
                out.positions.push_back(p);
            }
        }
    }

    out.normals.resize(out.positions.size());
    parallel_for(static_cast<u32>(out.positions.size()), [&](u32 begin, u32 end) {
        for (u32 i = begin; i < end; i++) {
            out.normals[i] = sdf_gradient(sdf, user, out.positions[i], grid.cell * 0.5f);
        }
    });

    const auto emit = [&out](i32 a, i32 b, i32 c, i32 d, Vec3 outward) {
        const Vec3 pa = out.positions[static_cast<size_t>(a)];
        const Vec3 pb = out.positions[static_cast<size_t>(b)];
        const Vec3 pc = out.positions[static_cast<size_t>(c)];
        const Vec3 pd = out.positions[static_cast<size_t>(d)];
        const bool split_ac = length_sq(pa - pc) <= length_sq(pb - pd);
        u32 tri[6];
        if (split_ac) {
            const u32 t[6] = {static_cast<u32>(a), static_cast<u32>(b), static_cast<u32>(c),
                              static_cast<u32>(a), static_cast<u32>(c), static_cast<u32>(d)};
            for (u32 i = 0; i < 6; i++) {
                tri[i] = t[i];
            }
        } else {
            const u32 t[6] = {static_cast<u32>(a), static_cast<u32>(b), static_cast<u32>(d),
                              static_cast<u32>(b), static_cast<u32>(c), static_cast<u32>(d)};
            for (u32 i = 0; i < 6; i++) {
                tri[i] = t[i];
            }
        }
        for (u32 k = 0; k < 6; k += 3) {
            const Vec3 p0 = out.positions[tri[k]];
            const Vec3 p1 = out.positions[tri[k + 1]];
            const Vec3 p2 = out.positions[tri[k + 2]];
            if (dot(cross(p1 - p0, p2 - p0), outward) < 0.0f) {
                const u32 swap = tri[k + 1];
                tri[k + 1] = tri[k + 2];
                tri[k + 2] = swap;
            }
            out.indices.push_back(tri[k]);
            out.indices.push_back(tri[k + 1]);
            out.indices.push_back(tri[k + 2]);
        }
    };

    for (i32 z = 1; z < grid.nz; z++) {
        for (i32 y = 1; y < grid.ny; y++) {
            for (i32 x = 1; x < grid.nx; x++) {
                const f32 v0 = values[corner_index(x, y, z)];
                const bool in0 = v0 < 0.0f;
                const f32 vx = values[corner_index(x + 1, y, z)];
                if (in0 != (vx < 0.0f)) {
                    const i32 a = vertex_of[cell_index(x, y - 1, z - 1)];
                    const i32 b = vertex_of[cell_index(x, y, z - 1)];
                    const i32 c = vertex_of[cell_index(x, y, z)];
                    const i32 d = vertex_of[cell_index(x, y - 1, z)];
                    if (a >= 0 && b >= 0 && c >= 0 && d >= 0) {
                        emit(a, b, c, d, Vec3{in0 ? 1.0f : -1.0f, 0.0f, 0.0f});
                    }
                }
                const f32 vy = values[corner_index(x, y + 1, z)];
                if (in0 != (vy < 0.0f)) {
                    const i32 a = vertex_of[cell_index(x - 1, y, z - 1)];
                    const i32 b = vertex_of[cell_index(x, y, z - 1)];
                    const i32 c = vertex_of[cell_index(x, y, z)];
                    const i32 d = vertex_of[cell_index(x - 1, y, z)];
                    if (a >= 0 && b >= 0 && c >= 0 && d >= 0) {
                        emit(a, b, c, d, Vec3{0.0f, in0 ? 1.0f : -1.0f, 0.0f});
                    }
                }
                const f32 vz = values[corner_index(x, y, z + 1)];
                if (in0 != (vz < 0.0f)) {
                    const i32 a = vertex_of[cell_index(x - 1, y - 1, z)];
                    const i32 b = vertex_of[cell_index(x, y - 1, z)];
                    const i32 c = vertex_of[cell_index(x, y, z)];
                    const i32 d = vertex_of[cell_index(x - 1, y, z)];
                    if (a >= 0 && b >= 0 && c >= 0 && d >= 0) {
                        emit(a, b, c, d, Vec3{0.0f, 0.0f, in0 ? 1.0f : -1.0f});
                    }
                }
            }
        }
    }
}

} // namespace anom
