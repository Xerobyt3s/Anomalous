#include "test.h"

#include "carsys/items.h"
#include "core/arena.h"
#include "physics/gravity_field.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "platform/filesystem.h"
#include "world/entity.h"
#include "world/terrain.h"
#include "world/zone.h"

#include <cstdio>
#include <cstdlib>

using namespace anom;

namespace {

FixedString<256> temp_zone(const char* name)
{
    char* root = nullptr;
    std::size_t root_len = 0;
    _dupenv_s(&root, &root_len, "TEMP");

    FixedString<256> dir;
    dir.format("%s/anomalous_%s", root ? root : ".", name);
    std::free(root);
    fs::make_dir(dir.view());
    return dir;
}

bool write_text(std::string_view path, std::string_view text)
{
    std::FILE* out = fs::open(path, "wb");
    if (!out) {
        return false;
    }
    std::fwrite(text.data(), 1, text.size(), out);
    std::fclose(out);
    return true;
}

FixedString<256> cfg_path(const FixedString<256>& dir)
{
    FixedString<256> path;
    path.format("%s/zone.cfg", dir.c_str());
    return path;
}

bool contains(std::string_view haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string_view::npos;
}

u32 count_of(std::string_view haystack, std::string_view needle)
{
    u32 n = 0;
    std::size_t at = haystack.find(needle);
    while (at != std::string_view::npos) {
        n++;
        at = haystack.find(needle, at + 1);
    }
    return n;
}

struct Bench {
    Arena arena{megabytes(32)};
    Heightfield hf;
    PhysWorld phys;
    World world;
    Terrain terrain;

    Bench()
    {
        terrain.heightfield().alloc(arena, 64, 4.0f);
        terrain.heightfield().recompute_extents();
        hf.alloc(arena, 64, 4.0f);
        hf.recompute_extents();
        phys.init(arena, &hf);
        world.init(arena);
    }

    std::string_view save_and_read(const FixedString<256>& dir)
    {
        if (!zone_save(dir.view(), arena, world, phys, terrain)) {
            return {};
        }
        const fs::FileData data = fs::read_entire_file(arena, cfg_path(dir).view());
        return data.valid() ? data.text() : std::string_view{};
    }
};

} // namespace

TEST(zone_save, writes_every_entity_kind)
{
    const FixedString<256> dir = temp_zone("save_kinds");
    CHECK(write_text(cfg_path(dir).view(), "[entities]\n"));

    Bench bench;
    bench.world.spawn(EntityKind::Tree, Vec3{10.0f, 0.0f, 20.0f},
                      quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, 0.5f), 1.25f, "tree_pine",
                      kEntityFlagCollides);
    bench.world.spawn(EntityKind::Building, Vec3{-4.0f, 0.0f, 8.0f}, quat_identity(), 1.0f,
                      "relay_tower", kEntityFlagCollides | kEntityFlagTower);

    const EntityHandle trigger = bench.world.spawn(EntityKind::Trigger, Vec3{1.0f, 2.0f, 3.0f},
                                                   quat_identity(), 1.0f, "",
                                                   kEntityFlagInteractable);
    Entity* t = bench.world.entity(trigger);
    CHECK(t != nullptr);
    t->mesh_name.assign("shed_door");
    t->half = Vec3{1.5f, 2.0f, 0.5f};
    t->aux_kind = 3;
    t->aux_value = 0.75f;

    const std::string_view text = bench.save_and_read(dir);
    CHECK(!text.empty());
    CHECK(contains(text, "spawn = tree tree_pine 10.000 20.000"));
    CHECK(contains(text, "tower = -4.000 8.000"));
    CHECK(contains(text, "trigger = shed_door 1.000 2.000 3.000 1.500 2.000 0.500 0.00 3 0.750"));
}

TEST(zone_save, a_pickup_round_trips_through_its_item_id)
{
    const FixedString<256> dir = temp_zone("save_pickup");
    CHECK(write_text(cfg_path(dir).view(), "[entities]\n"));

    Bench bench;
    const EntityHandle handle = bench.world.spawn(EntityKind::PartPickup,
                                                  Vec3{5.0f, 1.0f, 6.0f}, quat_identity(), 1.0f,
                                                  "battery", 0);
    Entity* e = bench.world.entity(handle);
    CHECK(e != nullptr);
    e->aux_kind = static_cast<u32>(ITEM_BATTERY);
    e->aux_value = 0.65f;
    e->aux_data = 2;

    const std::string_view text = bench.save_and_read(dir);
    CHECK(contains(text, "pickup = battery "));
    CHECK(contains(text, " 0.650 2\n"));
}

