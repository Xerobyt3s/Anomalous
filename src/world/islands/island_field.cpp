#include "world/islands/island_field.h"
#include "core/arena.h"
#include "core/log.h"
#include "core/rng.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "world/entity.h"
#include "world/islands/sdf_noise.h"
#include "world/islands/surface_nets.h"
#include "world/terrain.h"
#include "world/zone.h"

#include <chrono>
#include <cmath>

namespace anom {
namespace {

constexpr f32 kRenderCell = 0.4f;
constexpr f32 kRenderCellLarge = 0.5f;
constexpr f32 kCollisionCell = 0.9f;
constexpr f32 kAnchorRim = 0.72f;
constexpr f32 kMinTurnRadius = 10.0f;
constexpr f32 kMaxTurnPerMetre = 6.0f;
constexpr u32 kWindDense = 256;
constexpr u32 kWindShapes = 3;
constexpr f32 kHandleScales[] = {0.3f, 0.45f, 0.65f, 0.9f, 1.2f};
constexpr f32 kWindScales[] = {0.0f, 0.15f, 0.3f, 0.5f, 0.75f};
constexpr f32 kLiftScales[] = {0.0f, 0.2f, 0.45f};
constexpr f32 kAnchorTurns[] = {0.0f, 25.0f, -25.0f, 50.0f, -50.0f};
constexpr f32 kAnchorTurnCost = 0.04f;
constexpr f32 kGroundTurns[] = {0.0f, 35.0f, -35.0f, 70.0f, -70.0f, 110.0f, -110.0f};
constexpr f32 kGroundTurnCost = 0.5f;
constexpr f32 kRoadClearHeight = 14.0f;
constexpr f32 kRoadMargin = 1.5f;
constexpr f32 kRoadThreshold = 0.25f;
constexpr f32 kRoadCost = 400.0f;
constexpr f32 kTwistEase = 0.5f;
constexpr f32 kBlockedCost = 10000.0f;
constexpr f32 kUnderCost = 200.0f;
constexpr f32 kBendCost = 30.0f;
constexpr f32 kTwistCost = 25.0f;
constexpr f32 kLengthCost = 0.15f;
constexpr f32 kRibbonThickness = 0.8f;
constexpr f32 kPathHeight = 4.5f;
constexpr f32 kPathBelow = 1.2f;
constexpr f32 kPathFalloff = 2.5f;
constexpr f32 kPathMargin = 0.6f;
constexpr f32 kIslandGravityHeight = 2.6f;
constexpr f32 kIslandGravityFalloff = 3.0f;
constexpr f32 kRockCell = 0.08f;
constexpr f32 kTurfCell = 0.12f;
constexpr f32 kChunkSpacing = 2.0f;
constexpr f32 kChunkKeep = 0.74f;
constexpr f32 kPathClearance = 0.3f;
constexpr f32 kGroundSettle = 0.25f;
constexpr f32 kGroundDipAllowed = -0.5f;
constexpr u32 kVariantSeed = 90210u;
constexpr f32 kHullCell = 0.3f;
constexpr f32 kOverlapStep = 1.5f;
constexpr f32 kIslandBoundGrow = 1.35f;
constexpr f32 kSharedEndSkip = 6.0f;

struct RockShape {
    Vec3 normals[7];
    f32 offsets[7];
    Vec3 stretch;
    u32 seed;
    u32 planes;
};

f32 rock_sdf_fn(Vec3 p, const void* user)
{
    const RockShape& r = *static_cast<const RockShape*>(user);
    const Vec3 q{p.x / r.stretch.x, p.y / r.stretch.y, p.z / r.stretch.z};
    const f32 min_stretch = f_min(r.stretch.x, f_min(r.stretch.y, r.stretch.z));
    f32 d = (length(q) - 1.0f) * min_stretch;
    for (u32 i = 0; i < r.planes; i++) {
        d = f_max(d, dot(p, r.normals[i]) - r.offsets[i]);
    }
    return d + (noise_fbm3(p * 2.2f, r.seed, 3) - 0.5f) * 0.18f;
}

f32 rock_bound_fn(Vec3 p, const void* user)
{
    const RockShape& r = *static_cast<const RockShape*>(user);
    const Vec3 q{p.x / r.stretch.x, p.y / r.stretch.y, p.z / r.stretch.z};
    return (length(q) - 1.0f) * f_min(r.stretch.x, f_min(r.stretch.y, r.stretch.z));
}

RockShape rock_shape(u32 seed)
{
    Rng rng(seed);
    RockShape shape{};
    shape.seed = seed;
    shape.stretch = Vec3{rng.range(0.8f, 1.15f), rng.range(0.55f, 0.9f), rng.range(0.75f, 1.1f)};
    shape.planes = 4 + rng.range_u32(0, 3);
    for (u32 i = 0; i < shape.planes; i++) {
        shape.normals[i] = rng_unit_vector(rng);
        shape.offsets[i] = rng.range(0.45f, 0.8f);
    }
    return shape;
}

constexpr Aabb kRockBox{Vec3{-1.3f, -1.3f, -1.3f}, Vec3{1.3f, 1.3f, 1.3f}};

void bake_rock_hull(u32 seed, std::vector<Vec3>& out)
{
    const RockShape shape = rock_shape(seed);
    SdfMesh mesh;
    surface_nets(sdf_grid_for(kRockBox, kHullCell), rock_sdf_fn, rock_bound_fn, 0.6f, &shape, mesh);
    out.clear();
    out.reserve(mesh.indices.size());
    for (u32 index : mesh.indices) {
        out.push_back(mesh.positions[index]);
    }
}

f32 mesh_reach(const BakedMesh& mesh)
{
    f32 reach = 0.0f;
    for (const AmshVertex& v : mesh.vertices) {
        reach = f_max(reach, std::sqrt(v.pos[0] * v.pos[0] + v.pos[1] * v.pos[1] + v.pos[2] * v.pos[2]));
    }
    return reach;
}

IslandParams turf_params(u32 i)
{
    Rng rng(kVariantSeed * 3u + i);
    IslandParams params;
    params.radius = 1.1f;
    params.depth = rng.range(0.9f, 1.6f);
    params.thickness = 0.3f;
    params.relief = 0.06f;
    params.detail = 0.25f;
    params.dome = 0.06f;
    params.rim_drop = 0.15f;
    params.seed = kVariantSeed + 31u * i;
    params.style = i;
    return params;
}

f32 point_segment_distance(Vec3 p, Vec3 a, Vec3 b)
{
    const Vec3 ab = b - a;
    const f32 len_sq = length_sq(ab);
    const f32 t = len_sq > 1e-8f ? f_clamp01(dot(p - a, ab) / len_sq) : 0.0f;
    return length(p - (a + ab * t));
}

Vec3 island_bound_centre(Vec3 pos, Quat rot, const IslandParams& params)
{
    return pos + rotate(rot, Vec3{0.0f, -params.depth * 0.4f, 0.0f});
}

f32 island_bound_radius(const IslandParams& params)
{
    const f32 r = params.radius * kIslandBoundGrow;
    const f32 h = params.depth * 0.6f + 2.0f;
    return std::sqrt(r * r + h * h);
}

void bake_rock(u32 seed, BakedMesh& out)
{
    const RockShape shape = rock_shape(seed);
    SdfMesh mesh;
    surface_nets(sdf_grid_for(kRockBox, kRockCell), rock_sdf_fn, rock_bound_fn, 0.5f, &shape, mesh);
    out.vertices.resize(mesh.positions.size());
    out.bounds = aabb_empty();
    for (size_t i = 0; i < mesh.positions.size(); i++) {
        const Vec3 p = mesh.positions[i];
        const Vec3 n = mesh.normals[i];
        AmshVertex& v = out.vertices[i];
        v.pos[0] = p.x;
        v.pos[1] = p.y;
        v.pos[2] = p.z;
        v.normal[0] = n.x;
        v.normal[1] = n.y;
        v.normal[2] = n.z;
        v.uv[0] = 1.0f;
        v.uv[1] = bake_ambient_occlusion(rock_sdf_fn, &shape, p, n, 0.12f);
        out.bounds = expand(out.bounds, p);
    }
    out.indices = std::move(mesh.indices);
}

Vec3 bezier(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, f32 t)
{
    const f32 u = 1.0f - t;
    return p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) + p3 * (t * t * t);
}

Vec3 bezier_tangent(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, f32 t)
{
    const f32 u = 1.0f - t;
    const Vec3 d = (p1 - p0) * (3.0f * u * u) + (p2 - p1) * (6.0f * u * t) + (p3 - p2) * (3.0f * t * t);
    return length_sq(d) > 1e-10f ? normalize(d) : normalize(p3 - p0);
}

Quat frame_quat(Vec3 forward, Vec3 up)
{
    const Vec3 f = normalize(forward);
    const Vec3 u = normalize(up - f * dot(up, f));
    const Vec3 r = cross(f, u);
    const Quat align_up = quat_from_to(Vec3{0.0f, 1.0f, 0.0f}, u);
    const Vec3 x_after = rotate(align_up, Vec3{1.0f, 0.0f, 0.0f});
    const f32 angle = std::atan2(dot(cross(x_after, r), u), dot(x_after, r));
    return normalize(quat_from_axis_angle(u, angle) * align_up);
}

IslandParams params_for(const Entity& e)
{
    IslandParams params;
    params.radius = f_clamp(e.half.x, 3.0f, kIslandMaxRadius);
    params.depth = e.aux_value > 0.0f ? e.aux_value : params.radius * 0.8f;
    params.thickness = 1.2f + params.radius * 0.04f;
    params.relief = 0.5f;
    params.detail = 0.8f + params.radius * 0.03f;
    params.seed = e.aux_data;
    params.style = e.aux_kind & 0xFFu;
    return params;
}

struct Anchor {
    Vec3 point;
    Vec3 up;
    Vec3 out;
};

struct PathEnds {
    Vec3 p0;
    Vec3 t0;
    Vec3 u0;
    Vec3 p3;
    Vec3 t3;
    Vec3 u3;
};

struct WindParams {
    f32 handle = 1.0f;
    f32 amp = 0.0f;
    f32 lift = 0.0f;
    u32 shape = 0;
};

f32 wind_profile(u32 shape, f32 s)
{
    const f32 arc = std::sin(kPi * s);
    if (shape == 2) {
        return arc * std::sin(kTau * s) * 1.3f;
    }
    return arc * arc;
}

void sample_wind(const PathEnds& e, const WindParams& w, GravityPath& path)
{
    Vec3 dense[kWindDense];
    const Vec3 p1 = e.p0 + e.t0 * w.handle;
    const Vec3 p2 = e.p3 - e.t3 * w.handle;
    const Vec3 along = normalize(e.p3 - e.p0);
    Vec3 mean_up = e.u0 + e.u3;
    mean_up = length_sq(mean_up - along * dot(mean_up, along)) > 1e-4f ? mean_up : any_perpendicular(along);
    const Vec3 side = normalize(cross(along, mean_up));
    const Vec3 rise = normalize(cross(side, along));
    for (u32 i = 0; i < kWindDense; i++) {
        const f32 s = static_cast<f32>(i) / static_cast<f32>(kWindDense - 1);
        const f32 arc = std::sin(kPi * s);
        dense[i] = bezier(e.p0, p1, p2, e.p3, s) + side * (w.amp * wind_profile(w.shape, s)) + rise * (w.lift * arc * arc);
    }
    f32 lengths[kWindDense];
    lengths[0] = 0.0f;
    for (u32 i = 1; i < kWindDense; i++) {
        lengths[i] = lengths[i - 1] + length(dense[i] - dense[i - 1]);
    }
    const f32 total = f_max(lengths[kWindDense - 1], 1e-3f);
    const u32 n = kGravityPathSamples;
    u32 j = 0;
    for (u32 i = 0; i < n; i++) {
        const f32 want = total * static_cast<f32>(i) / static_cast<f32>(n - 1);
        while (j + 2 < kWindDense && lengths[j + 1] < want) {
            j++;
        }
        const f32 seg = f_max(lengths[j + 1] - lengths[j], 1e-5f);
        path.points[i] = lerp(dense[j], dense[j + 1], f_clamp01((want - lengths[j]) / seg));
    }
    path.points[0] = e.p0;
    path.points[n - 1] = e.p3;
    path.count = n;

    Vec3 tangents[kGravityPathSamples];
    tangents[0] = e.t0;
    tangents[n - 1] = e.t3;
    for (u32 i = 1; i + 1 < n; i++) {
        tangents[i] = normalize(path.points[i + 1] - path.points[i - 1]);
    }
    Vec3 carried = e.u0 - tangents[0] * dot(e.u0, tangents[0]);
    carried = length_sq(carried) > 1e-6f ? normalize(carried) : any_perpendicular(tangents[0]);
    path.ups[0] = carried;
    for (u32 i = 1; i < n; i++) {
        carried = rotate(quat_from_to(tangents[i - 1], tangents[i]), carried);
        carried = carried - tangents[i] * dot(carried, tangents[i]);
        carried = length_sq(carried) > 1e-6f ? normalize(carried) : path.ups[i - 1];
        path.ups[i] = carried;
    }
    Vec3 goal = e.u3 - tangents[n - 1] * dot(e.u3, tangents[n - 1]);
    goal = length_sq(goal) > 1e-6f ? normalize(goal) : path.ups[n - 1];
    const Vec3 last = path.ups[n - 1];
    const f32 twist = std::atan2(dot(cross(last, goal), tangents[n - 1]), dot(last, goal));
    for (u32 i = 0; i < n; i++) {
        const f32 s = static_cast<f32>(i) / static_cast<f32>(n - 1);
        const f32 eased = s * s * (3.0f - 2.0f * s) * kTwistEase + s * (1.0f - kTwistEase);
        path.ups[i] = normalize(rotate(quat_from_axis_angle(tangents[i], twist * eased), path.ups[i]));
    }
}

f32 angle_between(Vec3 a, Vec3 b);

f32 road_cost(const GravityPath& p, const Heightfield& hf, const Terrain* terrain, f32 half)
{
    if (!terrain) {
        return 0.0f;
    }
    f32 cost = 0.0f;
    for (u32 i = 0; i < p.count; i++) {
        const Vec3 at = p.points[i];
        if (at.y - hf.sample(at.x, at.z) > kRoadClearHeight) {
            continue;
        }
        const Vec3 along = i + 1 < p.count ? p.points[i + 1] - at : at - p.points[i - 1];
        Vec3 side = cross(along, p.ups[i]);
        side = length_sq(side) > 1e-8f ? normalize(side) * (half + kRoadMargin) : Vec3{};
        for (f32 k : {-1.0f, 0.0f, 1.0f}) {
            const Vec3 q = at + side * k;
            if (terrain->road_amount(q.x, q.z) > kRoadThreshold) {
                cost += kRoadCost;
            }
        }
    }
    return cost;
}

f32 shape_cost(const GravityPath& p, const Heightfield& hf, bool ground_start)
{
    f32 tightest = 1e30f;
    f32 fastest_turn = 0.0f;
    f32 under = 0.0f;
    f32 total = 0.0f;
    for (u32 i = 0; i + 1 < p.count; i++) {
        const f32 seg = f_max(length(p.points[i + 1] - p.points[i]), 1e-3f);
        total += seg;
        fastest_turn = f_max(fastest_turn, angle_between(p.ups[i], p.ups[i + 1]) / seg);
        if (i > 0) {
            const f32 bend = angle_between(p.points[i] - p.points[i - 1], p.points[i + 1] - p.points[i]);
            if (bend > 1e-4f) {
                tightest = f_min(tightest, seg / bend);
            }
        }
        const f32 t = static_cast<f32>(i) / static_cast<f32>(p.count - 1);
        if (i >= 2 && i + 2 < p.count && !(ground_start && t < kGroundSettle)) {
            under = f_max(under, hf.sample(p.points[i].x, p.points[i].z) + kPathClearance - p.points[i].y);
        }
    }
    return under * kUnderCost + f_max(kMinTurnRadius - tightest, 0.0f) * kBendCost
         + f_max(fastest_turn * kRadToDeg - kMaxTurnPerMetre, 0.0f) * kTwistCost + total * kLengthCost;
}

Anchor island_anchor(const BuiltIsland& island, Vec3 target, f32 turn)
{
    const IslandShape shape(island.params);
    const Vec3 local = rotate(conjugate(island.rot), target - island.pos);
    Vec3 dir{local.x, 0.0f, local.z};
    dir = length_sq(dir) > 1e-6f ? normalize(dir) : Vec3{1.0f, 0.0f, 0.0f};
    const f32 cs = std::cos(turn);
    const f32 sn = std::sin(turn);
    dir = Vec3{dir.x * cs - dir.z * sn, 0.0f, dir.x * sn + dir.z * cs};
    const f32 r = shape.rim_radius(std::atan2(dir.z, dir.x)) * kAnchorRim;
    const Vec3 at{dir.x * r, shape.top_height(dir.x * r, dir.z * r), dir.z * r};
    Anchor a;
    a.point = island.pos + rotate(island.rot, at);
    a.up = rotate(island.rot, Vec3{0.0f, 1.0f, 0.0f});
    a.out = rotate(island.rot, dir);
    return a;
}

void hash_bytes(u64& h, const void* data, size_t size)
{
    const u8* bytes = static_cast<const u8*>(data);
    for (size_t i = 0; i < size; i++) {
        h = (h ^ bytes[i]) * 1099511628211ull;
    }
}

f32 angle_between(Vec3 a, Vec3 b)
{
    return std::acos(f_clamp(dot(normalize(a), normalize(b)), -1.0f, 1.0f));
}

} // namespace

