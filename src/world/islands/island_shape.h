#pragma once

#include "assets/amsh.h"
#include "core/types.h"
#include "math/vmath.h"
#include "world/islands/surface_nets.h"

#include <vector>

namespace anom {

struct IslandParams {
    f32 radius = 14.0f;
    f32 depth = 12.0f;
    f32 thickness = 1.6f;
    f32 relief = 0.6f;
    f32 detail = 1.2f;
    f32 dome = 0.45f;
    f32 rim_drop = 0.7f;
    u32 seed = 1;
    u32 style = 0;
};

struct IslandLobe {
    Vec3 top{};
    Vec3 bottom{};
    f32 radius = 1.0f;
};

class IslandShape {
public:
    static constexpr u32 kMaxLobes = 5;

    explicit IslandShape(const IslandParams& params);

    f32 sdf(Vec3 p) const;
    f32 base(Vec3 p) const;
    f32 top_height(f32 x, f32 z) const;
    f32 rim_radius(f32 phi) const;
    f32 rho(f32 x, f32 z) const;
    f32 detail_amplitude() const { return detail_amp_; }
    Aabb bounds() const { return bounds_; }
    const IslandParams& params() const { return params_; }
    f32 surface_class(Vec3 p, Vec3 n) const;
    f32 bottom_height(f32 rho_value) const;

private:
    IslandParams params_;
    IslandLobe lobes_[kMaxLobes];
    u32 lobe_count_ = 0;
    f32 detail_amp_ = 1.0f;
    f32 scale_ = 1.0f;
    Aabb bounds_{};
    u32 seed_rim_ = 0;
    u32 seed_top_ = 0;
    u32 seed_detail_ = 0;
};

struct BakedMesh {
    std::vector<AmshVertex> vertices;
    std::vector<u32> indices;
    Aabb bounds = aabb_empty();
};

f32 island_sdf_fn(Vec3 p, const void* user);
f32 island_base_fn(Vec3 p, const void* user);
void bake_island_mesh(const IslandShape& shape, f32 cell, BakedMesh& out);
void bake_island_collision(const IslandShape& shape, f32 cell, std::vector<Vec3>& out_tris);
void bake_island_grass(const IslandShape& shape, u32 res, f32 extent, std::vector<f32>& out);
f32 bake_ambient_occlusion(SdfFn sdf, const void* user, Vec3 p, Vec3 n, f32 step);

} // namespace anom
