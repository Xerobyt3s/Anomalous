#include "test.h"

#include "core/arena.h"
#include "physics/heightfield.h"
#include "engine/physics/physics_world.h"
#include "math/glm_bridge.h"
#include "physics/world.h"
#include "app/player_view.h"
#include "player/player.h"
#include "render/camera.h"
#include "physics/body.h"
#include "vehicle/vehicle.h"

#include <cmath>

using namespace anom;

namespace {
constexpr f32 kDt = 1.0f / 120.0f;

struct WalkRig {
    Arena arena{megabytes(64)};
    Heightfield hf;
    ghost::engine::PhysicsWorld jolt;
    PhysWorld world;
    Player player;
    PlayerView view;

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
        world.set_jolt(&jolt);
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

}

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
    rig.player.init(Vec3{-4.0f, 0.5f, -4.3f}, 0.0f);
    for (i32 i = 0; i < 240; i++) {
        car.tick(rig.world, kDt);
        rig.player.tick(rig.world, &car, PlayerCommand{}, kDt);
        rig.world.tick(kDt);
    }

    PlayerCommand walk;
    walk.move_x = 1.0f;
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

TEST(player, the_camera_sits_at_eye_height_on_foot)
{
    WalkRig rig;
    rig.setup();
    rig.player.init(Vec3{3.0f, 0.0f, 4.0f}, 0.6f);

    Camera cam;
    rig.view.camera(rig.player, rig.world, nullptr, 1.0f, kDt, false, 0.0f, 0.0f, cam);

    CHECK_NEAR(cam.pos.x, 3.0f, 1e-4);
    CHECK_NEAR(cam.pos.y, kPlayerEyeHeight, 1e-4);
    CHECK_NEAR(cam.pos.z, 4.0f, 1e-4);
    CHECK_NEAR(cam.yaw, 0.6f, 1e-5);
}

TEST(player, the_camera_interpolates_between_ticks)
{
    WalkRig rig;
    rig.setup();
    rig.player.init(Vec3{0.0f, 0.0f, 0.0f}, 0.0f);

    PlayerCommand cmd;
    cmd.move_z = 1.0f;
    for (i32 i = 0; i < 30; i++) {
        rig.player.tick(rig.world, nullptr, cmd, kDt);
    }

    Camera at_prev;
    Camera at_now;
    rig.view.camera(rig.player, rig.world, nullptr, 0.0f, kDt, false, 0.0f, 0.0f, at_prev);
    rig.view.camera(rig.player, rig.world, nullptr, 1.0f, kDt, false, 0.0f, 0.0f, at_now);

    CHECK(distance(at_prev.pos, at_now.pos) > 0.0f);
    CHECK_NEAR(at_prev.pos.z, rig.player.prev_pos().z + 0.0f, 1e-4);
    CHECK_NEAR(at_now.pos.z, rig.player.pos().z, 1e-4);
}

namespace {
struct ChaseRig {
    Arena arena{megabytes(64)};
    Heightfield hf;
    PhysWorld world;
    ghost::engine::PhysicsWorld jolt;
    Vehicle veh;
    Player player;
    PlayerView view;

    bool setup()
    {
        hf.init_procedural(arena, 128, 2.0f, 1u, 0.0f);
        world.init(arena, &hf);
        world.set_jolt(&jolt);
        if (!veh.init(world, arena, "assets/cars/excel.cfg", Vec3{0.0f, 0.9f, 0.0f}, 0.0f)) {
            return false;
        }
        const RigidBody* car = world.body(veh.body());
        player.init(Vec3{car->pos.x - car->half_extents.x - 0.5f, 0.0f, car->pos.z}, 0.0f);

        PlayerCommand cmd;
        cmd.interact = true;
        player.tick(world, &veh, cmd, kDt);
        cmd.interact = false;
        for (i32 i = 0; i < 300 && !player.driving(); i++) {
            player.tick(world, &veh, cmd, kDt);
            world.tick(kDt);
        }
        return player.driving();
    }

