#include "test.h"

#include "carsys/items.h"
#include "core/arena.h"
#include "engine/physics/physics_world.h"
#include "game/ballistics/surface.h"
#include "math/glm_bridge.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "player/interact.h"
#include "player/movement.h"
#include "sim/sim.h"
#include "world/entity.h"
#include "world/pickup_body.h"

#include <memory>

using namespace anom;

namespace {
constexpr f32 kDt = 1.0f / 120.0f;

struct Yard {
    Arena arena{megabytes(32)};
    Heightfield hf;
    PhysWorld phys;
    ghost::engine::PhysicsWorld jolt;
    World world;

    Yard()
    {
        hf.init_procedural(arena, 96, 1.0f, 5u, 0.0f);
        phys.init(arena, &hf);
        phys.set_jolt(&jolt);
        world.init(arena);
    }

    void run(i32 ticks)
    {
        for (i32 i = 0; i < ticks; i++) {
            jolt.step(kDt);
        }
    }

    PickupState state(EntityHandle h) const
    {
        PickupState s;
        const Entity* e = world.entity(h);
        if (e) {
            pickup_body_state(phys, *e, s);
        }
        return s;
    }
};

}

TEST(pickups, a_pickup_is_a_jolt_body_that_falls_and_rests_on_the_terrain)
{
    Yard yard;
    const EntityHandle h = interact_spawn_pickup(yard.world, yard.phys, Item{ITEM_BATTERY}, Vec3{2.0f, 3.0f, 2.0f},
                                                 0.0f, Vec3{});
    const Entity* e = yard.world.entity(h);
    CHECK(e != nullptr);
    CHECK(pickup_has_body(yard.phys, *e));
    CHECK(yard.jolt.dynamicCount() == 1);
    yard.run(360);
    const PickupState s = yard.state(h);
    const f32 half = item_cargo_half(ITEM_BATTERY).y;
    CHECK_NEAR(s.pos.y, yard.hf.sample(2.0f, 2.0f) + half, 0.05);
    CHECK(length(s.vel) < 0.05f);
}

TEST(pickups, stacked_pickups_stay_stacked_without_sinking_into_each_other)
{
    Yard yard;
    const f32 half = item_cargo_half(ITEM_BATTERY).y;
    const EntityHandle low = interact_spawn_pickup(yard.world, yard.phys, Item{ITEM_BATTERY},
                                                   Vec3{0.0f, half + 0.01f, 0.0f}, 0.0f, Vec3{});
    const EntityHandle high = interact_spawn_pickup(yard.world, yard.phys, Item{ITEM_BATTERY},
                                                    Vec3{0.0f, 3.0f * half + 0.05f, 0.0f}, 0.0f, Vec3{});
    yard.run(480);
    const PickupState a = yard.state(low);
    const PickupState b = yard.state(high);
    CHECK(b.pos.y - a.pos.y > 2.0f * half - 0.03f);
    CHECK(length(Vec3{b.pos.x - a.pos.x, 0.0f, b.pos.z - a.pos.z}) < 0.1f);
}

TEST(pickups, the_gravity_field_pulls_jolt_bodies_too)
{
    Yard yard;
    yard.jolt.setGravityField([](const glm::vec3&) { return glm::vec3(4.0f, 0.0f, 0.0f); });
    const EntityHandle h = interact_spawn_pickup(yard.world, yard.phys, Item{ITEM_BATTERY}, Vec3{0.0f, 5.0f, 0.0f},
                                                 0.0f, Vec3{});
    yard.run(120);
    const PickupState s = yard.state(h);
    CHECK_NEAR(s.pos.x, 0.5f * 4.0f * 1.0f, 0.1);
    CHECK_NEAR(s.pos.y, 5.0f, 0.05);
}

TEST(pickups, rebuilding_the_statics_keeps_pickups_and_characters)
{
    Yard yard;
    Movement move;
    move.set_character(0);
    move.init(&yard.jolt, Vec3{10.0f, 0.0f, 10.0f}, Vec3{0.0f, 1.0f, 0.0f}, 0.0f);
    const EntityHandle h = interact_spawn_pickup(yard.world, yard.phys, Item{ITEM_BATTERY}, Vec3{2.0f, 1.0f, 2.0f},
                                                 0.0f, Vec3{});
    const u32 statics = yard.jolt.staticCount();
    yard.phys.sync_jolt();
    yard.phys.sync_jolt();
    CHECK(yard.jolt.staticCount() == statics);
    CHECK(yard.jolt.characterValid(0));
    CHECK(pickup_has_body(yard.phys, *yard.world.entity(h)));
}