void IslandField::clear()
{
    islands_.clear();
    links_.clear();
    debris_.clear();
    rejected_.clear();
    haze_count_ = 0;
}

void IslandField::build_variants()
{
    if (variants_built_) {
        return;
    }
    for (u32 i = 0; i < kRockVariants; i++) {
        bake_rock(kVariantSeed + i * 977u, rocks_[i]);
        bake_rock_hull(kVariantSeed + i * 977u, rock_hulls_[i]);
        rock_reach_[i] = mesh_reach(rocks_[i]);
    }
    for (u32 i = 0; i < kTurfVariants; i++) {
        const IslandShape shape(turf_params(i));
        bake_island_mesh(shape, kTurfCell, turfs_[i]);
        bake_island_collision(shape, kHullCell, turf_hulls_[i]);
        turf_reach_[i] = mesh_reach(turfs_[i]);
    }
    variants_built_ = true;
}

void IslandField::build(const World& world, const Heightfield& hf, const Terrain* terrain)
{
    terrain_ = terrain;
    const auto start = std::chrono::steady_clock::now();
    clear();
    build_variants();

    const Pool<Entity>& pool = world.entities();
    std::vector<IslandShape> shapes;
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (!e || e->kind != EntityKind::Island || e->mesh_name.empty()) {
            continue;
        }
        BuiltIsland island;
        island.name.assign(e->mesh_name.view());
        island.pos = e->pos;
        island.rot = normalize(e->rot);
        island.params = params_for(*e);
        island.crater = ((e->aux_kind >> 8) & 1u) != 0;
        const IslandShape shape(island.params);
        bool duplicate = false;
        for (const BuiltIsland& other : islands_) {
            duplicate = duplicate || other.name == island.name;
        }
        if (duplicate) {
            reject(island.name.view(), "duplicate name");
            continue;
        }
        const i32 hit = island_clear(shape, island.pos, island.rot, island.params, shapes);
        if (hit >= 0) {
            FixedString<64> why;
            why.format("overlaps %s", islands_[static_cast<u32>(hit)].name.c_str());
            reject(island.name.view(), why.view());
            continue;
        }
        shapes.push_back(shape);
        bake_island_mesh(shape, island.params.radius > 12.0f ? kRenderCellLarge : kRenderCell, island.mesh);
        bake_island_collision(shape, kCollisionCell, island.collision);
        bake_island_grass(shape, kGrassRes, kGrassExtent, island.grass);
        islands_.push_back(std::move(island));
    }

    const auto find = [this](std::string_view name) -> i32 {
        for (size_t i = 0; i < islands_.size(); i++) {
            if (islands_[i].name == name) {
                return static_cast<i32>(i);
            }
        }
        return -2;
    };
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (!e || e->kind != EntityKind::IslandLink) {
            continue;
        }
        const std::string_view spec = e->mesh_name.view();
        const size_t split = spec.find('>');
        if (split == std::string_view::npos) {
            continue;
        }
        const i32 from = find(spec.substr(0, split));
        const std::string_view to_name = spec.substr(split + 1);
        const i32 to = to_name == "ground" ? kLinkGround : find(to_name);
        if (from < 0 || to == -2 || to == from) {
            reject(spec, "refers to a missing or rejected island");
            continue;
        }
        bool doubled = false;
        for (const BuiltLink& other : links_) {
            const bool same = static_cast<i32>(other.from) == from && other.to == to;
            const bool swapped = other.to == from && static_cast<i32>(other.from) == to;
            doubled = doubled || (to != kLinkGround && (same || swapped));
        }
        if (doubled) {
            reject(spec, "doubles an existing link");
            continue;
        }
        BuiltLink link;
        link.from = static_cast<u32>(from);
        link.to = to;
        link.width = e->aux_value > 1.0f ? e->aux_value : kLinkDefaultWidth;
        build_link(link, hf, e->pos, (e->aux_kind & 1u) != 0, shapes);
        FixedString<64> why;
        if (!link_clear(link, shapes, why)) {
            reject(spec, why.view());
            continue;
        }
        links_.push_back(std::move(link));
    }

    for (u32 i = 0; i < islands_.size(); i++) {
        scatter_island_debris(islands_[i], i);
    }
    for (u32 i = 0; i < links_.size(); i++) {
        scatter_link_debris(links_[i], i);
    }
    settle_debris(hf);
    terrain_ = nullptr;
    build_haze();
    generation_++;
    build_ms_ = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
    log_info("islands: %zu islands, %zu links, %zu debris (%u solid) in %.0f ms", islands_.size(),
             links_.size(), debris_.size(), solid_debris(), build_ms_);
}

