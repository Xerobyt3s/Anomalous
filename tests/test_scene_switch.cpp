#include "test.h"

#include "core/arena.h"
#include "game/ballistics/surface.h"
#include "math/glm_bridge.h"
#include "sim/sim.h"
#include "world/scenes.h"
#include "world/zone.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <variant>

using namespace anom;

namespace {

constexpr const char* kZoneDir = "assets/zones/testzone";

struct Stage {
    Arena perm{megabytes(256)};
    Arena scratch{megabytes(256)};
    std::unique_ptr<Sim> sim = std::make_unique<Sim>();
    ghost::game::EventList seen;

    bool setup() { return sim->init(perm, scratch, kZoneDir); }

    void run(PlayerCommand cmd, u32 ticks)
    {
        for (u32 i = 0; i < ticks; i++) {
            cmd.gameplay = true;
            const Player& p = sim->player(0);
            if (length_sq(cmd.view_dir) < 1e-6f) {
                cmd.view_dir = Vec3{0.0f, 0.0f, -1.0f};
            }
            cmd.view_origin = p.pos() + p.up() * kPlayerEyeHeight;
            sim->tick(cmd, kFixedDt);
            seen.insert(seen.end(), sim->events().begin(), sim->events().end());
            sim->events().clear();
            cmd.scene = -1;
        }
    }

    void go(std::string_view id)
    {
        PlayerCommand c;
        c.scene = scene_index(id);
        run(c, 1);
        run(PlayerCommand{}, 30);
    }
};

u32 count_kind(const Sim& sim, EntityKind kind)
{
    return static_cast<u32>(sim.scene_entities(kind).size());
}

}

TEST(scene_switch, every_listed_scene_loads_and_says_what_it_is)
{
    Stage stage;
    if (!stage.setup()) {
        FAIL("sim init");
        return;
    }
    CHECK(stage.sim->scene().kind == SceneKind::Zone);
    CHECK(stage.sim->has_car());
    for (const SceneEntry& entry : scenes()) {
        CHECK(stage.sim->switch_scene(scene_index(entry.id)));
        CHECK(stage.sim->zone_dir() == entry.zone_dir);
    }
    CHECK(stage.sim->switch_scene(scene_index("yard")));
    CHECK(stage.sim->scene().kind == SceneKind::Arena);
    CHECK(!stage.sim->has_car());
    CHECK(count_kind(*stage.sim, EntityKind::Prop) == 29u);
    CHECK(count_kind(*stage.sim, EntityKind::SpawnPoint) == 4u);
    CHECK(count_kind(*stage.sim, EntityKind::Bench) == 4u);
    CHECK(stage.sim->switch_scene(scene_index("range")));
    CHECK(stage.sim->scene().kind == SceneKind::Range);
    CHECK(count_kind(*stage.sim, EntityKind::Prop) == 22u);
}

TEST(scene_switch, an_arena_puts_everyone_at_their_spawn_with_the_arena_kit_and_no_car)
{
    Stage stage;
    if (!stage.setup()) {
        FAIL("sim init");
        return;
    }
    const PlayerId second = stage.sim->add_player("Second");
    stage.go("yard");
    const ghost::game::PlayerRules& rules = stage.sim->player_rules();
    CHECK(rules.arena);
    CHECK(rules.friendlyFire);
    for (PlayerId id : {PlayerId{0}, second}) {
        const Entity* spawn = stage.sim->spawn_entity_for(id);
        CHECK(spawn != nullptr);
        if (spawn) {
            const Vec3 feet = stage.sim->player(id).pos();
            CHECK(length(Vec3{feet.x - spawn->pos.x, 0.0f, feet.z - spawn->pos.z}) < 0.3f);
        }
    }
    CHECK(length(stage.sim->player(0).pos() - stage.sim->player(second).pos()) > 5.0f);
    CHECK(stage.sim->slot(0)->gun.pouch.count(ghost::game::kPlainElement) == 30);
    const RigidBody* car = stage.sim->phys().body(stage.sim->vehicle().body());
    CHECK(car->pos.y < -1000.0f);
    CHECK(stage.sim->interact(0).action() != InteractAction::EnterCar);

    stage.go("testzone");
    CHECK(!stage.sim->player_rules().arena);
    CHECK(stage.sim->has_car());
    CHECK(stage.sim->phys().body(stage.sim->vehicle().body())->pos.y > -100.0f);
}