TEST(zone_save, keeps_comments_and_other_sections)
{
    const FixedString<256> dir = temp_zone("save_preserve");
    CHECK(write_text(cfg_path(dir).view(),
                     "# hand written\n"
                     "\n"
                     "[terrain]\n"
                     "heightmap = heightmap.png\n"
                     "cell_size = 2.0\n"
                     "\n"
                     "[entities]\n"
                     "spawn = tree stale_pine 1.000 2.000 0.00 1.000\n"
                     "\n"
                     "[spawn]\n"
                     "car_pos = 274.52 96.86\n"));

    Bench bench;
    bench.world.spawn(EntityKind::Tree, Vec3{7.0f, 0.0f, 9.0f}, quat_identity(), 1.0f,
                      "fresh_pine", kEntityFlagCollides);

    const std::string_view text = bench.save_and_read(dir);
    CHECK(contains(text, "# hand written"));
    CHECK(contains(text, "[terrain]"));
    CHECK(contains(text, "heightmap = heightmap.png"));
    CHECK(contains(text, "car_pos = 274.52 96.86"));
    CHECK(contains(text, "fresh_pine"));
    CHECK(!contains(text, "stale_pine"));
}

TEST(zone_save, entities_stay_inside_their_own_section)
{
    const FixedString<256> dir = temp_zone("save_ordering");
    CHECK(write_text(cfg_path(dir).view(),
                     "[entities]\n"
                     "[spawn]\n"
                     "car_pos = 1 2\n"));

    Bench bench;
    bench.world.spawn(EntityKind::Tree, Vec3{7.0f, 0.0f, 9.0f}, quat_identity(), 1.0f, "pine",
                      kEntityFlagCollides);

    const std::string_view text = bench.save_and_read(dir);
    const std::size_t entities_at = text.find("[entities]");
    const std::size_t pine_at = text.find("pine");
    const std::size_t spawn_at = text.find("[spawn]");
    CHECK(entities_at != std::string_view::npos);
    CHECK(pine_at != std::string_view::npos);
    CHECK(spawn_at != std::string_view::npos);
    CHECK(entities_at < pine_at);
    CHECK(pine_at < spawn_at);
}

TEST(zone_save, adds_the_entities_header_when_it_is_missing)
{
    const FixedString<256> dir = temp_zone("save_header");
    CHECK(write_text(cfg_path(dir).view(), "[terrain]\ncell_size = 2.0\n"));

    Bench bench;
    bench.world.spawn(EntityKind::Building, Vec3{3.0f, 0.0f, 4.0f}, quat_identity(), 1.0f,
                      "garage", kEntityFlagCollides);

    const std::string_view text = bench.save_and_read(dir);
    CHECK(contains(text, "[entities]"));
    CHECK(contains(text, "spawn = building garage"));
    CHECK(text.find("[entities]") > text.find("[terrain]"));
}

TEST(zone_save, repeated_saves_do_not_grow_the_file)
{
    const FixedString<256> dir = temp_zone("save_idempotent");
    CHECK(write_text(cfg_path(dir).view(), "# keep\n[entities]\n"));

    Bench bench;
    bench.world.spawn(EntityKind::Tree, Vec3{7.0f, 0.0f, 9.0f}, quat_identity(), 1.0f, "pine",
                      kEntityFlagCollides);
    bench.world.spawn(EntityKind::Building, Vec3{-4.0f, 0.0f, 8.0f}, quat_identity(), 1.0f,
                      "relay_tower", kEntityFlagCollides | kEntityFlagTower);

    const u64 first = bench.save_and_read(dir).size();
    const std::string_view second = bench.save_and_read(dir);

    CHECK(first == second.size());
    CHECK(count_of(second, "[entities]") == 1);
    CHECK(count_of(second, "tower = ") == 1);
    CHECK(count_of(second, "# keep") == 1);
}

TEST(zone_save, pitch_and_roll_survive_the_write)
{
    const FixedString<256> dir = temp_zone("save_euler");
    CHECK(write_text(cfg_path(dir).view(), "[entities]\n"));

    Bench bench;
    const f32 yaw = 40.0f * kDegToRad;
    const f32 pitch = -12.0f * kDegToRad;
    const f32 roll = 7.0f * kDegToRad;
    bench.world.spawn(EntityKind::StaticMesh, Vec3{2.0f, 0.0f, 3.0f},
                      quat_from_euler(yaw, pitch, roll), 1.0f, "crate", kEntityFlagCollides);

    const std::string_view text = bench.save_and_read(dir);
    CHECK(contains(text, "spawn = static crate 2.000 3.000 40.00 1.000"));
    CHECK(contains(text, "-12.00 7.00"));
}