void IslandField::build_link(BuiltLink& link, const Heightfield& hf, Vec3 ground_point, bool has_ground_point,
                             const std::vector<IslandShape>& shapes)
{
    const BuiltIsland& a = islands_[link.from];
    const bool ground_start = link.to == kLinkGround;
    std::vector<Vec3> grounds;
    if (ground_start) {
        if (has_ground_point) {
            grounds.push_back(ground_point);
        } else {
            const Vec3 up = rotate(a.rot, Vec3{0.0f, 1.0f, 0.0f});
            Vec3 h{up.x, 0.0f, up.z};
            if (length(h) < 0.2f) {
                const Vec3 side = rotate(a.rot, Vec3{1.0f, 0.0f, 0.0f});
                h = Vec3{side.x, 0.0f, side.z};
            }
            h = length_sq(h) > 1e-6f ? normalize(h) : Vec3{1.0f, 0.0f, 0.0f};
            const f32 lift = f_max(a.pos.y - hf.sample(a.pos.x, a.pos.z), 0.0f);
            const f32 reach = a.params.radius + 14.0f + lift * 0.9f;
            for (f32 turn : kGroundTurns) {
                const f32 cs = std::cos(turn * kDegToRad);
                const f32 sn = std::sin(turn * kDegToRad);
                grounds.push_back(a.pos + Vec3{h.x * cs - h.z * sn, 0.0f, h.x * sn + h.z * cs} * reach);
            }
        }
        for (Vec3& g : grounds) {
            g.y = hf.sample(g.x, g.z);
        }
    } else {
        grounds.push_back(Vec3{});
    }
    const auto make_ends = [&](Vec3 g, f32 turn_a, f32 turn_b) {
        PathEnds e;
        if (ground_start) {
            const Anchor end = island_anchor(a, g, turn_a);
            e.p0 = g + Vec3{0.0f, 0.05f, 0.0f};
            Vec3 flat{end.point.x - g.x, 0.0f, end.point.z - g.z};
            e.t0 = length_sq(flat) > 1e-6f ? normalize(flat) : Vec3{1.0f, 0.0f, 0.0f};
            e.u0 = Vec3{0.0f, 1.0f, 0.0f};
            e.p3 = end.point;
            e.t3 = end.out * -1.0f;
            e.u3 = end.up;
        } else {
            const BuiltIsland& b = islands_[static_cast<u32>(link.to)];
            const Anchor start = island_anchor(a, b.pos, turn_a);
            const Anchor end = island_anchor(b, a.pos, turn_b);
            e.p0 = start.point;
            e.t0 = start.out;
            e.u0 = start.up;
            e.p3 = end.point;
            e.t3 = end.out * -1.0f;
            e.u3 = end.up;
        }
        return e;
    };

    GravityPath& path = link.path;
    path.count = kGravityPathSamples;
    path.half_width = link.width * 0.5f + kPathMargin;
    path.height = kPathHeight;
    path.below = kPathBelow;
    path.falloff = kPathFalloff;

    constexpr u32 turn_count = static_cast<u32>(sizeof(kAnchorTurns) / sizeof(kAnchorTurns[0]));
    const u32 end_count = ground_start ? 1u : turn_count;
    const u32 pairs = static_cast<u32>(grounds.size()) * turn_count * end_count;
    std::vector<f32> pair_cost(pairs, 1e30f);
    std::vector<GravityPath> pair_path(pairs, path);
    parallel_for(pairs, [&](u32 begin, u32 end) {
        BuiltLink trial;
        trial.from = link.from;
        trial.to = link.to;
        trial.width = link.width;
        trial.path = path;
        for (u32 pair = begin; pair < end; pair++) {
            const f32 turn_a = kAnchorTurns[pair % turn_count];
            const f32 turn_b = kAnchorTurns[(pair / turn_count) % end_count];
            const Vec3 g = grounds[pair / (turn_count * end_count)];
            const f32 ground_cost = ground_start && !has_ground_point
                                  ? static_cast<f32>(pair / (turn_count * end_count)) * kGroundTurnCost : 0.0f;
            f32& best = pair_cost[pair];
            GravityPath& chosen = pair_path[pair];
            const PathEnds ends = make_ends(g, turn_a * kDegToRad, turn_b * kDegToRad);
            const f32 chord = length(ends.p3 - ends.p0);
            const f32 turn = angle_between(ends.u0, ends.u3) + 0.5f * angle_between(ends.t0, ends.t3);
            const f32 needed = f_max(chord, turn * kMinTurnRadius);
            const f32 anchor_cost = (f_abs(turn_a) + f_abs(turn_b)) * kAnchorTurnCost;
            for (f32 handle : kHandleScales) {
                for (f32 lift : kLiftScales) {
                    for (u32 shape = 0; shape < kWindShapes; shape++) {
                        for (f32 amp : kWindScales) {
                            if ((amp == 0.0f) != (shape == 0)) {
                                continue;
                            }
                            for (f32 sign : {1.0f, -1.0f}) {
                                if (amp == 0.0f && sign < 0.0f) {
                                    continue;
                                }
                                WindParams wind;
                                wind.handle = f_max(chord * 0.3f, needed * handle);
                                wind.amp = amp * chord * sign;
                                wind.lift = lift * chord;
                                wind.shape = shape;
                                sample_wind(ends, wind, trial.path);
                                f32 cost = anchor_cost + ground_cost + shape_cost(trial.path, hf, ground_start);
                                if (cost >= best) {
                                    continue;
                                }
                                cost += road_cost(trial.path, hf, terrain_, link.width * 0.5f);
                                if (cost >= best) {
                                    continue;
                                }
                                FixedString<64> why;
                                if (!link_clear(trial, shapes, why)) {
                                    cost += kBlockedCost;
                                }
                                if (cost < best) {
                                    best = cost;
                                    chosen = trial.path;
                                }
                            }
                        }
                    }
                }
            }
        }
    });
    u32 winner = 0;
    for (u32 pair = 1; pair < pairs; pair++) {
        if (pair_cost[pair] < pair_cost[winner]) {
            winner = pair;
        }
    }
    path = pair_path[winner];

    link.collision.clear();
    const f32 half = link.width * 0.5f;
    for (u32 i = 0; i + 1 < path.count; i++) {
        Vec3 corners[2][4];
        for (u32 k = 0; k < 2; k++) {
            const u32 j = i + k;
            const Vec3 tangent = j + 1 < path.count ? path.points[j + 1] - path.points[j]
                                                    : path.points[j] - path.points[j - 1];
            const Vec3 up = path.ups[j];
            const Vec3 side = normalize(cross(normalize(tangent), up));
            const Vec3 c = path.points[j];
            corners[k][0] = c - side * half;
            corners[k][1] = c + side * half;
            corners[k][2] = corners[k][1] - up * kRibbonThickness;
            corners[k][3] = corners[k][0] - up * kRibbonThickness;
        }
        const auto quad = [&link](Vec3 a0, Vec3 a1, Vec3 b1, Vec3 b0) {
            link.collision.push_back(a0);
            link.collision.push_back(a1);
            link.collision.push_back(b1);
            link.collision.push_back(a0);
            link.collision.push_back(b1);
            link.collision.push_back(b0);
        };
        quad(corners[0][0], corners[0][1], corners[1][1], corners[1][0]);
        quad(corners[0][1], corners[0][2], corners[1][2], corners[1][1]);
        quad(corners[0][2], corners[0][3], corners[1][3], corners[1][2]);
        quad(corners[0][3], corners[0][0], corners[1][0], corners[1][3]);
    }
}