TEST(scene_switch, props_are_solid_and_answer_with_their_surface)
{
    Stage stage;
    if (!stage.setup()) {
        FAIL("sim init");
        return;
    }
    stage.go("yard");
    const auto wall = stage.sim->physics().raycast(glm::vec3(0.0f, 1.0f, 15.0f), glm::vec3(0.0f, 1.0f, 30.0f));
    CHECK(wall.has_value());
    if (wall) {
        CHECK(static_cast<ghost::game::Surface>(wall->userData) == ghost::game::Surface::Concrete);
        CHECK(std::abs(wall->point.z - 20.0f) < 0.05f);
    }
    const auto plate = stage.sim->physics().raycast(glm::vec3(-10.0f, 1.0f, -13.0f), glm::vec3(-14.0f, 1.0f, -13.0f));
    CHECK(plate.has_value());
    if (plate) {
        CHECK(static_cast<ghost::game::Surface>(plate->userData) == ghost::game::Surface::Steel);
    }
}

TEST(scene_switch, downed_in_the_arena_you_come_back_at_your_spawn_with_a_fresh_kit)
{
    Stage stage;
    if (!stage.setup()) {
        FAIL("sim init");
        return;
    }
    stage.go("yard");
    PlayerSlot& me = *stage.sim->slot(0);
    PlayerCommand walk;
    walk.move_z = 1.0f;
    stage.run(walk, 120);
    me.gun.pouch.set(ghost::game::kPlainElement, 2);
    const Vec3 spawn = stage.sim->spawn_entity_for(0)->pos;
    CHECK(length(Vec3{me.player.pos().x - spawn.x, 0.0f, me.player.pos().z - spawn.z}) > 1.0f);
    stage.sim->gameplay().hurtPlayer(0, 2.0f, glm::vec3(0.0f), stage.sim->events());
    stage.run(PlayerCommand{}, 2);
    CHECK(stage.sim->roster().downed(0));
    stage.run(PlayerCommand{}, static_cast<u32>((stage.sim->player_rules().arenaRespawn + 0.5f) * 120.0f));
    CHECK(!stage.sim->roster().downed(0));
    CHECK(length(Vec3{me.player.pos().x - spawn.x, 0.0f, me.player.pos().z - spawn.z}) < 0.3f);
    CHECK(me.gun.pouch.count(ghost::game::kPlainElement) == 30);
}

TEST(scene_switch, holding_e_at_your_bench_fills_your_speedloaders_from_the_pouch)
{
    Stage stage;
    if (!stage.setup()) {
        FAIL("sim init");
        return;
    }
    stage.go("yard");
    PlayerSlot& me = *stage.sim->slot(0);
    const Entity* bench = stage.sim->bench_for(0);
    CHECK(bench != nullptr);
    if (!bench) {
        return;
    }
    const Vec3 spawn = stage.sim->spawn_entity_for(0)->pos;
    const Vec3 toward = normalize(Vec3{bench->pos.x - spawn.x, 0.0f, bench->pos.z - spawn.z});
    me.player.init(Vec3{bench->pos.x, 0.0f, bench->pos.z} - toward * 1.0f, 0.0f);
    stage.run(PlayerCommand{}, 10);
    CHECK(me.at_bench);
    const int before = me.gun.pouch.count(ghost::game::kPlainElement);
    PlayerCommand hold;
    hold.use_down = true;
    stage.run(hold, 240);
    CHECK(me.gun.speedloaders.filled() == ghost::game::kSpeedloadersCarried);
    CHECK(me.gun.speedloaders.loaders[1].count() == ghost::game::kSpeedloaderSlots);
    CHECK(me.gun.pouch.count(ghost::game::kPlainElement) == before - ghost::game::kSpeedloaderSlots * ghost::game::kSpeedloadersCarried);
}

