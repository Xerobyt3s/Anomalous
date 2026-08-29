#include "test.h"

#include "core/arena.h"
#include "core/rng.h"
#include "physics/body.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "player/player.h"
#include "vehicle/vehicle.h"
#include "world/entity.h"
#include "world/terrain.h"
#include "world/zone.h"

using namespace anom;

namespace {

constexpr f32 kDt = 1.0f / 120.0f;
constexpr const char* kCarCfg = "assets/cars/excel.cfg";
constexpr const char* kZoneDir = "assets/zones/testzone";

constexpr u64 kFnvOffset = 14695981039346656037ull;
constexpr u64 kFnvPrime = 1099511628211ull;

void checksum_bytes(u64& hash, const void* data, u64 size)
{
    const u8* bytes = static_cast<const u8*>(data);
    for (u64 i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
}

void checksum_bodies(u64& hash, const PhysWorld& world)
{
    const Pool<RigidBody>& bodies = world.bodies();
    for (u32 idx : bodies.live_indices()) {
        const RigidBody* body = bodies.at(idx);
        checksum_bytes(hash, &body->pos, sizeof(body->pos));
        checksum_bytes(hash, &body->rot, sizeof(body->rot));
    }
}

void checksum_car(u64& hash, const PhysWorld& world, const Vehicle& veh)
{
    const RigidBody* body = world.body(veh.body());
    checksum_bytes(hash, &body->pos, sizeof(body->pos));
    checksum_bytes(hash, &body->rot, sizeof(body->rot));
    checksum_bytes(hash, &body->vel, sizeof(body->vel));
    checksum_bytes(hash, &body->angular_vel, sizeof(body->angular_vel));
    for (u32 i = 0; i < kWheelCount; i++) {
        const f32 omega = veh.wheel(i).omega;
        checksum_bytes(hash, &omega, sizeof(omega));
    }
}

void checksum_player(u64& hash, const Player& p)
{
    const Vec3 pos = p.pos();
    const Vec3 vel = p.vel();
    const u32 state = static_cast<u32>(p.state());
    checksum_bytes(hash, &pos, sizeof(pos));
    checksum_bytes(hash, &vel, sizeof(vel));
    checksum_bytes(hash, &state, sizeof(state));
}

void car_run(PhysWorld& world, Vehicle& veh, const VehicleInput& input, u32 ticks)
{
    for (u32 i = 0; i < ticks; i++) {
        veh.set_input(input);
        veh.tick(world, kDt);
        world.tick(kDt);
    }
}

void walk_run(PhysWorld& world, Player& p, const PlayerCommand& cmd, u32 ticks)
{
    for (u32 i = 0; i < ticks; i++) {
        world.tick(kDt);
        p.tick(world, nullptr, cmd, kDt);
    }
}

void add_quad(PhysWorld& world, Vec3 a, Vec3 b, Vec3 c, Vec3 d)
{
    world.add_static_tri(a, b, c);
    world.add_static_tri(a, c, d);
}

struct PhysScene {
    Arena arena{megabytes(64)};
    Heightfield hf;
    PhysWorld world;

    u64 checksum = 0;
    f32 min_y = 1e30f;
    f32 max_speed = 0.0f;
    u32 settled = 0;
    u32 bodies = 0;
    bool valid = true;

    void run()
    {
        hf.init_procedural(arena, 96, 1.0f, 1234u, 1.0f);
        world.init(arena, &hf);

        Rng rng(1234u);
        for (i32 i = 0; i < 24; i++) {
            const Vec3 pos{rng.range(-14.0f, 14.0f), rng.range(8.0f, 18.0f),
                           rng.range(-14.0f, 14.0f)};
            const Vec3 half{rng.range(0.25f, 0.8f), rng.range(0.25f, 0.8f),
                            rng.range(0.25f, 0.8f)};
            const Quat rot = quat_from_euler(rng.range(0.0f, kTau), rng.range(0.0f, kTau),
                                             rng.range(0.0f, kTau));
            world.body_create_box(pos, rot, half, 20.0f);
        }

        for (u32 tick = 0; tick < 1440; tick++) {
            world.tick(kDt);
        }

        const Pool<RigidBody>& pool = world.bodies();
        for (u32 idx : pool.live_indices()) {
            const RigidBody* body = pool.at(idx);
            bodies++;
            if (!body_state_valid(*body) || body->pos.y < hf.min_height() - 2.0f) {
                valid = false;
                continue;
            }
            const f32 speed = length(body->vel);
            min_y = f_min(min_y, body->pos.y);
            max_speed = f_max(max_speed, speed);
            settled += speed < 0.5f ? 1 : 0;
        }

        checksum = kFnvOffset;
        checksum_bodies(checksum, world);
    }
};

struct CarRig {
    Arena arena{megabytes(64)};
    Heightfield hf;
    PhysWorld world;
    Vehicle veh;

    bool flat(f32 size, f32 cell)
    {
        hf.init_procedural(arena, static_cast<u32>(size), cell, 1u, 0.0f);
        world.init(arena, &hf);
        return veh.init(world, arena, kCarCfg, Vec3{-150.0f, 0.9f, 0.0f}, -kPi * 0.5f);
    }

    bool slope(f32 grade, f32 spawn_x)
    {
        hf.init_slope(arena, 120, 1.0f, grade);
        world.init(arena, &hf);
        return veh.init(world, arena, kCarCfg,
                        Vec3{spawn_x, spawn_x * grade + 0.9f, 0.0f}, -kPi * 0.5f);
    }

    const RigidBody& body() const { return *world.body(veh.body()); }
};

} // namespace

TEST(scene_phys, twenty_four_bodies_settle_without_escaping)
{
    PhysScene scene;
    scene.run();

    CHECK(scene.bodies == 24);
    CHECK(scene.valid);
    CHECK(scene.max_speed < 2.0f);
    CHECK(scene.settled >= 22);
    CHECK(scene.min_y > scene.hf.min_height() - 2.0f);
}

TEST(scene_phys, a_long_run_is_bit_identical_across_repeats)
{
    PhysScene a;
    PhysScene b;
    a.run();
    b.run();

    CHECK(a.checksum == b.checksum);
    CHECK(a.bodies == b.bodies);
    CHECK(a.settled == b.settled);
}

TEST(scene_car, full_throttle_reaches_a_plausible_speed_without_drifting)
{
    CarRig rig;
    CHECK(rig.flat(220.0f, 2.0f));

    car_run(rig.world, rig.veh, VehicleInput{}, 240);
    const f32 z_start = rig.body().pos.z;

    VehicleInput throttle;
    throttle.throttle = 1.0f;
    car_run(rig.world, rig.veh, throttle, 720);

    const f32 six_second_speed = rig.veh.forward_speed(rig.world);
    CHECK(six_second_speed >= 14.0f);
    CHECK(six_second_speed <= 45.0f);

    car_run(rig.world, rig.veh, throttle, 720);
    const f32 speed = rig.veh.forward_speed(rig.world);
    const f32 drift = f_abs(rig.body().pos.z - z_start);
    CHECK(body_state_valid(rig.body()));
    CHECK(speed >= 25.0f);
    CHECK(drift < 2.0f);
}

TEST(scene_car, braking_decelerates_between_six_and_twelve_metres_per_second_squared)
{
    CarRig rig;
    CHECK(rig.flat(220.0f, 2.0f));

    car_run(rig.world, rig.veh, VehicleInput{}, 240);
    VehicleInput throttle;
    throttle.throttle = 1.0f;
    car_run(rig.world, rig.veh, throttle, 720);

    const f32 entry_speed = rig.veh.forward_speed(rig.world);
    const f32 x_start = rig.body().pos.x;

    VehicleInput brake;
    brake.brake = 1.0f;
    bool stopped = false;
    f32 min_forward = 1e30f;
    for (u32 i = 0; i < 960; i++) {
        rig.veh.set_input(brake);
        rig.veh.tick(rig.world, kDt);
        rig.world.tick(kDt);
        const f32 fwd = rig.veh.forward_speed(rig.world);
        min_forward = f_min(min_forward, fwd);
        if (fwd < 0.15f) {
            stopped = true;
            break;
        }
    }

    const f32 stop_dist = f_abs(rig.body().pos.x - x_start);
    const f32 avg_decel = entry_speed * entry_speed / f_max(2.0f * stop_dist, 0.1f);
    CHECK(stopped);
    CHECK(body_state_valid(rig.body()));
    CHECK(avg_decel >= 6.0f);
    CHECK(avg_decel <= 12.0f);
    CHECK(min_forward > -0.5f);
}

TEST(scene_car, a_steady_turn_holds_its_yaw_rate_and_stays_on_its_wheels)
{
    CarRig rig;
    CHECK(rig.flat(220.0f, 2.0f));

    car_run(rig.world, rig.veh, VehicleInput{}, 240);
    VehicleInput throttle;
    throttle.throttle = 1.0f;
    car_run(rig.world, rig.veh, throttle, 420);

    VehicleInput turn;
    turn.throttle = 0.25f;
    turn.steer = 0.5f;
    f32 yaw_accum = 0.0f;
    f32 min_up = 1.0f;
    for (u32 i = 0; i < 480; i++) {
        rig.veh.set_input(turn);
        rig.veh.tick(rig.world, kDt);
        rig.world.tick(kDt);
        if (i >= 360) {
            yaw_accum += f_abs(rig.body().angular_vel.y);
        }
        min_up = f_min(min_up, rotate(rig.body().rot, Vec3{0.0f, 1.0f, 0.0f}).y);
    }

    const f32 avg_yaw = yaw_accum / 120.0f;
    CHECK(body_state_valid(rig.body()));
    CHECK(avg_yaw >= 0.2f);
    CHECK(avg_yaw <= 1.6f);
    CHECK(min_up > 0.906f);
    CHECK(f_abs(rig.veh.forward_speed(rig.world)) > 4.0f);
}

TEST(scene_car, the_handbrake_holds_it_on_a_fifteen_degree_slope)
{
    CarRig rig;
    CHECK(rig.slope(0.28f, 20.0f));

    VehicleInput hold;
    hold.brake = 1.0f;
    hold.handbrake = true;
    car_run(rig.world, rig.veh, hold, 240);
    const Vec3 parked = rig.body().pos;

    VehicleInput handbrake;
    handbrake.handbrake = true;
    car_run(rig.world, rig.veh, handbrake, 600);

    CHECK(body_state_valid(rig.body()));
    CHECK(distance(rig.body().pos, parked) < 0.15f);
}

TEST(scene_car, the_scripted_run_is_bit_identical_across_repeats)
{
    u64 checksums[2] = {kFnvOffset, kFnvOffset};
    for (u32 pass = 0; pass < 2; pass++) {
        CarRig rig;
        CHECK(rig.flat(220.0f, 2.0f));
        car_run(rig.world, rig.veh, VehicleInput{}, 240);
        VehicleInput throttle;
        throttle.throttle = 1.0f;
        car_run(rig.world, rig.veh, throttle, 480);
        VehicleInput turn;
        turn.throttle = 0.25f;
        turn.steer = 0.5f;
        car_run(rig.world, rig.veh, turn, 300);
        checksum_car(checksums[pass], rig.world, rig.veh);
    }
    CHECK(checksums[0] == checksums[1]);
}

TEST(scene_zone, the_testzone_loads_and_answers_raycasts_the_same_way_twice)
{
    u64 checksums[2] = {kFnvOffset, kFnvOffset};
    u32 hits[2] = {};
    u32 tris[2] = {};

    for (u32 pass = 0; pass < 2; pass++) {
        Arena perm(megabytes(256));
        Arena scratch(megabytes(64));
        World world;
        Terrain terrain;
        PhysWorld phys;
        ZoneSpawn spawn;

        world.init(perm);
        phys.init(perm, &terrain.heightfield());
        CHECK(zone_load(kZoneDir, perm, scratch, world, phys, terrain, spawn, nullptr));
        tris[pass] = phys.statics().tri_count();

        Rng rng(99u);
        for (u32 i = 0; i < 300; i++) {
            Ray ray;
            ray.origin = Vec3{rng.range(-360.0f, 360.0f), 80.0f, rng.range(-360.0f, 360.0f)};
            ray.dir = normalize(Vec3{rng.range(-0.3f, 0.3f), -1.0f, rng.range(-0.3f, 0.3f)});
            PhysRayHit hit;
            if (phys.raycast(ray, 300.0f, &hit)) {
                hits[pass]++;
                checksum_bytes(checksums[pass], &hit.t, sizeof(hit.t));
                checksum_bytes(checksums[pass], &hit.normal, sizeof(hit.normal));
            }
        }
    }

    CHECK(tris[0] > 0);
    CHECK(tris[0] == tris[1]);
    CHECK(hits[0] > 200);
    CHECK(hits[0] == hits[1]);
    CHECK(checksums[0] == checksums[1]);
}

TEST(scene_zone, the_car_spawns_in_the_zone_and_drives_away_from_its_spawn)
{
    Arena perm(megabytes(256));
    Arena scratch(megabytes(64));
    World world;
    Terrain terrain;
    PhysWorld phys;
    ZoneSpawn spawn;

    world.init(perm);
    phys.init(perm, &terrain.heightfield());
    CHECK(zone_load(kZoneDir, perm, scratch, world, phys, terrain, spawn, nullptr));

    Vehicle veh;
    CHECK(veh.init(phys, scratch, kCarCfg, spawn.car_pos, spawn.car_yaw));

    car_run(phys, veh, VehicleInput{}, 360);
    VehicleInput drive;
    drive.throttle = 0.6f;
    car_run(phys, veh, drive, 480);

    const RigidBody& body = *phys.body(veh.body());
    CHECK(body_state_valid(body));
    CHECK(body.pos.y > terrain.heightfield().min_height() - 2.0f);
    CHECK(distance(body.pos, spawn.car_pos) > 3.0f);
}

TEST(scene_walk, two_seconds_of_walking_covers_seven_to_nine_metres)
{
    Arena arena{megabytes(32)};
    Heightfield hf;
    PhysWorld world;
    hf.init_procedural(arena, 96, 1.0f, 5u, 0.0f);
    world.init(arena, &hf);

    Player p;
    p.init(Vec3{0.0f, 0.0f, 10.0f}, 0.0f);

    PlayerCommand forward;
    forward.move_z = 1.0f;
    walk_run(world, p, forward, 240);

    const f32 walked = 10.0f - p.pos().z;
    CHECK(walked >= 7.0f);
    CHECK(walked <= 8.8f);
    CHECK(p.grounded());
    CHECK(f_abs(p.pos().y) < 0.2f);
}

TEST(scene_walk, a_jump_clears_a_third_of_a_metre_and_lands_grounded)
{
    Arena arena{megabytes(32)};
    Heightfield hf;
    PhysWorld world;
    hf.init_procedural(arena, 96, 1.0f, 5u, 0.0f);
    world.init(arena, &hf);

    Player p;
    p.init(Vec3{0.0f, 0.0f, 10.0f}, 0.0f);
    walk_run(world, p, PlayerCommand{}, 60);

    PlayerCommand jump;
    jump.jump = true;
    walk_run(world, p, jump, 30);
    const f32 peak = p.pos().y;

    walk_run(world, p, PlayerCommand{}, 150);
    CHECK(peak > 0.35f);
    CHECK(p.grounded());
    CHECK(f_abs(p.pos().y) < 0.1f);
}

TEST(scene_walk, a_wall_stops_the_capsule_without_letting_it_through)
{
    Arena arena{megabytes(32)};
    Heightfield hf;
    PhysWorld world;
    hf.init_procedural(arena, 96, 1.0f, 5u, 0.0f);
    world.init(arena, &hf);
    world.statics_reserve(arena, 64);
    add_quad(world, Vec3{-6.0f, 0.0f, -6.0f}, Vec3{6.0f, 0.0f, -6.0f},
             Vec3{6.0f, 3.0f, -6.0f}, Vec3{-6.0f, 3.0f, -6.0f});
    world.statics_build(arena);

    Player p;
    p.init(Vec3{0.0f, 0.0f, 10.0f}, 0.0f);
    PlayerCommand forward;
    forward.move_z = 1.0f;
    walk_run(world, p, forward, 840);

    CHECK(p.pos().z > -6.01f);
    CHECK(p.pos().z < -5.2f);
    CHECK(f_abs(p.pos().x) < 0.5f);
}

TEST(scene_walk, a_quarter_metre_step_is_climbed_and_stepped_back_off)
{
    Arena arena{megabytes(32)};
    Heightfield hf;
    PhysWorld world;
    hf.init_procedural(arena, 96, 1.0f, 5u, 0.0f);
    world.init(arena, &hf);
    world.statics_reserve(arena, 64);
    add_quad(world, Vec3{-4.0f, 0.25f, -4.0f}, Vec3{4.0f, 0.25f, -4.0f},
             Vec3{4.0f, 0.25f, -14.0f}, Vec3{-4.0f, 0.25f, -14.0f});
    world.statics_build(arena);

    Player p;
    p.init(Vec3{0.0f, 0.0f, 0.0f}, 0.0f);
    PlayerCommand forward;
    forward.move_z = 1.0f;
    walk_run(world, p, forward, 300);

    const f32 on_step_y = p.pos().y;
    const f32 on_step_z = p.pos().z;
    walk_run(world, p, forward, 180);

    CHECK(on_step_y >= 0.2f);
    CHECK(on_step_y <= 0.35f);
    CHECK(on_step_z < -6.0f);
    CHECK(p.pos().z < -14.5f);
    CHECK(f_abs(p.pos().y) < 0.1f);
}

TEST(scene_walk, the_scripted_walk_is_bit_identical_across_repeats)
{
    u64 checksums[2] = {kFnvOffset, kFnvOffset};
    for (u32 pass = 0; pass < 2; pass++) {
        Arena arena{megabytes(32)};
        Heightfield hf;
        PhysWorld world;
        hf.init_procedural(arena, 96, 1.0f, 5u, 0.0f);
        world.init(arena, &hf);

        Player p;
        p.init(Vec3{0.0f, 0.0f, 10.0f}, 0.0f);
        PlayerCommand forward;
        forward.move_z = 1.0f;
        walk_run(world, p, forward, 240);
        PlayerCommand jump;
        jump.jump = true;
        walk_run(world, p, jump, 30);
        walk_run(world, p, PlayerCommand{}, 150);
        checksum_player(checksums[pass], p);
    }
    CHECK(checksums[0] == checksums[1]);
}