void IslandField::scatter_island_debris(const BuiltIsland& island, u32 index)
{
    const IslandShape shape(island.params);
    Rng rng(seed_from_name(island.name.view(), 0x5eedu + index));
    const f32 radius = island.params.radius;
    const u32 count = 30 + static_cast<u32>(radius * 5.0f);
    const Vec3 up = rotate(island.rot, Vec3{0.0f, 1.0f, 0.0f});
    for (u32 k = 0; k < count; k++) {
        DebrisInstance d;
        const bool under = rng.next_f32() < 0.86f;
        const f32 size = under ? 0.25f + std::pow(rng.next_f32(), 2.4f) * 2.6f
                               : 0.15f + std::pow(rng.next_f32(), 3.0f) * 0.9f;
        const f32 phi = rng.range(0.0f, kTau);
        Vec3 local;
        if (under) {
            const f32 rho = std::sqrt(rng.next_f32()) * 1.05f;
            const f32 r = rho * shape.rim_radius(phi);
            const f32 floor = shape.bottom_height(f_min(rho, 1.0f));
            local = Vec3{std::cos(phi) * r, floor - size - rng.range(0.6f, 9.0f) * (0.6f + (1.0f - f_min(rho, 1.0f))), std::sin(phi) * r};
        } else {
            const f32 rho = rng.range(1.08f, 1.5f);
            const f32 r = rho * shape.rim_radius(phi);
            local = Vec3{std::cos(phi) * r, rng.range(-4.0f, 0.6f), std::sin(phi) * r};
        }
        d.kind = rng.next_f32() < 0.12f ? DebrisKind::Turf : DebrisKind::Rock;
        d.variant = static_cast<u16>(rng.range_u32(0, (d.kind == DebrisKind::Turf ? kTurfVariants : kRockVariants) - 1));
        d.pos = island.pos + rotate(island.rot, local);
        d.rot = normalize(Quat{rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f)});
        d.scale = d.kind == DebrisKind::Turf ? size * 0.7f : size;
        d.bob_dir = up;
        d.bob_amp = rng.range(0.08f, 0.45f) / (1.0f + size * 0.3f);
        d.bob_rate = kTau / rng.range(9.0f, 26.0f);
        d.spin_axis = rng_unit_vector(rng);
        d.spin_rate = rng.range(1.5f, 7.0f) * kDegToRad / (1.0f + size * 0.5f);
        d.phase = rng.range(0.0f, kTau);
        debris_.push_back(d);
    }
}

