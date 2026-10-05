#include "test.h"

#include "core/arena.h"
#include "physics/heightfield.h"
#include "world/entity.h"
#include "world/islands/island_field.h"
#include "world/islands/island_shape.h"
#include "world/islands/surface_nets.h"

#include <chrono>
#include <cstdio>
#include <map>
#include <utility>

using namespace anom;

namespace {

u64 mesh_checksum(const BakedMesh& mesh)
{
    u64 h = 1469598103934665603ull;
    const auto mix = [&h](const void* data, size_t size) {
        const u8* bytes = static_cast<const u8*>(data);
        for (size_t i = 0; i < size; i++) {
            h = (h ^ bytes[i]) * 1099511628211ull;
        }
    };
    mix(mesh.vertices.data(), mesh.vertices.size() * sizeof(AmshVertex));
    mix(mesh.indices.data(), mesh.indices.size() * sizeof(u32));
    return h;
}

struct GraphBench {
    Arena arena{megabytes(64)};
    World world;
    Heightfield hf;
    IslandField field;

    GraphBench()
    {
        world.init(arena);
        hf.init_procedural(arena, 256, 1.0f, 3u, 0.0f);
    }

    void island(std::string_view name, Vec3 pos, f32 radius)
    {
        const EntityHandle h = world.spawn(EntityKind::Island, pos, quat_identity(), 1.0f, name, 0);
        Entity* e = world.entity(h);
        e->half = Vec3{radius, 0.0f, 0.0f};
        e->aux_value = 6.0f;
        e->aux_data = 4u;
    }

    void link(std::string_view spec, f32 width)
    {
        const EntityHandle h = world.spawn(EntityKind::IslandLink, Vec3{}, quat_identity(), 1.0f, spec, 0);
        world.entity(h)->aux_value = width;
    }

    bool rejected(std::string_view name) const
    {
        for (const FixedString<64>& r : field.rejected()) {
            if (r == name) {
                return true;
            }
        }
        return false;
    }
};

} // namespace

TEST(islands, a_baked_island_is_watertight_and_finite)
{
    IslandParams params;
    params.seed = 7;
    const IslandShape shape(params);
    BakedMesh mesh;
    const auto start = std::chrono::steady_clock::now();
    bake_island_mesh(shape, 0.4f, mesh);
    const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("ISL verts=%zu tris=%zu ms=%.1f\n", mesh.vertices.size(), mesh.indices.size() / 3, ms);
    CHECK(mesh.indices.size() > 3000);

    std::map<std::pair<u32, u32>, i32> edges;
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        for (u32 k = 0; k < 3; k++) {
            const u32 a = mesh.indices[i + k];
            const u32 b = mesh.indices[i + (k + 1) % 3];
            edges[{a, b}] += 1;
        }
    }
    u32 open = 0;
    for (const auto& [edge, count] : edges) {
        if (edges.find({edge.second, edge.first}) == edges.end()) {
            open++;
        }
    }
    CHECK(open == 0);
    for (const AmshVertex& v : mesh.vertices) {
        CHECK(v.pos[0] == v.pos[0] && v.pos[1] == v.pos[1] && v.pos[2] == v.pos[2]);
        CHECK(v.uv[0] >= 0.0f && v.uv[0] <= 1.0f);
    }
}

TEST(islands, the_same_seed_bakes_the_same_island_and_another_does_not)
{
    IslandParams a;
    a.seed = 11;
    IslandParams b = a;
    b.seed = 12;
    BakedMesh m1;
    BakedMesh m2;
    BakedMesh m3;
    bake_island_mesh(IslandShape(a), 0.6f, m1);
    bake_island_mesh(IslandShape(a), 0.6f, m2);
    bake_island_mesh(IslandShape(b), 0.6f, m3);
    CHECK(mesh_checksum(m1) == mesh_checksum(m2));
    CHECK(mesh_checksum(m1) != mesh_checksum(m3));
}

TEST(island_graph, an_island_overlapping_an_earlier_one_is_rejected)
{
    GraphBench bench;
    bench.island("first", Vec3{100.0f, 30.0f, 100.0f}, 10.0f);
    bench.island("crowding", Vec3{112.0f, 31.0f, 100.0f}, 9.0f);
    bench.island("distant", Vec3{160.0f, 30.0f, 100.0f}, 9.0f);
    bench.field.build(bench.world, bench.hf);
    CHECK(bench.field.islands().size() == 2);
    CHECK(bench.rejected("crowding"));
    CHECK(!bench.rejected("distant"));
}

TEST(island_graph, a_link_winds_around_a_third_island_and_a_doubled_link_is_rejected)
{
    GraphBench bench;
    bench.island("west", Vec3{60.0f, 30.0f, 100.0f}, 9.0f);
    bench.island("east", Vec3{140.0f, 30.0f, 100.0f}, 9.0f);
    bench.island("middle", Vec3{100.0f, 31.0f, 100.0f}, 9.0f);
    bench.island("north", Vec3{100.0f, 30.0f, 160.0f}, 9.0f);
    bench.link("west>east", 7.0f);
    bench.link("west>north", 7.0f);
    bench.link("north>west", 7.0f);
    bench.field.build(bench.world, bench.hf);
    CHECK(!bench.rejected("west>east"));
    CHECK(!bench.rejected("west>north"));
    CHECK(bench.rejected("north>west"));
    CHECK(bench.field.links().size() == 2);
    for (const BuiltLink& l : bench.field.links()) {
        for (u32 i = 0; i < l.path.count; i++) {
            CHECK(length(l.path.points[i] - Vec3{100.0f, 31.0f, 100.0f}) > 9.0f + l.width * 0.5f);
        }
    }
}

TEST(island_graph, debris_near_the_ground_is_solid_and_still_and_high_debris_drifts)
{
    GraphBench bench;
    bench.island("low", Vec3{100.0f, 7.0f, 100.0f}, 10.0f);
    bench.island("high", Vec3{160.0f, 60.0f, 100.0f}, 10.0f);
    bench.field.build(bench.world, bench.hf);
    u32 solid = 0;
    u32 drifting = 0;
    for (const DebrisInstance& d : bench.field.debris()) {
        CHECK(d.pos.y > bench.hf.sample(d.pos.x, d.pos.z) - 3.0f * d.scale);
        if (d.solid) {
            solid++;
            CHECK(d.bob_amp == 0.0f && d.spin_rate == 0.0f);
            CHECK(d.pos.y - 3.0f * d.scale < bench.hf.sample(d.pos.x, d.pos.z) + IslandField::kDebrisSolidBand);
        } else if (d.pos.x > 130.0f) {
            drifting += d.bob_amp > 0.0f ? 1u : 0u;
        }
    }
    CHECK(solid > 5);
    CHECK(drifting > 20);
    CHECK(bench.field.solid_debris() == solid);
}