TEST(zone_save, a_missing_directory_fails_without_writing)
{
    Bench bench;
    CHECK(!zone_save("assets/zones/definitely_missing", bench.arena, bench.world, bench.phys,
                     bench.terrain));
}

TEST(vmath, euler_round_trips_through_the_quaternion)
{
    const f32 angles[][3] = {
        {0.0f, 0.0f, 0.0f},   {35.0f, 0.0f, 0.0f},    {0.0f, 20.0f, 0.0f},
        {0.0f, 0.0f, -45.0f}, {120.0f, -30.0f, 15.0f}, {-160.0f, 12.0f, -80.0f},
    };
    for (const auto& a : angles) {
        const Quat q = quat_from_euler(a[0] * kDegToRad, a[1] * kDegToRad, a[2] * kDegToRad);
        f32 yaw = 0.0f;
        f32 pitch = 0.0f;
        f32 roll = 0.0f;
        quat_to_euler(q, yaw, pitch, roll);
        const Quat back = quat_from_euler(yaw, pitch, roll);
        const f32 alignment = f_abs(q.x * back.x + q.y * back.y + q.z * back.z + q.w * back.w);
        CHECK_NEAR(alignment, 1.0f, 1e-4);
    }
}

TEST(vmath, euler_yaw_matches_quat_yaw)
{
    const Quat q = quat_from_euler(0.9f, 0.0f, 0.0f);
    f32 yaw = 0.0f;
    f32 pitch = 0.0f;
    f32 roll = 0.0f;
    quat_to_euler(q, yaw, pitch, roll);
    CHECK_NEAR(yaw, quat_yaw(q), 1e-5);
    CHECK_NEAR(pitch, 0.0f, 1e-5);
    CHECK_NEAR(roll, 0.0f, 1e-5);
}

TEST(zone_save, gravity_volumes_round_trip_with_their_shape_mode_and_sector)
{
    const FixedString<256> dir = temp_zone("save_gravity");
    CHECK(write_text(cfg_path(dir).view(), "[entities]\n"));

    Bench bench;
    struct Spec {
        Vec3 pos;
        Quat rot;
        Vec3 half;
        f32 falloff;
        f32 strength;
        u32 shape;
        u32 mode;
        u32 sector;
    };
    const Spec specs[] = {
        {Vec3{4.0f, 6.0f, -3.0f}, quat_from_euler(0.4f, 0.0f, 90.0f * kDegToRad), Vec3{6.0f, 2.0f, 5.0f}, 2.0f, 9.81f, 0, 0, 90},
        {Vec3{-8.0f, 12.0f, 2.0f}, quat_from_euler(-1.1f, 0.0f, 22.0f * kDegToRad), Vec3{11.0f, 11.0f, 6.0f}, 1.0f, 12.0f, 0, 1, 42},
        {Vec3{20.0f, 30.0f, 10.0f}, quat_from_euler(0.2f, 0.3f, 2.9f), Vec3{7.0f, 0.0f, 0.0f}, 3.0f, 4.0f, 1, 2, 90},
    };
    for (const Spec& spec : specs) {
        const EntityHandle h = bench.world.spawn(EntityKind::Gravity, spec.pos, spec.rot, spec.falloff, "", 0);
        Entity* e = bench.world.entity(h);
        CHECK(e != nullptr);
        e->mesh_name.assign("vol");
        e->half = spec.half;
        e->aux_value = spec.strength;
        e->aux_kind = spec.shape | (spec.mode << 4);
        e->aux_data = spec.sector;
    }
    GravityField before;
    zone_gravity(bench.world, before);
    CHECK(!bench.save_and_read(dir).empty());

    World reloaded;
    reloaded.init(bench.arena);
    CHECK(zone_reload(dir.view(), bench.arena, bench.arena, reloaded, bench.phys, bench.terrain, nullptr));
    GravityField after;
    zone_gravity(reloaded, after);
    CHECK(after.count() == before.count());

    for (i32 x = -20; x <= 30; x += 3) {
        for (i32 y = 0; y <= 40; y += 4) {
            for (i32 z = -10; z <= 15; z += 5) {
                const Vec3 p{static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(z)};
                const GravitySample a = before.sample(p);
                const GravitySample b = after.sample(p);
                CHECK(length(a.gravity - b.gravity) < 0.05f);
                CHECK(f_abs(a.presence - b.presence) < 0.02f);
            }
        }
    }
}