void IslandField::scatter_link_debris(const BuiltLink& link, u32 index)
{
    const GravityPath& path = link.path;
    Rng rng(0xD3B21u + index * 7919u + link.from * 31u);
    f32 lengths[kGravityPathSamples];
    lengths[0] = 0.0f;
    for (u32 i = 1; i < path.count; i++) {
        lengths[i] = lengths[i - 1] + length(path.points[i] - path.points[i - 1]);
    }
    const f32 total = lengths[path.count - 1];
    const auto frame_at = [&](f32 s, Vec3& p, Vec3& up, Vec3& tangent) {
        u32 i = 0;
        while (i + 2 < path.count && lengths[i + 1] < s) {
            i++;
        }
        const f32 seg = f_max(lengths[i + 1] - lengths[i], 1e-4f);
        const f32 t = f_clamp01((s - lengths[i]) / seg);
        p = lerp(path.points[i], path.points[i + 1], t);
        up = normalize(lerp(path.ups[i], path.ups[i + 1], t));
        tangent = normalize(path.points[i + 1] - path.points[i]);
    };
    const f32 half = link.width * 0.5f;

    for (f32 s = 1.0f; s < total - 0.5f; s += kChunkSpacing * rng.range(0.8f, 1.2f)) {
        Vec3 p;
        Vec3 up;
        Vec3 tangent;
        frame_at(s, p, up, tangent);
        const Vec3 side = normalize(cross(tangent, up));
        for (f32 off = -half + 0.9f; off <= half - 0.9f; off += 2.1f) {
            if (rng.next_f32() > kChunkKeep) {
                continue;
            }
            DebrisInstance d;
            d.kind = DebrisKind::Turf;
            d.variant = static_cast<u16>(rng.range_u32(0, kTurfVariants - 1));
            const f32 jitter = rng.range(-0.4f, 0.4f);
            d.pos = p + side * (off + jitter) + up * -0.08f;
            d.rot = normalize(quat_from_axis_angle(up, rng.range(0.0f, kTau)) * frame_quat(tangent, up));
            d.scale = rng.range(0.85f, 1.5f);
            d.bob_dir = up;
            d.bob_amp = 0.015f;
            d.bob_rate = kTau / rng.range(10.0f, 20.0f);
            d.phase = rng.range(0.0f, kTau);
            d.walkway = true;
            debris_.push_back(d);
        }
    }

    const u32 under = static_cast<u32>(total * 1.1f);
    for (u32 k = 0; k < under; k++) {
        Vec3 p;
        Vec3 up;
        Vec3 tangent;
        frame_at(rng.range(0.0f, total), p, up, tangent);
        const Vec3 side = normalize(cross(tangent, up));
        DebrisInstance d;
        const bool beside = rng.next_f32() < 0.35f;
        const f32 size = beside ? rng.range(0.2f, 0.8f) : 0.2f + std::pow(rng.next_f32(), 2.0f) * 1.2f;
        d.kind = DebrisKind::Rock;
        d.variant = static_cast<u16>(rng.range_u32(0, kRockVariants - 1));
        if (beside) {
            const f32 sign = rng.next_f32() < 0.5f ? -1.0f : 1.0f;
            d.pos = p + side * (sign * (half + rng.range(0.5f, 4.0f))) + up * rng.range(-1.0f, 2.0f);
        } else {
            d.pos = p + up * -rng.range(1.2f + size, 8.0f) + side * rng.range(-link.width * 0.9f, link.width * 0.9f);
        }
        d.rot = normalize(Quat{rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f)});
        d.scale = size;
        d.bob_dir = up;
        d.bob_amp = rng.range(0.08f, 0.4f);
        d.bob_rate = kTau / rng.range(8.0f, 22.0f);
        d.spin_axis = rng_unit_vector(rng);
        d.spin_rate = rng.range(2.0f, 8.0f) * kDegToRad;
        d.phase = rng.range(0.0f, kTau);
        debris_.push_back(d);
    }
}

