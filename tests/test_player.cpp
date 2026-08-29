#include "test.h"

#include "core/arena.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "player/player.h"
#include "vehicle/vehicle.h"

using namespace anom;

namespace {

constexpr f32 kDt = 1.0f / 120.0f;

struct WalkRig {
    Arena arena{megabytes(64)};
    Heightfield hf;
    PhysWorld world;
    Player player;

    void setup(f32 slope = 0.0f)
    {
        hf.alloc(arena, 96, 2.0f);
        for (u32 z = 0; z < 96; z++) {
            for (u32 x = 0; x < 96; x++) {
                const f32 wx = hf.origin().x + static_cast<f32>(x) * hf.cell_size();
                hf.set_height(x, z, f_max(wx, 0.0f) * slope);
            }
        }
        hf.recompute_extents();
        world.init(arena, &hf);
        player.init(Vec3{0.0f, 0.5f, 0.0f}, 0.0f);
    }

    void run(const PlayerCommand& cmd, i32 ticks, Vehicle* veh = nullptr)
    {
        for (i32 i = 0; i < ticks; i++) {
            player.tick(world, veh, cmd, kDt);
            world.tick(kDt);
        }
    }
};

} // namespace

TEST(player, falls_and_lands_on_the_ground)
{
    WalkRig rig;
    rig.setup();
    rig.player.init(Vec3{0.0f, 5.0f, 0.0f}, 0.0f);

    rig.run(PlayerCommand{}, 360);
    CHECK(rig.player.grounded());
    CHECK_NEAR(rig.player.pos().y, 0.0f, 0.05);
}

TEST(player, walks_forward)
{
    WalkRig rig;
    rig.setup();
    rig.run(PlayerCommand{}, 120);

    PlayerCommand cmd;
    cmd.move_z = 1.0f;
    const Vec3 before = rig.player.pos();
    rig.run(cmd, 120);
    const Vec3 after = rig.player.pos();

    CHECK(after.z < before.z - 2.0f);
    CHECK(f_abs(after.x - before.x) < 0.2f);
}

TEST(player, running_is_faster_than_walking)
{
    WalkRig walk;
    walk.setup();
    walk.run(PlayerCommand{}, 120);
    PlayerCommand cmd;
    cmd.move_z = 1.0f;
    const Vec3 walk_start = walk.player.pos();
    walk.run(cmd, 240);
    const f32 walked = distance(walk_start, walk.player.pos());

    WalkRig run;
    run.setup();
    run.run(PlayerCommand{}, 120);
    cmd.run = true;
    const Vec3 run_start = run.player.pos();
    run.run(cmd, 240);
    const f32 ran = distance(run_start, run.player.pos());

    CHECK(ran > walked * 1.3f);
}

TEST(player, strafes_sideways)
{
    WalkRig rig;
    rig.setup();
    rig.run(PlayerCommand{}, 120);

    PlayerCommand cmd;
    cmd.move_x = 1.0f;
    const Vec3 before = rig.player.pos();
    rig.run(cmd, 120);

    CHECK(rig.player.pos().x > before.x + 2.0f);
}

TEST(player, jumps_and_returns_to_ground)
{
    WalkRig rig;
    rig.setup();
    rig.run(PlayerCommand{}, 120);
    CHECK(rig.player.grounded());

    PlayerCommand jump;
    jump.jump = true;
    rig.player.tick(rig.world, nullptr, jump, kDt);
    CHECK(!rig.player.grounded());
    CHECK(rig.player.vel().y > 3.0f);

    rig.run(PlayerCommand{}, 240);
    CHECK(rig.player.grounded());
}

TEST(player, speed_multiplier_slows_movement)
{
    WalkRig rig;
    rig.setup();
    rig.run(PlayerCommand{}, 120);
    rig.player.set_speed_mul(0.5f);

    PlayerCommand cmd;
    cmd.move_z = 1.0f;
    const Vec3 before = rig.player.pos();
    rig.run(cmd, 240);
    const f32 moved = distance(before, rig.player.pos());

    WalkRig full;
    full.setup();
    full.run(PlayerCommand{}, 120);
    const Vec3 full_before = full.player.pos();
    full.run(cmd, 240);
    const f32 full_moved = distance(full_before, full.player.pos());

    CHECK(moved < full_moved * 0.7f);
}

TEST(player, look_clamps_pitch)
{
    WalkRig rig;
    rig.setup();
    for (i32 i = 0; i < 400; i++) {
        rig.player.look(0.0f, -100.0f);
    }
    CHECK(rig.player.pitch() <= 89.0f * kDegToRad + 1e-3f);

    for (i32 i = 0; i < 800; i++) {
        rig.player.look(0.0f, 100.0f);
    }
    CHECK(rig.player.pitch() >= -89.0f * kDegToRad - 1e-3f);
}