TEST(scene_switch, scene_entities_round_trip_through_zone_save)
{
    Stage stage;
    if (!stage.setup()) {
        FAIL("sim init");
        return;
    }
    stage.go("alleys");
    const u32 props = count_kind(*stage.sim, EntityKind::Prop);
    const u32 spawns = count_kind(*stage.sim, EntityKind::SpawnPoint);
    const Entity* first = stage.sim->spawn_entity_for(0);
    const Vec3 first_pos = first->pos;
    const f32 first_yaw = stage.sim->spawn_yaw(0);
    World copy;
    copy.init(stage.perm);
    const char* dir = "build/test_scene_save";
    std::filesystem::create_directories(dir);
    std::filesystem::copy_file("assets/zones/alleys/zone.cfg", std::string(dir) + "/zone.cfg",
                               std::filesystem::copy_options::overwrite_existing);
    CHECK(zone_save(dir, stage.scratch, stage.sim->world(), stage.sim->phys(), stage.sim->terrain()));
    CHECK(zone_reload(dir, stage.perm, stage.scratch, copy, stage.sim->phys(), stage.sim->terrain(), nullptr));
    u32 copied_props = 0;
    u32 copied_spawns = 0;
    bool found = false;
    for (u32 idx : copy.entities().live_indices()) {
        const Entity* e = copy.entities().at(idx);
        copied_props += e->kind == EntityKind::Prop ? 1u : 0u;
        copied_spawns += e->kind == EntityKind::SpawnPoint ? 1u : 0u;
        if (e->kind == EntityKind::SpawnPoint && e->aux_kind == first->aux_kind) {
            found = length(e->pos - first_pos) < 1e-3f && std::abs(e->aux_value * kDegToRad - first_yaw) < 1e-3f;
        }
    }
    CHECK(copied_props == props);
    CHECK(copied_spawns == spawns);
    CHECK(found);
}

TEST(scene_switch, a_moved_prop_takes_its_collision_with_it)
{
    Stage stage;
    if (!stage.setup()) {
        FAIL("sim init");
        return;
    }
    stage.go("range");
    Entity* table = nullptr;
    for (u32 idx : stage.sim->world().entities().live_indices()) {
        Entity* e = stage.sim->world().entities().at(idx);
        if (e->kind == EntityKind::Prop && length(Vec3{e->pos.x, 0.0f, e->pos.z}) < 0.1f) {
            table = e;
        }
    }
    CHECK(table != nullptr);
    if (!table) {
        return;
    }
    const auto down = [&](f32 x) { return stage.sim->physics().raycast(glm::vec3(x, 3.0f, 0.0f), glm::vec3(x, -1.0f, 0.0f)); };
    CHECK(down(0.0f)->point.y > 0.8f);
    table->pos.x = 6.0f;
    stage.sim->watch_islands(1.0f);
    stage.run(PlayerCommand{}, 2);
    CHECK(down(0.0f)->point.y < 0.1f);
    CHECK(down(6.0f)->point.y > 0.8f);
}

TEST(scene_switch, ghosts_over_island_craters_keep_their_height_through_zone_save)
{
    Stage stage;
    if (!stage.setup()) {
        FAIL("sim init");
        return;
    }
    std::vector<Vec3> before;
    for (u32 idx : stage.sim->world().entities().live_indices()) {
        const Entity* e = stage.sim->world().entities().at(idx);
        if (e->kind == EntityKind::GhostSpawn) {
            before.push_back(e->pos);
        }
    }
    CHECK(before.size() >= 8u);
    const Terrain& terrain = stage.sim->terrain();
    bool cratered = false;
    for (const Vec3& p : before) {
        cratered = cratered || std::abs(terrain.heightfield().sample(p.x, p.z) - terrain.base_height(p.x, p.z)) > 0.5f;
    }
    CHECK(cratered);
    const char* dir = "build/test_ghost_save";
    std::filesystem::create_directories(dir);
    std::filesystem::copy_file("assets/zones/testzone/zone.cfg", std::string(dir) + "/zone.cfg",
                               std::filesystem::copy_options::overwrite_existing);
    CHECK(zone_save(dir, stage.scratch, stage.sim->world(), stage.sim->phys(), stage.sim->terrain()));
    World copy;
    copy.init(stage.perm);
    CHECK(zone_reload(dir, stage.perm, stage.scratch, copy, stage.sim->phys(), stage.sim->terrain(), nullptr));
    u32 matched = 0;
    for (u32 idx : copy.entities().live_indices()) {
        const Entity* e = copy.entities().at(idx);
        if (e->kind != EntityKind::GhostSpawn) {
            continue;
        }
        for (const Vec3& p : before) {
            matched += length(e->pos - p) < 2e-3f ? 1u : 0u;
        }
    }
    CHECK(matched == before.size());
}

TEST(scene_switch, the_parked_car_stays_put_in_a_scene_without_a_car)
{
    Stage stage;
    if (!stage.setup()) {
        FAIL("sim init");
        return;
    }
    stage.go("yard");
    CHECK(!stage.sim->has_car());
    const RigidBody* car = stage.sim->phys().body(stage.sim->vehicle().body());
    const Vec3 parked = car->pos;
    stage.run(PlayerCommand{}, 1200);
    car = stage.sim->phys().body(stage.sim->vehicle().body());
    CHECK(length(car->vel) < 0.5f);
    CHECK(length(car->pos - parked) < 0.5f);
}