void IslandField::reject(std::string_view what, std::string_view why)
{
    log_warn("islands: rejected %.*s, %.*s", static_cast<int>(what.size()), what.data(),
             static_cast<int>(why.size()), why.data());
    FixedString<64> entry;
    entry.assign(what);
    rejected_.push_back(entry);
}

i32 IslandField::island_clear(const IslandShape& shape, Vec3 pos, Quat rot, const IslandParams& params,
                               const std::vector<IslandShape>& shapes) const
{
    const Vec3 centre = island_bound_centre(pos, rot, params);
    const f32 bound = island_bound_radius(params);
    const f32 reach = params.radius * kIslandBoundGrow;
    for (size_t k = 0; k < islands_.size(); k++) {
        const BuiltIsland& other = islands_[k];
        const Vec3 other_centre = island_bound_centre(other.pos, other.rot, other.params);
        if (length(centre - other_centre) > bound + island_bound_radius(other.params) + kIslandGap) {
            continue;
        }
        const f32 margin = kIslandGap + shape.detail_amplitude() + shapes[k].detail_amplitude();
        const Quat to_other = conjugate(other.rot);
        for (f32 y = -params.depth - 2.0f; y <= 2.0f; y += kOverlapStep) {
            for (f32 x = -reach; x <= reach; x += kOverlapStep) {
                for (f32 z = -reach; z <= reach; z += kOverlapStep) {
                    const Vec3 local{x, y, z};
                    if (shape.base(local) > 0.0f) {
                        continue;
                    }
                    const Vec3 world_point = pos + rotate(rot, local);
                    if (shapes[k].base(rotate(to_other, world_point - other.pos)) < margin) {
                        return static_cast<i32>(k);
                    }
                }
            }
        }
    }
    return -1;
}