TEST(player, does_not_pass_through_static_walls)
{
    WalkRig rig;
    rig.setup();
    rig.world.statics_reserve(rig.arena, 16);

    const f32 z = -3.0f;
    rig.world.add_static_tri(Vec3{-6.0f, 0.0f, z}, Vec3{6.0f, 0.0f, z}, Vec3{6.0f, 4.0f, z});
    rig.world.add_static_tri(Vec3{-6.0f, 0.0f, z}, Vec3{6.0f, 4.0f, z}, Vec3{-6.0f, 4.0f, z});
    rig.world.statics_build(rig.arena);

    rig.run(PlayerCommand{}, 120);
    PlayerCommand cmd;
    cmd.move_z = 1.0f;
    rig.run(cmd, 600);

    CHECK(rig.player.pos().z > z - 0.1f);
}

TEST(player, cannot_enter_a_car_from_far_away)
{
    WalkRig rig;
    rig.setup();
    Vehicle car;
    CHECK(car.init(rig.world, rig.arena, "assets/cars/excel.cfg", Vec3{40.0f, 1.0f, 0.0f},
                   0.0f));
    rig.run(PlayerCommand{}, 120);
    CHECK(!rig.player.can_enter(rig.world, &car));
}

TEST(player, enters_and_exits_a_car)
{
    WalkRig rig;
    rig.setup();

    Vehicle car;
    CHECK(car.init(rig.world, rig.arena, "assets/cars/excel.cfg", Vec3{0.0f, 1.0f, -4.0f},
                   0.0f));
    car.effects().ignition_ok = false;
    rig.player.init(Vec3{4.0f, 0.5f, -4.3f}, 0.0f);
    for (i32 i = 0; i < 240; i++) {
        car.tick(rig.world, kDt);
        rig.player.tick(rig.world, &car, PlayerCommand{}, kDt);
        rig.world.tick(kDt);
    }

    PlayerCommand walk;
    walk.move_x = -1.0f;
    for (i32 i = 0; i < 200 && !rig.player.can_enter(rig.world, &car); i++) {
        car.tick(rig.world, kDt);
        rig.player.tick(rig.world, &car, walk, kDt);
        rig.world.tick(kDt);
    }
    CHECK(rig.player.can_enter(rig.world, &car));

    PlayerCommand interact;
    interact.interact = true;
    rig.player.tick(rig.world, &car, interact, kDt);
    CHECK(rig.player.state() == PlayerState::Entering);

    for (i32 i = 0; i < 120; i++) {
        car.tick(rig.world, kDt);
        rig.player.tick(rig.world, &car, PlayerCommand{}, kDt);
        rig.world.tick(kDt);
    }
    CHECK(rig.player.driving());
    CHECK(rig.player.can_exit(rig.world, &car));

    rig.player.tick(rig.world, &car, interact, kDt);
    CHECK(rig.player.state() == PlayerState::Exiting);

    for (i32 i = 0; i < 240; i++) {
        car.tick(rig.world, kDt);
        rig.player.tick(rig.world, &car, PlayerCommand{}, kDt);
        rig.world.tick(kDt);
    }
    CHECK(rig.player.state() == PlayerState::OnFoot);
    CHECK(rig.player.grounded());
}

TEST(player, cannot_exit_a_moving_car)
{
    WalkRig rig;
    rig.setup();
    Vehicle car;
    CHECK(car.init(rig.world, rig.arena, "assets/cars/excel.cfg", Vec3{0.0f, 1.0f, 0.0f},
                   0.0f));

    rig.player.init(Vec3{0.0f, 0.5f, 0.0f}, 0.0f);
    RigidBody* body = rig.world.body(car.body());
    body->vel = Vec3{0.0f, 0.0f, -20.0f};
    CHECK(!rig.player.can_exit(rig.world, &car));
}

TEST(player, walking_is_deterministic)
{
    const auto simulate = [](Vec3& out) {
        WalkRig rig;
        rig.setup(0.15f);
        PlayerCommand cmd;
        cmd.move_z = 1.0f;
        cmd.move_x = 0.4f;
        cmd.run = true;
        rig.run(cmd, 900);
        out = rig.player.pos();
    };

    Vec3 a;
    Vec3 b;
    simulate(a);
    simulate(b);
    CHECK(a.x == b.x);
    CHECK(a.y == b.y);
    CHECK(a.z == b.z);
}