TEST(pickups, the_car_shoves_a_pickup_and_feels_it)
{
    Yard yard;
    const glm::vec3 half{1.0f, 0.6f, 2.2f};
    glm::vec3 car{0.0f, 0.55f, 0.0f};
    yard.jolt.setVehicle(car, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), half, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f),
                         static_cast<std::uint64_t>(ghost::game::Surface::Steel));
    const EntityHandle h = interact_spawn_pickup(yard.world, yard.phys, Item{ITEM_BATTERY}, Vec3{0.0f, 0.3f, -3.0f},
                                                 0.0f, Vec3{});
    yard.run(60);
    const f32 before = yard.state(h).pos.z;
    glm::vec3 total{0.0f};
    for (i32 i = 0; i < 90; i++) {
        car.z -= 6.0f * kDt;
        yard.jolt.moveVehicle(car, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), kDt);
        yard.jolt.step(kDt);
        for (const ghost::engine::VehiclePush& push : yard.jolt.takeVehiclePushes()) {
            total += push.impulse;
        }
    }
    CHECK(yard.state(h).pos.z < before - 1.0f);
    CHECK(total.z > 1.0f);
}

TEST(pickups, rays_hit_pickups_and_the_car_unless_only_statics_are_wanted)
{
    Yard yard;
    yard.jolt.setVehicle(glm::vec3(5.0f, 0.8f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.6f, 2.2f),
                         glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f),
                         static_cast<std::uint64_t>(ghost::game::Surface::Steel));
    interact_spawn_pickup(yard.world, yard.phys, Item{ITEM_BATTERY}, Vec3{0.0f, 2.0f, 0.0f}, 0.0f, Vec3{});
    const auto on_pickup = yard.jolt.raycast(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, -10.0f, 0.0f));
    CHECK(on_pickup.has_value());
    CHECK(on_pickup && on_pickup->dynamic);
    const auto on_car = yard.jolt.raycast(glm::vec3(5.0f, 10.0f, 0.0f), glm::vec3(5.0f, -10.0f, 0.0f));
    CHECK(on_car && on_car->userData == static_cast<std::uint64_t>(ghost::game::Surface::Steel));
    ghost::engine::RayFilter ground;
    ground.staticOnly = true;
    const auto through = yard.jolt.raycast(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, -10.0f, 0.0f), ground);
    CHECK(through && !through->dynamic);
    CHECK(through && through->point.y < 0.5f);
}

TEST(pickups, a_thrown_pickup_in_the_sim_flies_and_lands)
{
    Arena perm{megabytes(256)};
    Arena scratch{megabytes(256)};
    auto sim = std::make_unique<Sim>();
    if (!sim->init(perm, scratch, "assets/zones/testzone")) {
        FAIL("sim init");
        return;
    }
    for (i32 i = 0; i < 30; i++) {
        sim->tick(PlayerCommand{}, kFixedDt);
    }
    sim->interact(0).hands() = Item{ITEM_BATTERY};
    PlayerCommand toss;
    toss.gameplay = true;
    toss.view_origin = sim->player(0).pos() + Vec3{0.0f, kPlayerEyeHeight, 0.0f};
    toss.view_dir = normalize(Vec3{1.0f, 0.4f, 0.0f});
    toss.throw_power = 1.0f;
    const u32 before = sim->physics().dynamicCount();
    sim->tick(toss, kFixedDt);
    CHECK(sim->interact(0).hands().kind == ITEM_NONE);
    CHECK(sim->physics().dynamicCount() == before + 1);
    for (i32 i = 0; i < 480; i++) {
        sim->tick(PlayerCommand{}, kFixedDt);
    }
    bool resting = false;
    for (u32 idx : sim->world().entities().live_indices()) {
        const Entity* e = sim->world().entities().at(idx);
        PickupState s;
        if (e && static_cast<ItemKind>(e->aux_kind) == ITEM_BATTERY && pickup_body_state(sim->phys(), *e, s)) {
            resting = resting || (length(s.vel) < 0.1f && length(s.pos - sim->player(0).pos()) > 1.0f);
        }
    }
    CHECK(resting);
}