FixedString<64> IslandField::link_name(const BuiltLink& link) const
{
    FixedString<64> name;
    name.format("%s>%s", islands_[link.from].name.c_str(),
                link.to == kLinkGround ? "ground" : islands_[static_cast<u32>(link.to)].name.c_str());
    return name;
}

bool IslandField::link_clear(const BuiltLink& link, const std::vector<IslandShape>& shapes,
                             FixedString<64>& why) const
{
    const GravityPath& path = link.path;
    const f32 half = link.width * 0.5f;
    for (size_t k = 0; k < islands_.size(); k++) {
        if (k == link.from || static_cast<i32>(k) == link.to) {
            continue;
        }
        const BuiltIsland& island = islands_[k];
        const Vec3 centre = island_bound_centre(island.pos, island.rot, island.params);
        const f32 bound = island_bound_radius(island.params) + half + kLinkGap;
        const Quat inv = conjugate(island.rot);
        for (u32 i = 0; i < path.count; i++) {
            if (length(path.points[i] - centre) > bound) {
                continue;
            }
            const f32 d = shapes[k].base(rotate(inv, path.points[i] - island.pos)) - shapes[k].detail_amplitude();
            if (d < half + kLinkGap) {
                why.format("cuts through %s", island.name.c_str());
                return false;
            }
        }
    }
    for (const BuiltLink& other : links_) {
        const f32 limit = (link.width + other.width) * 0.5f + kLinkGap;
        i32 shared[2] = {-1, -1};
        u32 shared_count = 0;
        const i32 ends_a[2] = {static_cast<i32>(link.from), link.to};
        const i32 ends_b[2] = {static_cast<i32>(other.from), other.to};
        for (i32 a : ends_a) {
            for (i32 b : ends_b) {
                if (a >= 0 && a == b && shared_count < 2) {
                    shared[shared_count++] = a;
                }
            }
        }
        const auto near_shared = [&](Vec3 p) {
            for (u32 s = 0; s < shared_count; s++) {
                const BuiltIsland& island = islands_[static_cast<u32>(shared[s])];
                if (length(p - island.pos) < island.params.radius + kSharedEndSkip) {
                    return true;
                }
            }
            return false;
        };
        for (u32 i = 0; i < path.count; i++) {
            const Vec3 p = path.points[i];
            if (near_shared(p)) {
                continue;
            }
            for (u32 j = 0; j + 1 < other.path.count; j++) {
                if (near_shared(other.path.points[j]) && near_shared(other.path.points[j + 1])) {
                    continue;
                }
                if (point_segment_distance(p, other.path.points[j], other.path.points[j + 1]) < limit) {
                    why.format("too close to %s", link_name(other).c_str());
                    return false;
                }
            }
        }
    }
    return true;
}

void IslandField::settle_debris(const Heightfield& hf)
{
    size_t kept = 0;
    for (size_t i = 0; i < debris_.size(); i++) {
        DebrisInstance d = debris_[i];
        const f32 reach = d.scale * (d.kind == DebrisKind::Rock ? rock_reach_[d.variant] : turf_reach_[d.variant]);
        const f32 ground = hf.sample(d.pos.x, d.pos.z);
        if (d.pos.y + reach < ground) {
            continue;
        }
        if (terrain_ && !d.walkway && d.pos.y - reach < ground + kRoadClearHeight
            && terrain_->road_amount(d.pos.x, d.pos.z) > kRoadThreshold) {
            continue;
        }
        if (!d.walkway && d.pos.y - reach - d.bob_amp < ground + kDebrisSolidBand) {
            d.solid = true;
            d.bob_amp = 0.0f;
            d.spin_rate = 0.0f;
        }
        debris_[kept++] = d;
    }
    debris_.resize(kept);
}

u32 IslandField::solid_debris() const
{
    u32 count = 0;
    for (const DebrisInstance& d : debris_) {
        count += d.solid ? 1u : 0u;
    }
    return count;
}

