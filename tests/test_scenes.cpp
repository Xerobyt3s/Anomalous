#include "test.h"

#include "core/arena.h"
#include "core/log.h"
#include "core/rng.h"
#include "physics/body.h"
#include "physics/gravity_field.h"
#include "physics/heightfield.h"
#include "physics/jolt_world.h"
#include "physics/world.h"
#include "player/interact.h"
#include "player/player.h"
#include "vehicle/vehicle.h"
#include "world/entity.h"
#include "world/terrain.h"
#include "world/islands/island_field.h"
#include "world/zone.h"

#include <cmath>
#include <vector>

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
    JoltWorld jolt;
    hf.init_procedural(arena, 96, 1.0f, 5u, 0.0f);
    world.init(arena, &hf);
    world.set_jolt(&jolt);

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
    JoltWorld jolt;
    hf.init_procedural(arena, 96, 1.0f, 5u, 0.0f);
    world.init(arena, &hf);
    world.set_jolt(&jolt);

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
    JoltWorld jolt;
    hf.init_procedural(arena, 96, 1.0f, 5u, 0.0f);
    world.init(arena, &hf);
    world.set_jolt(&jolt);
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
    JoltWorld jolt;
    hf.init_procedural(arena, 96, 1.0f, 5u, 0.0f);
    world.init(arena, &hf);
    world.set_jolt(&jolt);
    world.statics_reserve(arena, 64);
    add_quad(world, Vec3{-4.0f, 0.25f, -4.0f}, Vec3{4.0f, 0.25f, -4.0f},
             Vec3{4.0f, 0.25f, -14.0f}, Vec3{-4.0f, 0.25f, -14.0f});
    add_quad(world, Vec3{-4.0f, -0.1f, -4.0f}, Vec3{4.0f, -0.1f, -4.0f},
             Vec3{4.0f, 0.25f, -4.0f}, Vec3{-4.0f, 0.25f, -4.0f});
    add_quad(world, Vec3{-4.0f, -0.1f, -14.0f}, Vec3{4.0f, -0.1f, -14.0f},
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
        JoltWorld jolt;
        hf.init_procedural(arena, 96, 1.0f, 5u, 0.0f);
        world.init(arena, &hf);
        world.set_jolt(&jolt);

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

namespace {

struct ZoneSwapRig {
    Arena perm{megabytes(32)};
    Arena zone{megabytes(64)};
    Arena scratch{megabytes(64)};
    World world;
    PhysWorld phys;
    Terrain terrain;
    ZoneSpawn spawn;
    ZonePickups pickups;

    void setup()
    {
        world.init(perm);
        phys.init(perm, &terrain.heightfield());
    }

    bool swap(const char* dir)
    {
        for (u32 idx : world.entities().live_indices()) {
            Entity* e = world.entities().at(idx);
            if (e && phys.body(e->body)) {
                phys.body_destroy(e->body);
                e->body = BodyHandle{};
            }
        }
        world.clear();
        phys.statics_clear();
        zone.reset();
        if (!zone_load(dir, zone, scratch, world, phys, terrain, spawn, &pickups)) {
            return false;
        }
        interact_spawn_zone_pickups(world, phys, terrain, pickups);
        return true;
    }
};

} // namespace

TEST(travel_zone, swapping_zones_returns_the_arena_to_where_it_started)
{
    ZoneSwapRig rig;
    rig.setup();

    CHECK(rig.swap("assets/zones/testzone"));
    const u64 first = rig.zone.used();
    const f32 first_span = rig.terrain.heightfield().span_x();

    CHECK(rig.swap("assets/zones/touge"));
    const u64 other = rig.zone.used();
    CHECK(rig.terrain.heightfield().span_x() > first_span);

    CHECK(rig.swap("assets/zones/testzone"));
    CHECK(rig.zone.used() == first);
    CHECK(other != first);
    CHECK_NEAR(rig.terrain.heightfield().span_x(), first_span, 1e-3);
}

TEST(travel_zone, swapping_zones_hands_back_every_pickup_body)
{
    ZoneSwapRig rig;
    rig.setup();

    CHECK(rig.swap("assets/zones/testzone"));
    const u32 settled = rig.phys.bodies().count();
    CHECK(settled > 0);

    for (i32 i = 0; i < 6; i++) {
        CHECK(rig.swap(i % 2 == 0 ? "assets/zones/touge" : "assets/zones/testzone"));
    }
    CHECK(rig.swap("assets/zones/testzone"));
    CHECK(rig.phys.bodies().count() == settled);
}

TEST(travel_zone, the_arriving_terrain_is_the_one_that_was_asked_for)
{
    ZoneSwapRig rig;
    rig.setup();

    CHECK(rig.swap("assets/zones/touge"));
    const Vec3 car = rig.spawn.car_pos;
    const f32 ground = rig.terrain.heightfield().sample(car.x, car.z);

    // The spawn has to be standing on the new heightfield, not the one it replaced.
    CHECK(f_abs(car.y - ground) < 2.0f);
    CHECK(contains(rig.terrain.heightfield().bounds(), Vec3{car.x, ground + 0.1f, car.z}));
}

namespace {

struct GravityZoneRig {
    Arena perm{megabytes(256)};
    Arena scratch{megabytes(64)};
    Arena statics{megabytes(48)};
    World world;
    Terrain terrain;
    PhysWorld phys;
    ZoneSpawn spawn;
    GravityField field;
    IslandField islands;

    bool load()
    {
        world.init(perm);
        phys.init(perm, &terrain.heightfield());
        if (!zone_load(kZoneDir, perm, scratch, world, phys, terrain, spawn, nullptr)) {
            return false;
        }
        islands_rebuild(islands, world, terrain, phys, statics, scratch);
        zone_gravity(world, field);
        islands.fill_gravity(field);
        phys.set_gravity_field(&field);
        return true;
    }

    const BuiltIsland* island(std::string_view name) const
    {
        for (const BuiltIsland& i : islands.islands()) {
            if (i.name == name) {
                return &i;
            }
        }
        return nullptr;
    }

    const BuiltLink* link(std::string_view from, std::string_view to) const
    {
        for (const BuiltLink& l : islands.links()) {
            const bool from_match = islands.islands()[l.from].name == from;
            const bool to_match = to == "ground" ? l.to == kLinkGround
                                                 : (l.to >= 0 && islands.islands()[static_cast<u32>(l.to)].name == to);
            if (from_match && to_match) {
                return &l;
            }
        }
        return nullptr;
    }

    bool place_car(Vehicle& veh, Vec3 at, Vec3 dir)
    {
        const Vec3 ground{at.x, terrain.heightfield().sample(at.x, at.z) + 1.0f, at.z};
        if (!veh.init(phys, scratch, kCarCfg, ground, std::atan2(-dir.x, -dir.z))) {
            return false;
        }
        car_run(phys, veh, VehicleInput{}, 120);
        return true;
    }

    bool place_car_before_ground_link(Vehicle& veh, const BuiltLink& l, f32 run_up)
    {
        const Vec3 a = l.path.points[0];
        const Vec3 b = l.path.points[1];
        Vec3 dir{b.x - a.x, 0.0f, b.z - a.z};
        dir = normalize(dir);
        return place_car(veh, a - dir * run_up, dir);
    }
};

struct RouteDriver {
    std::vector<Vec3> points;
    u32 progress = 0;

    void follow(const BuiltLink& l)
    {
        for (u32 i = 0; i < l.path.count; i++) {
            points.push_back(l.path.points[i]);
        }
    }

    VehicleInput input(const RigidBody& body, f32 throttle)
    {
        while (progress + 1 < points.size() && distance_sq(body.pos, points[progress]) > distance_sq(body.pos, points[progress + 1])) {
            progress++;
        }
        u32 target = progress;
        while (target + 1 < points.size() && length(points[target] - body.pos) < kRouteLookahead) {
            target++;
        }
        const Vec3 d = points[target] - body.pos;
        const f32 ahead = dot(d, rotate(body.rot, Vec3{0.0f, 0.0f, -1.0f}));
        const f32 side = dot(d, rotate(body.rot, Vec3{1.0f, 0.0f, 0.0f}));
        VehicleInput in;
        const f32 speed = dot(body.vel, rotate(body.rot, Vec3{0.0f, 0.0f, -1.0f}));
        in.throttle = speed < kRouteMaxSpeed ? throttle : 0.0f;
        in.brake = speed > kRouteMaxSpeed + 1.5f ? 0.3f : 0.0f;
        in.steer = f_clamp(std::atan2(side, f_max(ahead, 0.1f)) * kRouteSteerGain, -1.0f, 1.0f);
        return in;
    }

    static constexpr f32 kRouteLookahead = 7.0f;
    static constexpr f32 kRouteSteerGain = 2.5f;
    static constexpr f32 kRouteMaxSpeed = 11.0f;
};

struct IslandTrack {
    const BuiltIsland* island = nullptr;
    f32 best = -1.0f;

    void sample(const RigidBody& body)
    {
        if (!island) {
            return;
        }
        const Vec3 local = rotate(conjugate(island->rot), body.pos - island->pos);
        const f32 reach = island->params.radius * 0.7f;
        if (f_abs(local.x) < reach && f_abs(local.z) < reach && local.y > -0.5f && local.y < 3.0f) {
            best = f_max(best, dot(rotate(body.rot, Vec3{0.0f, 1.0f, 0.0f}),
                                   rotate(island->rot, Vec3{0.0f, 1.0f, 0.0f})));
        }
    }
};

} // namespace

TEST(scene_gravity, the_car_climbs_a_sideways_island_and_drives_onto_an_upside_down_one)
{
    GravityZoneRig rig;
    CHECK(rig.load());
    const BuiltLink* start = rig.link("isle_g", "ground");
    const BuiltLink* next = rig.link("isle_g", "isle_c");
    CHECK(start != nullptr && next != nullptr);
    if (!start || !next) {
        return;
    }
    Vehicle veh;
    CHECK(rig.place_car_before_ground_link(veh, *start, 40.0f));
    RouteDriver route;
    route.follow(*start);
    route.follow(*next);
    route.points.push_back(rig.island("isle_c")->pos);
    IslandTrack wall{rig.island("isle_g")};
    IslandTrack ceiling{rig.island("isle_c")};
    bool valid = true;
    for (u32 i = 0; i < 2400; i++) {
        const RigidBody& body = *rig.phys.body(veh.body());
        car_run(rig.phys, veh, route.input(body, 0.8f), 1);
        valid = valid && body_state_valid(body);
        wall.sample(body);
        ceiling.sample(body);
    }
    CHECK(valid);
    CHECK(wall.best > 0.85f);
    CHECK(ceiling.best > 0.85f);
}

TEST(scene_gravity, the_ground_link_carries_the_car_onto_the_tilted_island_and_on_to_the_next)
{
    for (f32 throttle : {0.75f, 1.0f}) {
        GravityZoneRig rig;
        CHECK(rig.load());
        const BuiltLink* start = rig.link("isle_a", "ground");
        const BuiltLink* next = rig.link("isle_a", "isle_b");
        CHECK(start != nullptr && next != nullptr);
        if (!start || !next) {
            return;
        }
        Vehicle veh;
        CHECK(rig.place_car_before_ground_link(veh, *start, 12.0f));
        RouteDriver route;
        route.follow(*start);
        route.follow(*next);
        route.points.push_back(rig.island("isle_b")->pos);
        IslandTrack a{rig.island("isle_a")};
        IslandTrack b{rig.island("isle_b")};
        for (u32 i = 0; i < 1800; i++) {
            const RigidBody& body = *rig.phys.body(veh.body());
            car_run(rig.phys, veh, route.input(body, throttle), 1);
            a.sample(body);
            b.sample(body);
        }
        CHECK(a.best > 0.95f);
        CHECK(b.best > 0.9f);
    }
}

TEST(scene_gravity, recovering_rights_the_car_to_the_local_gravity)
{
    Arena arena{megabytes(64)};
    Heightfield hf;
    hf.init_procedural(arena, 64, 1.0f, 3u, 0.0f);
    PhysWorld phys;
    phys.init(arena, &hf);
    GravityField field;
    GravityVolume wall;
    wall.pos = Vec3{0.0f, 20.0f, 0.0f};
    wall.rot = quat_from_euler(0.0f, 0.0f, 90.0f * kDegToRad);
    wall.half = Vec3{15.0f, 15.0f, 15.0f};
    field.add(wall);
    phys.set_gravity_field(&field);

    Vehicle veh;
    CHECK(veh.init(phys, arena, kCarCfg, Vec3{0.0f, 20.0f, 0.0f}, 0.0f));
    veh.teleport(phys, Vec3{0.0f, 20.0f, 0.0f}, quat_from_euler(0.3f, 0.0f, kPi));
    veh.recover(phys);
    const RigidBody& body = *phys.body(veh.body());
    const Vec3 up = rotate(body.rot, Vec3{0.0f, 1.0f, 0.0f});
    CHECK_NEAR(up.x, -1.0f, 1e-3);
    CHECK(f_abs(dot(rotate(body.rot, Vec3{0.0f, 0.0f, -1.0f}), up)) < 1e-3f);
}

TEST(scene_gravity, gravity_drives_are_bit_identical_across_repeats)
{
    u64 checksums[2] = {kFnvOffset, kFnvOffset};
    u64 fields[2] = {0, 0};
    for (u32 pass = 0; pass < 2; pass++) {
        GravityZoneRig rig;
        CHECK(rig.load());
        fields[pass] = rig.islands.checksum();
        const BuiltLink* start = rig.link("isle_a", "ground");
        CHECK(start != nullptr);
        if (!start) {
            return;
        }
        Vehicle veh;
        CHECK(rig.place_car_before_ground_link(veh, *start, 12.0f));
        VehicleInput drive;
        drive.throttle = 1.0f;
        car_run(rig.phys, veh, drive, 700);
        checksum_bodies(checksums[pass], rig.phys);
    }
    CHECK(fields[0] == fields[1]);
    CHECK(checksums[0] == checksums[1]);
}

TEST(scene_gravity, link_paths_turn_smoothly_and_meet_their_ends_flush)
{
    GravityZoneRig rig;
    CHECK(rig.load());
    CHECK(rig.islands.links().size() >= 8);
    for (const BuiltLink& l : rig.islands.links()) {
        const GravityPath& p = l.path;
        for (u32 i = 0; i + 1 < p.count; i++) {
            CHECK(dot(p.ups[i], p.ups[i + 1]) > std::cos(10.0f * kDegToRad));
        }
        const BuiltIsland& from = rig.islands.islands()[l.from];
        const Vec3 from_up = rotate(from.rot, Vec3{0.0f, 1.0f, 0.0f});
        const Vec3 end_up = p.ups[p.count - 1];
        const Vec3 start_up = p.ups[0];
        if (l.to == kLinkGround) {
            CHECK(dot(start_up, Vec3{0.0f, 1.0f, 0.0f}) > 0.98f);
            CHECK(dot(end_up, from_up) > 0.98f);
        } else {
            const BuiltIsland& to = rig.islands.islands()[static_cast<u32>(l.to)];
            CHECK(dot(start_up, from_up) > 0.98f);
            CHECK(dot(end_up, rotate(to.rot, Vec3{0.0f, 1.0f, 0.0f})) > 0.98f);
        }
        const GravitySample mid = rig.field.sample(p.points[p.count / 2] + p.ups[p.count / 2] * 1.0f);
        CHECK(dot(mid.up, p.ups[p.count / 2]) > 0.98f);
    }
}

TEST(scene_gravity, the_island_field_builds_inside_its_budget)
{
    GravityZoneRig rig;
    CHECK(rig.load());
    CHECK(rig.islands.islands().size() >= 6);
    CHECK(rig.islands.debris().size() > 400);
    CHECK(rig.islands.haze_count() >= 1);
    CHECK(rig.phys.statics().dropped() == 0);
    CHECK(rig.islands.build_ms() < 1500.0);
    CHECK(rig.islands.rejected().empty());
}

TEST(scene_gravity, solid_debris_stops_a_ray_where_the_rock_is)
{
    GravityZoneRig rig;
    CHECK(rig.load());
    u32 tested = 0;
    for (const DebrisInstance& d : rig.islands.debris()) {
        if (!d.solid || d.kind != DebrisKind::Rock || d.scale < 0.4f
            || d.pos.y < rig.terrain.heightfield().sample(d.pos.x, d.pos.z) + 0.5f) {
            continue;
        }
        const f32 drop = d.scale * 1.4f + 0.3f;
        PhysRayHit hit{};
        Ray ray;
        ray.origin = d.pos + Vec3{0.0f, drop, 0.0f};
        ray.dir = Vec3{0.0f, -1.0f, 0.0f};
        CHECK(rig.phys.raycast(ray, drop + 2.0f, &hit));
        CHECK(hit.t < drop);
        tested++;
    }
    CHECK(tested > 10);
}

TEST(scene_walk, walking_and_running_over_the_zone_stay_grounded_and_stop_cleanly)
{
    GravityZoneRig rig;
    CHECK(rig.load());
    JoltWorld jolt;
    rig.phys.set_jolt(&jolt);
    const f32 headings[6] = {0.0f, 1.0f, 2.1f, 3.14f, 4.2f, 5.3f};
    for (u32 h = 0; h < 6; h++) {
        Player p;
        p.init(rig.spawn.player_pos + Vec3{0.0f, 0.3f, 0.0f}, headings[h]);
        PlayerCommand idle;
        for (i32 i = 0; i < 60; i++) {
            p.tick(rig.phys, nullptr, idle, kDt);
        }
        PlayerCommand fwd;
        fwd.move_z = 1.0f;
        fwd.run = h >= 3;
        u32 air = 0;
        f32 max_step = 0.0f;
        for (i32 i = 0; i < 1200; i++) {
            const Vec3 before = p.pos();
            p.tick(rig.phys, nullptr, fwd, kDt);
            air += p.grounded() ? 0 : 1;
            max_step = f_max(max_step, length(p.pos() - before));
        }
        const Vec3 released = p.pos();
        for (i32 i = 0; i < 120; i++) {
            p.tick(rig.phys, nullptr, idle, kDt);
        }
        CHECK(air < 12);
        CHECK(max_step < 0.16f);
        CHECK(distance(released, p.pos()) < 0.9f);
    }
}

TEST(scene_walk, walking_the_links_reaches_a_sideways_island_and_an_upside_down_one)
{
    GravityZoneRig rig;
    CHECK(rig.load());
    JoltWorld jolt;
    rig.phys.set_jolt(&jolt);
    const char* routes[2][2] = {{"isle_a", "isle_b"}, {"isle_g", "isle_c"}};
    for (u32 r = 0; r < 2; r++) {
        const BuiltLink* start = rig.link(routes[r][0], "ground");
        const BuiltLink* next = rig.link(routes[r][0], routes[r][1]);
        const BuiltIsland* goal = rig.island(routes[r][1]);
        CHECK(start != nullptr && next != nullptr && goal != nullptr);
        if (!start || !next || !goal) {
            return;
        }
        const Vec3 goal_up = rotate(goal->rot, Vec3{0.0f, 1.0f, 0.0f});
        RouteDriver route;
        route.follow(*start);
        route.follow(*next);
        route.points.push_back(goal->pos + goal_up * 1.0f);
        Player p;
        const Vec3 foot = start->path.points[0];
        p.init(Vec3{foot.x, rig.terrain.heightfield().sample(foot.x, foot.z) + 0.3f, foot.z}, 0.0f);
        u32 air = 0;
        for (u32 i = 0; i < 4800; i++) {
            const Vec3 to = route.points[route.progress + 1 < route.points.size() ? route.progress + 1 : route.progress] - p.pos();
            const Vec3 forward = frame_forward(p.movement().state().frame, p.yaw());
            const Vec3 right = frame_right(p.movement().state().frame, p.yaw());
            PlayerCommand cmd;
            if (distance(p.pos(), route.points.back()) > 2.0f) {
                const Vec3 planar = to - p.up() * dot(to, p.up());
                const Vec3 dir = length_sq(planar) > 1e-6f ? normalize(planar) : forward;
                cmd.move_z = dot(dir, forward);
                cmd.move_x = dot(dir, right);
                cmd.run = true;
            }
            while (route.progress + 1 < route.points.size()
                   && distance_sq(p.pos(), route.points[route.progress]) > distance_sq(p.pos(), route.points[route.progress + 1])) {
                route.progress++;
            }
            rig.phys.tick(kDt);
            p.tick(rig.phys, nullptr, cmd, kDt);
            air += p.grounded() ? 0 : 1;
        }
        CHECK(p.grounded());
        CHECK(dot(p.up(), goal_up) > 0.97f);
        CHECK(distance(p.pos(), route.points.back()) < 4.0f);
        CHECK(air < 60);
    }
}

TEST(scene_gravity, every_link_bends_gently_and_turns_gravity_slowly)
{
    GravityZoneRig rig;
    CHECK(rig.load());
    for (const BuiltLink& l : rig.islands.links()) {
        const GravityPath& p = l.path;
        f32 tight = 1e9f;
        f32 rate = 0.0f;
        f32 total = 0.0f;
        for (u32 i = 0; i + 1 < p.count; i++) {
            const f32 seg = length(p.points[i + 1] - p.points[i]);
            total += seg;
            rate = f_max(rate, std::acos(f_clamp(dot(p.ups[i], p.ups[i + 1]), -1.0f, 1.0f)) * kRadToDeg / seg);
            if (i > 0) {
                const Vec3 a = normalize(p.points[i] - p.points[i - 1]);
                const Vec3 b = normalize(p.points[i + 1] - p.points[i]);
                const f32 bend = std::acos(f_clamp(dot(a, b), -1.0f, 1.0f));
                if (bend > 1e-4f) {
                    tight = f_min(tight, seg / bend);
                }
            }
        }
        CHECK(tight > 8.5f);
        CHECK(rate < 6.5f);
        CHECK(total < 4.0f * length(p.points[p.count - 1] - p.points[0]) + 20.0f);
    }
}



TEST(scene_gravity, no_link_or_low_rock_sits_on_the_road)
{
    GravityZoneRig rig;
    CHECK(rig.load());
    const Heightfield& hf = rig.terrain.heightfield();
    for (const BuiltLink& l : rig.islands.links()) {
        const GravityPath& p = l.path;
        for (u32 i = 0; i + 1 < p.count; i++) {
            const Vec3 at = p.points[i];
            if (at.y - hf.sample(at.x, at.z) > 14.0f) {
                continue;
            }
            const Vec3 side = normalize(cross(p.points[i + 1] - at, p.ups[i])) * (l.width * 0.5f);
            for (f32 k : {-1.0f, 0.0f, 1.0f}) {
                const Vec3 q = at + side * k;
                CHECK(rig.terrain.road_amount(q.x, q.z) <= 0.25f);
            }
        }
    }
    for (const DebrisInstance& d : rig.islands.debris()) {
        if (d.solid) {
            CHECK(rig.terrain.road_amount(d.pos.x, d.pos.z) <= 0.25f);
        }
    }
}

