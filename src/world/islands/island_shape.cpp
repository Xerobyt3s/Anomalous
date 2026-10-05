#include "world/islands/island_shape.h"
#include "core/rng.h"
#include "world/islands/sdf_noise.h"

#include <cmath>

namespace anom {
namespace {

constexpr f32 kRimShape = 0.9f;
constexpr f32 kRimDropStart = 0.82f;
constexpr f32 kEdgeRound = 0.8f;
constexpr f32 kLobeBlend = 1.5f;
constexpr f32 kRootPower = 1.35f;
constexpr f32 kGrassRho = 0.86f;
constexpr f32 kNoGrass = -1000.0f;

f32 segment_distance(Vec3 p, Vec3 a, Vec3 b, f32& t)
{
    const Vec3 ab = b - a;
    t = f_clamp01(dot(p - a, ab) / f_max(dot(ab, ab), 1e-6f));
    return length(p - (a + ab * t));
}

} // namespace

IslandShape::IslandShape(const IslandParams& params) : params_(params)
{
    seed_rim_ = params.seed * 7919u + 11u;
    seed_top_ = params.seed * 104729u + 23u;
    seed_detail_ = params.seed * 1299709u + 37u;
    detail_amp_ = params.detail;
    scale_ = f_clamp(params.radius / 12.0f, 0.12f, 1.0f);

    Rng rng(static_cast<u64>(params.seed) * 31u + 5u);
    lobe_count_ = 2 + rng.range_u32(0, 2) + (params.style % 2);
    if (lobe_count_ > kMaxLobes) {
        lobe_count_ = kMaxLobes;
    }
    f32 lowest = bottom_height(0.0f);
    for (u32 i = 0; i < lobe_count_; i++) {
        const f32 angle = rng.range(0.0f, kTau);
        const f32 r = rng.range(0.15f, 0.55f) * params.radius;
        const Vec3 at{std::cos(angle) * r, 0.0f, std::sin(angle) * r};
        const f32 rho_value = r / params.radius;
        IslandLobe& lobe = lobes_[i];
        lobe.top = Vec3{at.x, bottom_height(rho_value) + 1.5f * scale_, at.z};
        lobe.bottom = Vec3{at.x * 1.1f, lobe.top.y - params.depth * rng.range(0.35f, 0.75f), at.z * 1.1f};
        lobe.radius = params.radius * rng.range(0.10f, 0.20f);
        lowest = f_min(lowest, lobe.bottom.y - lobe.radius);
    }

    const f32 reach = params.radius * 1.35f + detail_amp_ * 1.5f;
    bounds_.min = Vec3{-reach, lowest - detail_amp_ * 1.5f - 1.0f, -reach};
    bounds_.max = Vec3{reach, params.dome + params.relief + 1.5f, reach};
}

f32 IslandShape::rim_radius(f32 phi) const
{
    const f32 n = noise_fbm2(std::cos(phi) * 1.6f + 3.7f, std::sin(phi) * 1.6f - 1.3f, seed_rim_, 3);
    return params_.radius * (1.0f + (n - 0.5f) * kRimShape);
}

f32 IslandShape::rho(f32 x, f32 z) const
{
    const f32 r = std::sqrt(x * x + z * z);
    return r / f_max(rim_radius(std::atan2(z, x)), 1e-3f);
}

f32 IslandShape::top_height(f32 x, f32 z) const
{
    const f32 rho_value = f_min(rho(x, z), 1.2f);
    f32 h = params_.relief * (noise_fbm2(x * 0.055f, z * 0.055f, seed_top_, 3) - 0.5f) * 2.0f;
    h += params_.dome * (1.0f - f_min(rho_value * rho_value, 1.0f));
    if (rho_value > kRimDropStart) {
        h -= (rho_value - kRimDropStart) / (1.0f - kRimDropStart) * params_.rim_drop;
    }
    return h;
}

f32 IslandShape::bottom_height(f32 rho_value) const
{
    return -params_.thickness - params_.depth * std::pow(f_max(1.0f - rho_value, 0.0f), kRootPower);
}

f32 IslandShape::base(Vec3 p) const
{
    const f32 r = std::sqrt(p.x * p.x + p.z * p.z);
    const f32 rim = rim_radius(std::atan2(p.z, p.x));
    const f32 rho_value = r / f_max(rim, 1e-3f);
    const f32 vertical = f_max(p.y - top_height(p.x, p.z), bottom_height(f_min(rho_value, 1.0f)) - p.y);
    const f32 radial = (r - rim) * kRimShape;
    f32 d = smooth_max(vertical, radial, kEdgeRound * scale_);
    for (u32 i = 0; i < lobe_count_; i++) {
        f32 t = 0.0f;
        const f32 seg = segment_distance(p, lobes_[i].top, lobes_[i].bottom, t);
        const f32 lobe = seg - lobes_[i].radius * (1.0f - 0.75f * t);
        d = smooth_min(d, lobe, kLobeBlend * scale_);
    }
    return d;
}

f32 IslandShape::sdf(Vec3 p) const
{
    const f32 d = base(p);
    const f32 below = (top_height(p.x, p.z) - p.y) / scale_;
    const f32 w = smoothstep01(0.25f, 2.2f, below);
    const f32 rho_value = rho(p.x, p.z);
    const f32 crumble = smoothstep01(0.7f, 1.0f, rho_value) * (1.0f - w)
                      * (noise_fbm3(p * (0.45f / scale_), seed_detail_ + 9u, 3) - 0.5f) * scale_;
    if (w <= 0.0f) {
        return d + crumble;
    }
    const Vec3 q = noise_warp3(p * 0.16f, seed_detail_, 0.6f);
    const f32 ridged = (noise_ridged3(q, seed_detail_ + 3u, 4) - 0.45f) * detail_amp_ * 1.6f;
    const f32 fine = (noise_fbm3(p * 0.7f, seed_detail_ + 5u, 3) - 0.5f) * 0.35f;
    const f32 strata = 0.12f * scale_ * std::sin(p.y * 2.4f / scale_ + 3.0f * value_noise3(p * 0.1f, seed_detail_ + 7u));
    return d + w * (ridged + fine + strata) + crumble;
}

f32 IslandShape::surface_class(Vec3 p, Vec3 n) const
{
    const f32 below = (top_height(p.x, p.z) - p.y) / scale_;
    const f32 jitter = (value_noise3(p * 0.5f, seed_top_ + 13u) - 0.5f);
    f32 c = 0.5f * smoothstep01(0.2f, 0.45f, below + jitter * 0.3f)
          + 0.5f * smoothstep01(1.0f, 1.6f, below + jitter * 0.8f);
    if (n.y < 0.55f) {
        c = f_max(c, 0.5f + 0.5f * smoothstep01(0.55f, 0.15f, n.y));
    }
    return f_clamp01(c);
}

f32 island_sdf_fn(Vec3 p, const void* user)
{
    return static_cast<const IslandShape*>(user)->sdf(p);
}

f32 island_base_fn(Vec3 p, const void* user)
{
    return static_cast<const IslandShape*>(user)->base(p);
}

f32 bake_ambient_occlusion(SdfFn sdf, const void* user, Vec3 p, Vec3 n, f32 step)
{
    f32 occlusion = 0.0f;
    f32 weight = 0.5f;
    for (u32 i = 1; i <= 4; i++) {
        const f32 h = step * static_cast<f32>(i);
        occlusion += weight * f_max(h - sdf(p + n * h, user), 0.0f) / h;
        weight *= 0.5f;
    }
    return f_clamp(1.0f - occlusion * 1.4f, 0.25f, 1.0f);
}

void bake_island_mesh(const IslandShape& shape, f32 cell, BakedMesh& out)
{
    SdfMesh mesh;
    const f32 band = shape.detail_amplitude() * 1.2f + 0.8f + cell * 1.8f;
    surface_nets(sdf_grid_for(shape.bounds(), cell), island_sdf_fn, island_base_fn, band, &shape, mesh);
    out.vertices.resize(mesh.positions.size());
    parallel_for(static_cast<u32>(mesh.positions.size()), [&](u32 begin, u32 end) {
        for (u32 i = begin; i < end; i++) {
            const Vec3 p = mesh.positions[i];
            const Vec3 n = mesh.normals[i];
            AmshVertex& v = out.vertices[i];
            v.pos[0] = p.x;
            v.pos[1] = p.y;
            v.pos[2] = p.z;
            v.normal[0] = n.x;
            v.normal[1] = n.y;
            v.normal[2] = n.z;
            v.uv[0] = shape.surface_class(p, n);
            v.uv[1] = bake_ambient_occlusion(island_sdf_fn, &shape, p, n, 0.6f);
        }
    });
    out.bounds = aabb_empty();
    for (const Vec3& p : mesh.positions) {
        out.bounds = expand(out.bounds, p);
    }
    out.indices = std::move(mesh.indices);
}

void bake_island_collision(const IslandShape& shape, f32 cell, std::vector<Vec3>& out_tris)
{
    SdfMesh mesh;
    const f32 band = shape.detail_amplitude() * 1.2f + 0.8f + cell * 1.8f;
    surface_nets(sdf_grid_for(shape.bounds(), cell), island_sdf_fn, island_base_fn, band, &shape, mesh);
    out_tris.clear();
    out_tris.reserve(mesh.indices.size());
    for (u32 index : mesh.indices) {
        out_tris.push_back(mesh.positions[index]);
    }
}

void bake_island_grass(const IslandShape& shape, u32 res, f32 extent, std::vector<f32>& out)
{
    out.assign(static_cast<size_t>(res) * res, kNoGrass);
    const f32 step = 2.0f * extent / static_cast<f32>(res);
    for (u32 j = 0; j < res; j++) {
        const f32 z = -extent + (static_cast<f32>(j) + 0.5f) * step;
        for (u32 i = 0; i < res; i++) {
            const f32 x = -extent + (static_cast<f32>(i) + 0.5f) * step;
            if (shape.rho(x, z) < kGrassRho) {
                out[static_cast<size_t>(j) * res + i] = shape.top_height(x, z);
            }
        }
    }
}

} // namespace anom