void IslandField::build_haze()
{
    const u32 n = static_cast<u32>(islands_.size());
    std::vector<u32> parent(n);
    for (u32 i = 0; i < n; i++) {
        parent[i] = i;
    }
    const auto root = [&parent](u32 i) {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    };
    for (const BuiltLink& link : links_) {
        if (link.to >= 0) {
            parent[root(link.from)] = root(static_cast<u32>(link.to));
        }
    }
    haze_count_ = 0;
    for (u32 i = 0; i < n && haze_count_ < kMaxHaze; i++) {
        if (root(i) != i) {
            continue;
        }
        Vec3 centre{};
        u32 members = 0;
        for (u32 j = 0; j < n; j++) {
            if (root(j) == i) {
                centre += islands_[j].pos;
                members++;
            }
        }
        centre = centre * (1.0f / static_cast<f32>(members));
        f32 radius = 0.0f;
        for (u32 j = 0; j < n; j++) {
            if (root(j) == i) {
                radius = f_max(radius, length(islands_[j].pos - centre) + islands_[j].params.radius * 1.4f);
            }
        }
        haze_[haze_count_++] = HazeSphere{centre, radius + kHazeMargin};
    }
}

void IslandField::add_collision(PhysWorld& phys) const
{
    for (const BuiltIsland& island : islands_) {
        const Mat3 r = quat_to_mat3(island.rot);
        for (size_t i = 0; i + 2 < island.collision.size(); i += 3) {
            phys.add_static_tri(island.pos + r * island.collision[i], island.pos + r * island.collision[i + 1],
                                island.pos + r * island.collision[i + 2]);
        }
    }
    for (const BuiltLink& link : links_) {
        for (size_t i = 0; i + 2 < link.collision.size(); i += 3) {
            phys.add_static_tri(link.collision[i], link.collision[i + 1], link.collision[i + 2]);
        }
    }
    for (const DebrisInstance& d : debris_) {
        if (!d.solid) {
            continue;
        }
        const Mat4 m = debris_transform(d, 0.0f);
        const std::vector<Vec3>& hull = d.kind == DebrisKind::Rock ? rock_hulls_[d.variant] : turf_hulls_[d.variant];
        for (size_t i = 0; i + 2 < hull.size(); i += 3) {
            phys.add_static_tri(transform_point(m, hull[i]), transform_point(m, hull[i + 1]),
                                transform_point(m, hull[i + 2]));
        }
    }
}

void IslandField::fill_gravity(GravityField& field) const
{
    for (const BuiltIsland& island : islands_) {
        GravityVolume v;
        v.rot = island.rot;
        v.pos = island.pos + rotate(island.rot, Vec3{0.0f, 1.0f, 0.0f});
        v.half = Vec3{island.params.radius * 1.5f, kIslandGravityHeight, island.params.radius * 1.5f};
        v.falloff = kIslandGravityFalloff;
        field.add(v);
    }
    for (const BuiltLink& link : links_) {
        field.add_path(link.path);
    }
}

u32 IslandField::collect_craters(const World& world, CraterStamp* out, u32 max)
{
    u32 count = 0;
    const Pool<Entity>& pool = world.entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (!e || e->kind != EntityKind::Island || ((e->aux_kind >> 8) & 1u) == 0 || count >= max) {
            continue;
        }
        const f32 radius = f_clamp(e->half.x, 3.0f, kIslandMaxRadius);
        out[count++] = CraterStamp{e->pos.x, e->pos.z, radius * 0.95f, f_clamp(radius * 0.25f, 1.5f, 4.0f)};
    }
    return count;
}

u64 IslandField::signature(const World& world)
{
    u64 h = 1469598103934665603ull;
    const Pool<Entity>& pool = world.entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (!e || (e->kind != EntityKind::Island && e->kind != EntityKind::IslandLink && e->kind != EntityKind::Prop
                   && e->kind != EntityKind::Bench)) {
            continue;
        }
        hash_bytes(h, &e->kind, sizeof(e->kind));
        hash_bytes(h, &e->pos, sizeof(e->pos));
        hash_bytes(h, &e->rot, sizeof(e->rot));
        hash_bytes(h, &e->scale, sizeof(e->scale));
        hash_bytes(h, &e->half, sizeof(e->half));
        hash_bytes(h, &e->aux_kind, sizeof(e->aux_kind));
        hash_bytes(h, &e->aux_value, sizeof(e->aux_value));
        hash_bytes(h, &e->aux_data, sizeof(e->aux_data));
        hash_bytes(h, e->mesh_name.c_str(), e->mesh_name.size());
    }
    return h;
}

Mat4 IslandField::debris_transform(const DebrisInstance& d, f32 time)
{
    const Vec3 pos = d.pos + d.bob_dir * (d.bob_amp * std::sin(d.bob_rate * time + d.phase));
    const Quat rot = d.spin_rate > 0.0f ? quat_from_axis_angle(d.spin_axis, d.spin_rate * time + d.phase) * d.rot : d.rot;
    return mat4_trs(pos, rot, Vec3{d.scale, d.scale, d.scale});
}

u64 IslandField::checksum() const
{
    u64 h = 1469598103934665603ull;
    for (const BuiltIsland& island : islands_) {
        hash_bytes(h, island.mesh.vertices.data(), island.mesh.vertices.size() * sizeof(AmshVertex));
        hash_bytes(h, island.mesh.indices.data(), island.mesh.indices.size() * sizeof(u32));
        hash_bytes(h, island.collision.data(), island.collision.size() * sizeof(Vec3));
    }
    for (const BuiltLink& link : links_) {
        hash_bytes(h, link.path.points, sizeof(Vec3) * link.path.count);
        hash_bytes(h, link.path.ups, sizeof(Vec3) * link.path.count);
    }
    for (const DebrisInstance& d : debris_) {
        hash_bytes(h, &d.pos, sizeof(d.pos));
        hash_bytes(h, &d.scale, sizeof(d.scale));
        hash_bytes(h, &d.solid, sizeof(d.solid));
    }
    return h;
}

void islands_rebuild(IslandField& field, World& world, Terrain& terrain, PhysWorld& phys,
                     Arena& statics_arena, Arena& scratch)
{
    CraterStamp craters[IslandField::kMaxCraters];
    const u32 crater_count = IslandField::collect_craters(world, craters, IslandField::kMaxCraters);
    terrain.stamp_craters(craters, crater_count);
    field.build(world, terrain.heightfield(), &terrain);
    phys.statics_reserve(statics_arena, kZoneMaxStaticTris);
    zone_add_entity_collision(world, phys, scratch);
    field.add_collision(phys);
    phys.statics_build(statics_arena);
}

} // namespace anom