TEST(zone_bounds, far_outside_the_map_or_not_a_number_is_out_of_bounds)
{
    Bench bench;
    const Heightfield& hf = bench.terrain.heightfield();
    CHECK(!zone_out_of_bounds(hf, Vec3{0.0f, 5.0f, 0.0f}));
    CHECK(!zone_out_of_bounds(hf, Vec3{150.0f, 300.0f, -150.0f}));
    CHECK(zone_out_of_bounds(hf, Vec3{900.0f, 0.0f, 0.0f}));
    CHECK(zone_out_of_bounds(hf, Vec3{0.0f, 900.0f, 0.0f}));
    CHECK(zone_out_of_bounds(hf, Vec3{0.0f, -900.0f, 0.0f}));
    const f32 nan = std::sqrt(-1.0f);
    CHECK(zone_out_of_bounds(hf, Vec3{nan, 0.0f, 0.0f}));
}

TEST(zone_save, islands_and_links_round_trip_with_their_graph)
{
    const FixedString<256> dir = temp_zone("save_islands");
    CHECK(write_text(cfg_path(dir).view(), "[entities]\n"));

    Bench bench;
    const Quat tilt = quat_from_euler(0.6f, 0.2f, 1.4f);
    const EntityHandle a = bench.world.spawn(EntityKind::Island, Vec3{10.0f, 14.5f, -6.0f}, tilt, 1.0f, "north", 0);
    Entity* ea = bench.world.entity(a);
    CHECK(ea != nullptr);
    ea->half = Vec3{11.5f, 0.0f, 0.0f};
    ea->aux_value = 8.25f;
    ea->aux_data = 77u;
    ea->aux_kind = 1u | (1u << 8);
    const EntityHandle b = bench.world.spawn(EntityKind::Island, Vec3{-20.0f, 30.0f, 4.0f}, quat_identity(), 1.0f, "south", 0);
    Entity* eb = bench.world.entity(b);
    CHECK(eb != nullptr);
    eb->half = Vec3{7.0f, 0.0f, 0.0f};
    eb->aux_value = 5.0f;
    eb->aux_data = 5u;
    const EntityHandle l0 = bench.world.spawn(EntityKind::IslandLink, Vec3{}, quat_identity(), 1.0f, "north>south", 0);
    bench.world.entity(l0)->aux_value = 6.5f;
    const EntityHandle l1 = bench.world.spawn(EntityKind::IslandLink, Vec3{3.0f, 0.0f, 40.0f}, quat_identity(), 1.0f, "north>ground", 0);
    bench.world.entity(l1)->aux_value = 9.0f;
    bench.world.entity(l1)->aux_kind = 1u;

    const std::string_view text = bench.save_and_read(dir);
    CHECK(text.find("island = north") != std::string_view::npos);
    CHECK(text.find("link = north south") != std::string_view::npos);
    CHECK(text.find("link = north ground") != std::string_view::npos);

    World reloaded;
    reloaded.init(bench.arena);
    CHECK(zone_reload(dir.view(), bench.arena, bench.arena, reloaded, bench.phys, bench.terrain, nullptr));
    u32 islands = 0;
    u32 links = 0;
    for (u32 idx : reloaded.entities().live_indices()) {
        const Entity* e = reloaded.entities().at(idx);
        if (!e) {
            continue;
        }
        if (e->kind == EntityKind::Island && e->mesh_name == "north") {
            islands++;
            CHECK(distance(e->pos, Vec3{10.0f, 14.5f, -6.0f}) < 0.01f);
            CHECK(f_abs(e->rot.x * tilt.x + e->rot.y * tilt.y + e->rot.z * tilt.z + e->rot.w * tilt.w) > 0.9999f);
            CHECK_NEAR(e->half.x, 11.5f, 0.01);
            CHECK_NEAR(e->aux_value, 8.25f, 0.01);
            CHECK(e->aux_data == 77u);
            CHECK(e->aux_kind == (1u | (1u << 8)));
        } else if (e->kind == EntityKind::Island && e->mesh_name == "south") {
            islands++;
            CHECK(e->aux_data == 5u);
            CHECK(e->aux_kind == 0u);
        } else if (e->kind == EntityKind::IslandLink && e->mesh_name == "north>south") {
            links++;
            CHECK_NEAR(e->aux_value, 6.5f, 0.01);
            CHECK(e->aux_kind == 0u);
        } else if (e->kind == EntityKind::IslandLink && e->mesh_name == "north>ground") {
            links++;
            CHECK_NEAR(e->aux_value, 9.0f, 0.01);
            CHECK(e->aux_kind == 1u);
            CHECK_NEAR(e->pos.x, 3.0f, 0.01);
            CHECK_NEAR(e->pos.z, 40.0f, 0.01);
        }
    }
    CHECK(islands == 2);
    CHECK(links == 2);
}