    Camera settle(Vec3 vel, i32 ticks)
    {
        Camera cam;
        for (i32 i = 0; i < ticks; i++) {
            world.body(veh.body())->vel = vel;
            view.camera(player, world, &veh, 1.0f, kDt, true, 0.0f, 0.0f, cam);
        }
        return cam;
    }
};

}

TEST(chase_cam, the_fov_widens_with_speed)
{
    ChaseRig rig;
    CHECK(rig.setup());
    const Camera slow = rig.settle(Vec3{0.0f, 0.0f, 0.0f}, 200);
    CHECK_NEAR(slow.fov_y, 70.0f * kDegToRad, 1e-3);
    const Camera fast = rig.settle(Vec3{0.0f, 0.0f, -35.0f}, 400);
    CHECK(fast.fov_y > 78.0f * kDegToRad);
    CHECK(fast.fov_y < 83.0f * kDegToRad);
}

TEST(chase_cam, an_impact_kick_decays_to_nothing)
{
    ChaseRig rig;
    CHECK(rig.setup());
    const Camera before = rig.settle(Vec3{0.0f, 0.0f, 0.0f}, 200);
    rig.view.kick(1.0f);
    const Camera kicked = rig.settle(Vec3{0.0f, 0.0f, 0.0f}, 3);
    CHECK(f_abs(kicked.yaw - before.yaw) + f_abs(kicked.pitch - before.pitch) + f_abs(kicked.roll - before.roll) > 1e-3f);
    const Camera after = rig.settle(Vec3{0.0f, 0.0f, 0.0f}, 80);
    CHECK_NEAR(after.yaw, before.yaw, 1e-3);
    CHECK_NEAR(after.pitch, before.pitch, 1e-3);
    CHECK_NEAR(after.roll, before.roll, 1e-3);
}

TEST(chase_cam, look_behind_turns_the_camera_around)
{
    ChaseRig rig;
    CHECK(rig.setup());
    const Camera ahead = rig.settle(Vec3{0.0f, 0.0f, 0.0f}, 200);
    rig.view.set_look_behind(true);
    const Camera behind = rig.settle(Vec3{0.0f, 0.0f, 0.0f}, 120);
    CHECK_NEAR(f_abs(f_wrap_angle(behind.yaw - ahead.yaw)), kPi, 1e-2);
    rig.view.set_look_behind(false);
    const Camera back = rig.settle(Vec3{0.0f, 0.0f, 0.0f}, 120);
    CHECK_NEAR(f_wrap_angle(back.yaw - ahead.yaw), 0.0f, 1e-2);
}

TEST(chase_cam, the_cockpit_takes_a_fraction_of_the_roll)
{
    ChaseRig rig;
    CHECK(rig.setup());
    Camera cam;
    for (i32 i = 0; i < 60; i++) {
        rig.view.camera(rig.player, rig.world, &rig.veh, 1.0f, kDt, false, 0.0f, 0.0f, cam);
    }
    CHECK_NEAR(cam.roll, 0.0f, 1e-3);
    RigidBody* body = rig.world.body(rig.veh.body());
    body->rot = normalize(quat_from_axis_angle(Vec3{0.0f, 0.0f, 1.0f}, 0.3f) * body->rot);
    body->prev_rot = body->rot;
    rig.view.camera(rig.player, rig.world, &rig.veh, 1.0f, kDt, false, 0.0f, 0.0f, cam);
    CHECK_NEAR(f_abs(cam.roll), 0.35f * 0.3f, 0.02);
}

TEST(chase_cam, the_boom_sits_behind_a_stationary_car)
{
    ChaseRig rig;
    CHECK(rig.setup());

    const Camera cam = rig.settle(Vec3{0.0f, 0.0f, 0.0f}, 200);
    const RigidBody* car = rig.world.body(rig.veh.body());

    CHECK_NEAR(cam.yaw, 0.0f, 1e-3);
    CHECK(cam.pitch < 0.0f);
    CHECK(cam.pos.z > car->pos.z);
    CHECK(cam.pos.y > car->pos.y);
    CHECK_NEAR(length(Vec2{cam.pos.x - car->pos.x, cam.pos.z - car->pos.z}), 4.9f, 0.4);
}

TEST(chase_cam, the_boom_trails_the_direction_of_travel)
{
    ChaseRig rig;
    CHECK(rig.setup());

    const f32 travel = 40.0f * kDegToRad;
    const f32 speed = 24.0f;
    const Camera cam = rig.settle(Vec3{std::sin(travel) * speed, 0.0f, -std::cos(travel) * speed},
                                  400);

    CHECK(cam.yaw > 0.0f);
    CHECK(cam.yaw < travel);
    CHECK(cam.yaw > travel * 0.5f);
}

TEST(chase_cam, the_boom_pulls_back_with_speed)
{
    ChaseRig rig;
    CHECK(rig.setup());

    const Camera slow = rig.settle(Vec3{0.0f, 0.0f, -1.0f}, 200);
    const Camera fast = rig.settle(Vec3{0.0f, 0.0f, -36.0f}, 400);
    const RigidBody* car = rig.world.body(rig.veh.body());

    const f32 near_d = length(Vec2{slow.pos.x - car->pos.x, slow.pos.z - car->pos.z});
    const f32 far_d = length(Vec2{fast.pos.x - car->pos.x, fast.pos.z - car->pos.z});
    CHECK(far_d > near_d + 1.5f);
}

TEST(chase_cam, leaving_chase_returns_the_camera_to_the_seat)
{
    ChaseRig rig;
    CHECK(rig.setup());

    const Camera chased = rig.settle(Vec3{0.0f, 0.0f, -20.0f}, 200);

    Camera seated;
    rig.view.camera(rig.player, rig.world, &rig.veh, 1.0f, kDt, false, 0.0f, 0.0f, seated);
    const RigidBody* car = rig.world.body(rig.veh.body());

    CHECK(distance(chased.pos, car->pos) > distance(seated.pos, car->pos));
    CHECK(distance(seated.pos, car->pos) < 2.0f);
}

TEST(chase_cam, the_mouse_overrides_the_follow_while_it_is_moving)
{
    ChaseRig rig;
    CHECK(rig.setup());

    rig.settle(Vec3{0.0f, 0.0f, -20.0f}, 60);

    Camera cam;
    rig.view.chase_look(360.0f, 0.0f);
    rig.world.body(rig.veh.body())->vel = Vec3{0.0f, 0.0f, -20.0f};
    rig.view.camera(rig.player, rig.world, &rig.veh, 1.0f, kDt, true, 0.0f, 0.0f, cam);
    const f32 held = cam.yaw;

    for (i32 i = 0; i < 60; i++) {
        rig.view.chase_look(1.0f, 0.0f);
        rig.world.body(rig.veh.body())->vel = Vec3{14.0f, 0.0f, -14.0f};
        rig.view.camera(rig.player, rig.world, &rig.veh, 1.0f, kDt, true, 0.0f, 0.0f, cam);
    }

    CHECK(f_abs(f_wrap_angle(cam.yaw - held)) < 0.25f);
}

TEST(chase_cam, the_look_returns_to_the_follow_once_the_mouse_settles)
{
    ChaseRig rig;
    CHECK(rig.setup());

    rig.settle(Vec3{0.0f, 0.0f, -20.0f}, 60);
    rig.view.chase_look(360.0f, 120.0f);

    Camera cam;
    for (i32 i = 0; i < 30; i++) {
        rig.world.body(rig.veh.body())->vel = Vec3{0.0f, 0.0f, -20.0f};
        rig.view.camera(rig.player, rig.world, &rig.veh, 1.0f, kDt, true, 0.0f, 0.0f, cam);
    }
    CHECK(f_abs(cam.yaw) > 0.5f);

    const Camera settled = rig.settle(Vec3{0.0f, 0.0f, -20.0f}, 900);
    CHECK_NEAR(settled.yaw, 0.0f, 0.05);
    CHECK_NEAR(settled.pitch, -10.0f * kDegToRad, 0.05);
}

TEST(chase_cam, steering_right_turns_the_car_and_the_front_wheels_right)
{
    ChaseRig rig;
    CHECK(rig.setup());

    const RigidBody* car = rig.world.body(rig.veh.body());
    const f32 start_yaw = std::atan2(rotate(car->rot, Vec3{0.0f, 0.0f, -1.0f}).x,
                                     -rotate(car->rot, Vec3{0.0f, 0.0f, -1.0f}).z);

    for (i32 i = 0; i < 400; i++) {
        rig.veh.driver_input(rig.world, 0.30f, 0.0f, 1.0f, false);
        rig.veh.tick(rig.world, kDt);
        rig.world.tick(kDt);
    }

    const Vec3 fwd = rotate(car->rot, Vec3{0.0f, 0.0f, -1.0f});
    const f32 end_yaw = std::atan2(fwd.x, -fwd.z);

    CHECK(f_wrap_angle(end_yaw - start_yaw) > 0.02f);
    CHECK(rig.veh.wheel(WHEEL_FL).steer_rad > 0.0f);
}

TEST(chase_cam, the_steered_wheel_points_the_way_the_car_turns)
{
    Wheel wheel;
    wheel.steer_rad = 0.4f;
    wheel.spin_angle = 0.0f;

    for (bool right_side : {false, true}) {
        const Quat q = wheel_visual_rot(quat_identity(), wheel, right_side);
        const Vec3 axle = rotate(q, Vec3{1.0f, 0.0f, 0.0f});
        const Vec3 tyre = normalize(cross(Vec3{0.0f, 1.0f, 0.0f}, axle));
        CHECK(f_abs(tyre.z) > f_abs(tyre.x));
        CHECK((tyre.z < 0.0f ? tyre.x : -tyre.x) > 0.0f);
    }
}

TEST(travel_zone, a_teleported_driver_stays_in_the_car)
{
    ChaseRig rig;
    CHECK(rig.setup());
    CHECK(rig.player.driving());

    rig.player.teleport(Vec3{120.0f, 4.0f, -60.0f}, 1.2f);

    CHECK(rig.player.driving());
    CHECK_NEAR(rig.player.pos().x, 120.0f, 1e-4);
    CHECK_NEAR(rig.player.pos().z, -60.0f, 1e-4);
}

TEST(travel_zone, arriving_clears_the_suspension_it_had_before)
{
    ChaseRig rig;
    CHECK(rig.setup());

    for (i32 i = 0; i < 400; i++) {
        rig.veh.tick(rig.world, kDt);
        rig.world.tick(kDt);
    }
    CHECK(rig.veh.wheel(WHEEL_FL).grounded);

    rig.veh.reset_contacts();
    for (u32 i = 0; i < kWheelCount; i++) {
        CHECK(!rig.veh.wheel(i).grounded);
        CHECK_NEAR(rig.veh.wheel(i).load, 0.0f, 1e-6);
        CHECK_NEAR(rig.veh.wheel(i).compression, 0.0f, 1e-6);
    }
}

TEST(player, the_passenger_door_is_not_a_way_in)
{
    WalkRig rig;
    rig.setup();

    Vehicle car;
    CHECK(car.init(rig.world, rig.arena, "assets/cars/excel.cfg", Vec3{0.0f, 1.0f, -4.0f},
                   0.0f));
    car.effects().ignition_ok = false;
    for (i32 i = 0; i < 240; i++) {
        car.tick(rig.world, kDt);
        rig.world.tick(kDt);
    }

    const RigidBody* body = rig.world.body(car.body());
    CHECK(body != nullptr);
    const f32 reach = body->half_extents.x + 0.4f;
    const f32 seat_z = body->pos.z + car.config().seat_eye.z;

    rig.player.init(Vec3{body->pos.x + reach, 0.5f, seat_z}, 0.0f);
    rig.player.tick(rig.world, &car, PlayerCommand{}, kDt);
    CHECK(!rig.player.can_enter(rig.world, &car));

    rig.player.init(Vec3{body->pos.x - reach, 0.5f, seat_z}, 0.0f);
    rig.player.tick(rig.world, &car, PlayerCommand{}, kDt);
    CHECK(rig.player.can_enter(rig.world, &car));
}
