#include "test.h"

#include "assets/mesh_data.h"
#include "core/arena.h"
#include "core/log.h"
#include "engine/physics/physics_world.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "vehicle/surface.h"
#include "vehicle/tire.h"
#include "vehicle/vehicle_config.h"
#include "vehicle/vehicle.h"

#include <cmath>
#include <cstring>

using namespace anom;

namespace {

constexpr f32 kDt = 1.0f / 120.0f;
constexpr const char* kCarPath = "assets/cars/excel.cfg";

Heightfield make_flat(Arena& arena, u32 size = 128, f32 cell = 2.0f)
{
    Heightfield hf;
    hf.alloc(arena, size, cell);
    for (u32 z = 0; z < size; z++) {
        for (u32 x = 0; x < size; x++) {
            hf.set_height(x, z, 0.0f);
        }
    }
    hf.recompute_extents();
    return hf;
}

Heightfield make_ramp(Arena& arena, u32 size, f32 cell, f32 start_z, f32 slope_deg)
{
    constexpr f32 kBlend = 4.0f;
    Heightfield hf;
    hf.alloc(arena, size, cell);
    const f32 rise = std::tan(slope_deg * kDegToRad);
    for (u32 z = 0; z < size; z++) {
        const f32 world_z = hf.origin().z + static_cast<f32>(z) * cell;
        const f32 d = f_max(start_z - world_z, 0.0f);
        const f32 h = d < kBlend ? rise * d * d / (2.0f * kBlend) : rise * (d - kBlend * 0.5f);
        for (u32 x = 0; x < size; x++) {
            hf.set_height(x, z, h);
        }
    }
    hf.recompute_extents();
    return hf;
}

struct Rig {
    Arena arena{megabytes(64)};
    Heightfield hf;
    PhysWorld world;
    ghost::engine::PhysicsWorld jolt;
    Vehicle car;
    const char* path = kCarPath;

    bool setup(u32 size = 128, f32 cell = 2.0f)
    {
        hf = make_flat(arena, size, cell);
        world.init(arena, &hf);
        world.set_jolt(&jolt);
        return car.init(world, arena, path, Vec3{0.0f, 1.0f, 0.0f}, 0.0f);
    }

    bool setup_ramp(f32 slope_deg)
    {
        hf = make_ramp(arena, 256, 0.5f, -8.0f, slope_deg);
        world.init(arena, &hf);
        world.set_jolt(&jolt);
        return car.init(world, arena, path, Vec3{0.0f, 1.0f, 0.0f}, 0.0f);
    }

    void step(f32 throttle, f32 brake, f32 steer, bool handbrake, i32 ticks)
    {
        VehicleInput in;
        in.throttle = throttle;
        in.brake = brake;
        in.steer = steer;
        in.handbrake = handbrake;
        for (i32 i = 0; i < ticks; i++) {
            car.set_input(in);
            car.tick(world, kDt);
            world.tick(kDt);
        }
    }

    const RigidBody& body() const { return *world.body(car.body()); }
};

} // namespace

TEST(vehicle, config_loads_from_disk)
{
    Arena arena(megabytes(8));
    VehicleConfig cfg;
    CHECK(vehicle_config_load(cfg, arena, kCarPath));
    CHECK(cfg.mass > 400.0f);
    CHECK(cfg.mass < 4000.0f);
    CHECK(cfg.gear_count >= 3);
    CHECK(cfg.torque_count >= 2);
    CHECK(cfg.max_rpm > cfg.idle_rpm);
}

TEST(vehicle, missing_config_fails_cleanly)
{
    Arena arena(megabytes(8));
    VehicleConfig cfg;
    CHECK(!vehicle_config_load(cfg, arena, "assets/cars/nope.cfg"));
}

TEST(vehicle, settles_on_its_suspension)
{
    Rig rig;
    CHECK(rig.setup());

    rig.step(0.0f, 0.0f, 0.0f, false, 480);

    const RigidBody& body = rig.body();
    CHECK(body_state_valid(body));
    CHECK(body.pos.y > 0.0f);
    CHECK(body.pos.y < 1.5f);
    CHECK(f_abs(body.vel.y) < 0.2f);

    u32 grounded = 0;
    for (u32 i = 0; i < kWheelCount; i++) {
        if (rig.car.wheel(i).grounded) {
            grounded++;
        }
        CHECK(rig.car.wheel(i).load >= 0.0f);
    }
    CHECK(grounded == 4);
}

TEST(vehicle, suspension_carries_roughly_its_weight)
{
    Rig rig;
    CHECK(rig.setup());
    rig.step(0.0f, 0.0f, 0.0f, false, 480);

    f32 total = 0.0f;
    for (u32 i = 0; i < kWheelCount; i++) {
        total += rig.car.wheel(i).load;
    }
    const f32 weight = rig.car.config().mass * 9.81f;
    CHECK(total > weight * 0.7f);
    CHECK(total < weight * 1.3f);
}

TEST(vehicle, accelerates_forward_under_throttle)
{
    Rig rig;
    CHECK(rig.setup());
    rig.step(0.0f, 0.0f, 0.0f, false, 240);

    rig.car.effects().ignition_ok = true;
    rig.step(1.0f, 0.0f, 0.0f, false, 720);

    const f32 speed = rig.car.forward_speed(rig.world);
    CHECK(speed > 4.0f);
    CHECK(body_state_valid(rig.body()));
}

TEST(vehicle, brakes_bring_it_to_a_stop)
{
    Rig rig;
    CHECK(rig.setup());
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    rig.step(1.0f, 0.0f, 0.0f, false, 720);
    CHECK(rig.car.forward_speed(rig.world) > 4.0f);

    rig.step(0.0f, 1.0f, 0.0f, false, 900);
    CHECK(f_abs(rig.car.forward_speed(rig.world)) < 0.5f);
}

TEST(vehicle, steering_turns_the_car)
{
    Rig rig;
    CHECK(rig.setup());
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    rig.step(1.0f, 0.0f, 0.0f, false, 480);

    const f32 yaw_before = quat_yaw(rig.body().rot);
    rig.step(0.6f, 0.0f, 1.0f, false, 600);
    const f32 yaw_after = quat_yaw(rig.body().rot);

    CHECK(f_abs(f_wrap_angle(yaw_after - yaw_before)) > 0.2f);
    CHECK(body_state_valid(rig.body()));
}

TEST(vehicle, steering_is_ackermann_biased)
{
    Rig rig;
    CHECK(rig.setup());
    rig.step(0.0f, 0.0f, 1.0f, false, 120);

    const f32 fl = rig.car.wheel(WHEEL_FL).steer_rad;
    const f32 fr = rig.car.wheel(WHEEL_FR).steer_rad;
    CHECK(f_abs(fr) > f_abs(fl));
    CHECK(rig.car.wheel(WHEEL_RL).steer_rad == 0.0f);
    CHECK(rig.car.wheel(WHEEL_RR).steer_rad == 0.0f);
}

TEST(vehicle, handbrake_holds_it_on_flat_ground)
{
    Rig rig;
    CHECK(rig.setup());
    rig.step(0.0f, 0.0f, 0.0f, true, 480);
    const Vec3 settled = rig.body().pos;

    rig.step(0.0f, 0.0f, 0.0f, true, 600);
    CHECK(distance(settled, rig.body().pos) < 0.2f);
}

TEST(vehicle, ignition_off_produces_no_drive)
{
    Rig rig;
    CHECK(rig.setup());
    rig.step(0.0f, 0.0f, 0.0f, false, 240);

    rig.car.effects().ignition_ok = false;
    rig.step(1.0f, 0.0f, 0.0f, false, 600);

    CHECK(f_abs(rig.car.forward_speed(rig.world)) < 0.5f);
}

TEST(vehicle, teleport_resets_motion)
{
    Rig rig;
    CHECK(rig.setup());
    rig.step(1.0f, 0.0f, 0.0f, false, 600);

    rig.car.teleport(rig.world, Vec3{20.0f, 2.0f, -5.0f}, 1.2f);
    const RigidBody& body = rig.body();
    CHECK_NEAR(body.pos.x, 20.0f, 1e-4);
    CHECK_NEAR(body.pos.z, -5.0f, 1e-4);
    CHECK(length(body.vel) == 0.0f);
    for (u32 i = 0; i < kWheelCount; i++) {
        CHECK(rig.car.wheel(i).omega == 0.0f);
    }
}

TEST(vehicle, drive_is_deterministic)
{
    const auto simulate = [](Vec3& out_pos, f32& out_speed) {
        Rig rig;
        rig.setup();
        rig.step(0.0f, 0.0f, 0.0f, false, 240);
        rig.step(1.0f, 0.0f, 0.35f, false, 900);
        out_pos = rig.body().pos;
        out_speed = rig.car.forward_speed(rig.world);
    };

    Vec3 pos_a;
    Vec3 pos_b;
    f32 speed_a = 0.0f;
    f32 speed_b = 0.0f;
    simulate(pos_a, speed_a);
    simulate(pos_b, speed_b);

    CHECK(pos_a.x == pos_b.x);
    CHECK(pos_a.y == pos_b.y);
    CHECK(pos_a.z == pos_b.z);
    CHECK(speed_a == speed_b);
}

namespace {
f32 expected_lock(const Rig& rig)
{
    const VehicleConfig& cfg = rig.car.config();
    const f32 speed = f_abs(rig.car.forward_speed(rig.world));
    return f_remap(speed, 0.0f, cfg.steer_high_speed, cfg.steer_max_deg, cfg.steer_high_deg);
}

i32 ticks_to_lock(Rig& rig, f32 throttle)
{
    i32 ticks = 0;
    while (ticks < 240 && f_abs(rig.car.steer_deg()) < 0.95f * expected_lock(rig)) {
        rig.step(throttle, 0.0f, 1.0f, false, 1);
        ticks++;
    }
    return ticks;
}
}

TEST(vehicle, steering_turns_in_slower_at_speed)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    const i32 slow = ticks_to_lock(rig, 0.0f);
    rig.step(0.0f, 0.0f, 0.0f, false, 120);
    rig.step(1.0f, 0.0f, 0.0f, false, 900);
    CHECK(f_abs(rig.car.forward_speed(rig.world)) > 18.0f);
    const i32 fast = ticks_to_lock(rig, 1.0f);
    CHECK(slow > 0);
    CHECK(fast * 2 > slow * 3);
    CHECK(fast < 240);
}

TEST(vehicle, steering_returns_to_centre_faster_than_it_turns_in)
{
    Rig rig;
    CHECK(rig.setup());
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    const i32 in = ticks_to_lock(rig, 0.0f);
    i32 out = 0;
    while (out < 240 && f_abs(rig.car.steer_deg()) > 0.05f * expected_lock(rig)) {
        rig.step(0.0f, 0.0f, 0.0f, false, 1);
        out++;
    }
    CHECK(in > 0);
    CHECK(out > 0);
    CHECK(out < in);
}

TEST(vehicle, steering_lock_ignores_vertical_speed)
{
    Rig rig;
    CHECK(rig.setup());
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    rig.step(0.0f, 0.0f, 1.0f, false, 120);
    const f32 lock = f_abs(rig.car.steer_deg());
    CHECK(lock > 30.0f);
    rig.world.body(rig.car.body())->vel = Vec3{0.0f, 12.0f, 0.0f};
    rig.step(0.0f, 0.0f, 1.0f, false, 1);
    CHECK_NEAR(f_abs(rig.car.steer_deg()), lock, 0.5);
}

TEST(vehicle, grass_grips_less_than_road_and_rain_stacks_on_top)
{
    CHECK_NEAR(surface_grip_mul(1.0f, 0.0f, 0.8f), 1.0f, 1e-6);
    CHECK_NEAR(surface_grip_mul(0.0f, 0.0f, 0.8f), 0.8f, 1e-6);
    CHECK(surface_grip_mul(0.5f, 0.0f, 0.8f) > 0.8f);
    CHECK(surface_grip_mul(0.5f, 0.0f, 0.8f) < 1.0f);
    CHECK_NEAR(surface_grip_mul(1.0f, 1.0f, 0.8f), 0.76f, 1e-6);
    CHECK_NEAR(surface_grip_mul(0.0f, 1.0f, 0.8f), 0.8f * 0.6f, 1e-6);
    CHECK(surface_grip_mul(0.0f, 0.5f, 0.8f) < surface_grip_mul(1.0f, 0.5f, 0.8f));
    CHECK_NEAR(surface_rolling_mul(1.0f, 2.2f), 1.0f, 1e-6);
    CHECK_NEAR(surface_rolling_mul(0.0f, 2.2f), 2.2f, 1e-6);
}

TEST(vehicle, offroad_rolling_resistance_shortens_the_coast)
{
    const auto coast = [](f32 rolling_mul) {
        Rig rig;
        rig.setup(512, 8.0f);
        rig.step(0.0f, 0.0f, 0.0f, false, 240);
        rig.car.train().gear = 0;
        rig.car.train().manual = true;
        rig.world.body(rig.car.body())->vel = rotate(rig.body().rot, Vec3{0.0f, 0.0f, -15.0f});
        const Vec3 start = rig.body().pos;
        for (i32 i = 0; i < 1200; i++) {
            rig.car.effects().rolling_resist_mul = rolling_mul;
            rig.step(0.0f, 0.0f, 0.0f, false, 1);
        }
        return distance(rig.body().pos, start);
    };
    const f32 road = coast(1.0f);
    const f32 grass = coast(4.0f);
    CHECK(road > 40.0f);
    CHECK(grass < road - 5.0f);
    CHECK(grass < road * 0.92f);
}

TEST(vehicle, wheels_remember_their_previous_compression)
{
    Rig rig;
    CHECK(rig.setup());
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    const f32 compression = rig.car.wheel(0).compression;
    const f32 spin = rig.car.wheel(2).spin_angle;
    rig.step(1.0f, 0.0f, 0.0f, false, 1);
    CHECK(rig.car.wheel(0).prev_compression == compression);
    CHECK(rig.car.wheel(2).prev_spin_angle == spin);
}

TEST(drivetrain, torque_curve_interpolates_and_clamps)
{
    VehicleConfig cfg;
    cfg.torque_count = 3;
    cfg.torque_rpm[0] = 1000.0f; cfg.torque_nm[0] = 100.0f;
    cfg.torque_rpm[1] = 3000.0f; cfg.torque_nm[1] = 200.0f;
    cfg.torque_rpm[2] = 6000.0f; cfg.torque_nm[2] = 150.0f;

    CHECK_NEAR(drivetrain_torque_curve(cfg, 500.0f), 100.0f, 1e-4);
    CHECK_NEAR(drivetrain_torque_curve(cfg, 2000.0f), 150.0f, 1e-4);
    CHECK_NEAR(drivetrain_torque_curve(cfg, 3000.0f), 200.0f, 1e-4);
    CHECK_NEAR(drivetrain_torque_curve(cfg, 9000.0f), 150.0f, 1e-4);
}

TEST(drivetrain, gear_ratios_and_reverse)
{
    VehicleConfig cfg;
    cfg.gear_count = 3;
    cfg.gear_ratios[0] = 3.0f;
    cfg.gear_ratios[1] = 2.0f;
    cfg.gear_ratios[2] = 1.0f;
    cfg.final_drive = 4.0f;
    cfg.reverse_ratio = 3.5f;

    Drivetrain train;
    train.gear = 1;
    CHECK_NEAR(drivetrain_ratio(train, cfg), 12.0f, 1e-4);
    train.gear = 3;
    CHECK_NEAR(drivetrain_ratio(train, cfg), 4.0f, 1e-4);
    train.gear = -1;
    CHECK_NEAR(drivetrain_ratio(train, cfg), -14.0f, 1e-4);
    train.gear = 0;
    CHECK_NEAR(drivetrain_ratio(train, cfg), 0.0f, 1e-6);
}

TEST(drivetrain, idles_at_configured_rpm)
{
    VehicleConfig cfg;
    cfg.idle_rpm = 900.0f;
    Drivetrain train;
    drivetrain_init(train, cfg);
    CHECK_NEAR(drivetrain_rpm(train), 900.0f, 1e-2);
    CHECK(train.gear == 1);
}

TEST(tire, curve_peaks_near_configured_slip)
{
    VehicleConfig cfg;
    cfg.tire_peak_slip = 0.12f;
    cfg.tire_peak_angle_deg = 9.0f;
    cfg.tire_peak_mu = 1.05f;
    cfg.tire_slide_mu = 0.8f;

    const TireParams tp = tire_derive_params(cfg);

    f32 best_slip = 0.0f;
    f32 best_value = 0.0f;
    for (i32 i = 1; i <= 400; i++) {
        const f32 slip = static_cast<f32>(i) * 0.005f;
        const f32 value = tire_curve(slip, tp.b_long, tp.c_long);
        if (value > best_value) {
            best_value = value;
            best_slip = slip;
        }
    }
    CHECK(best_slip > 0.09f);
    CHECK(best_slip < 0.16f);
    CHECK_NEAR(best_value, 1.0f, 1e-2);
}

TEST(tire, combined_force_respects_the_friction_circle)
{
    VehicleConfig cfg;
    const TireParams tp = tire_derive_params(cfg);
    const f32 load = 4000.0f;

    const TireForces tf = tire_compute(tp, 0.9f, 0.8f, 18.0f, 20.0f, load, 1.0f, 1.0f);
    const f32 magnitude = std::sqrt(tf.fx * tf.fx + tf.fy * tf.fy);
    CHECK(magnitude <= tp.peak_mu * load * 1.001f);
}

TEST(tire, zero_slip_produces_no_force)
{
    VehicleConfig cfg;
    const TireParams tp = tire_derive_params(cfg);
    const TireForces tf = tire_compute(tp, 0.0f, 0.0f, 0.0f, 0.0f, 4000.0f, 1.0f, 1.0f);
    CHECK_NEAR(tf.fx, 0.0f, 1e-4);
    CHECK_NEAR(tf.fy, 0.0f, 1e-4);
}

namespace {

f32 kerb_probe_compression(u32 probes)
{
    Rig rig;
    if (!rig.setup()) {
        return 0.0f;
    }
    rig.car.config().susp_probes = probes;
    rig.step(0.0f, 0.0f, 0.0f, false, 240);

    const RigidBody& body = rig.body();
    const Vec3 attach = body.pos + quat_to_mat3(body.rot) * rig.car.wheel(WHEEL_FL).attach_local;
    const f32 radius = rig.car.wheel(WHEEL_FL).radius;
    const f32 edge = attach.z - radius * 0.5f;
    const f32 top = 0.20f;

    rig.world.statics_reserve(rig.arena, 4);
    rig.world.add_static_tri(Vec3{attach.x - 2.0f, top, edge - 4.0f},
                             Vec3{attach.x + 2.0f, top, edge - 4.0f},
                             Vec3{attach.x + 2.0f, top, edge});
    rig.world.add_static_tri(Vec3{attach.x - 2.0f, top, edge - 4.0f},
                             Vec3{attach.x + 2.0f, top, edge},
                             Vec3{attach.x - 2.0f, top, edge});
    rig.world.statics_build(rig.arena);

    rig.step(0.0f, 0.0f, 0.0f, false, 1);
    return rig.car.wheel(WHEEL_FL).compression_raw;
}

f32 drop_deepest_travel(f32 bump_mul, f32 height)
{
    Rig rig;
    if (!rig.setup()) {
        return 0.0f;
    }
    rig.car.config().bump_stop_mul = bump_mul;
    rig.car.teleport(rig.world, Vec3{0.0f, height, 0.0f}, 0.0f);

    f32 deepest = 0.0f;
    for (i32 i = 0; i < 300; i++) {
        rig.step(0.0f, 0.0f, 0.0f, false, 1);
        for (u32 w = 0; w < kWheelCount; w++) {
            deepest = f_max(deepest, rig.car.wheel(w).compression_raw);
        }
    }
    return deepest;
}

f32 split_mu_launch(f32 diff_lock, f32& out_spin_delta)
{
    Rig rig;
    if (!rig.setup()) {
        out_spin_delta = 0.0f;
        return 0.0f;
    }
    rig.car.config().diff_lock = diff_lock;
    rig.car.config().tc_strength = 0.0f;
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    rig.car.effects().tire_grip_mul[WHEEL_RL] = 0.03f;
    rig.step(1.0f, 0.0f, 0.0f, false, 360);

    out_spin_delta = f_abs(rig.car.wheel(WHEEL_RL).omega - rig.car.wheel(WHEEL_RR).omega);
    return rig.car.forward_speed(rig.world);
}

} // namespace

TEST(suspension, probes_envelope_a_kerb_the_centre_ray_misses)
{
    const f32 single = kerb_probe_compression(1);
    const f32 spread = kerb_probe_compression(5);
    CHECK(spread > single + 0.02f);
}

TEST(suspension, a_single_probe_matches_the_old_centre_ray_on_flat_ground)
{
    Rig rig;
    CHECK(rig.setup());
    rig.car.config().susp_probes = 1;
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    const f32 single = rig.car.wheel(WHEEL_FL).compression;

    Rig spread;
    CHECK(spread.setup());
    spread.step(0.0f, 0.0f, 0.0f, false, 240);
    CHECK_NEAR(spread.car.wheel(WHEEL_FL).compression, single, 1e-5);
}

TEST(suspension, bump_stops_only_engage_near_full_travel)
{
    Rig rig;
    CHECK(rig.setup());
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    for (u32 i = 0; i < kWheelCount; i++) {
        CHECK(rig.car.wheel(i).compression < rig.car.config().wheels[i].travel);
        CHECK(rig.car.wheel(i).bump_force == 0.0f);
    }
}

TEST(suspension, bump_stops_limit_travel_on_a_hard_landing)
{
    Rig rig;
    CHECK(rig.setup());
    rig.car.teleport(rig.world, Vec3{0.0f, 2.6f, 0.0f}, 0.0f);

    f32 peak_bump = 0.0f;
    f32 deepest = 0.0f;
    for (i32 i = 0; i < 300; i++) {
        rig.step(0.0f, 0.0f, 0.0f, false, 1);
        for (u32 w = 0; w < kWheelCount; w++) {
            peak_bump = f_max(peak_bump, rig.car.wheel(w).bump_force);
            deepest = f_max(deepest, rig.car.wheel(w).compression_raw);
        }
    }

    const f32 travel = rig.car.config().wheels[0].travel;
    CHECK(peak_bump > 0.0f);
    CHECK(deepest <= travel * (1.0f + rig.car.config().bump_stop_zone) + 1e-4f);
}

TEST(suspension, bump_stops_arrest_travel_before_the_hard_limit)
{
    const f32 with_stops = drop_deepest_travel(10.0f, 0.9f);
    const f32 without = drop_deepest_travel(0.0f, 0.9f);
    CHECK(with_stops < without);
}

TEST(tire, aligning_torque_opposes_the_yaw_of_a_steady_turn)
{
    Rig rig;
    CHECK(rig.setup());
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    rig.step(1.0f, 0.0f, 0.6f, false, 480);

    f32 yaw_moment = 0.0f;
    for (u32 i = 0; i < kWheelCount; i++) {
        const Wheel& w = rig.car.wheel(i);
        yaw_moment += w.contact_normal.y * w.align_torque;
    }
    const f32 yaw_rate = rig.body().angular_vel.y;

    CHECK(f_abs(yaw_rate) > 0.1f);
    CHECK(f_abs(yaw_moment) > 1.0f);
    CHECK(yaw_moment * yaw_rate < 0.0f);
}

TEST(tire, the_trail_settles_the_car_into_a_tighter_yaw_rate)
{
    const auto yaw_rate = [](bool trail) {
        Rig rig;
        rig.setup(512, 8.0f);
        if (!trail) {
            rig.car.config().tire_pneumatic_trail = 0.0f;
            rig.car.config().tire_mech_trail = 0.0f;
        }
        rig.step(0.0f, 0.0f, 0.0f, false, 240);
        rig.step(1.0f, 0.0f, 0.0f, false, 420);
        rig.step(0.35f, 0.0f, 0.6f, false, 360);
        f32 sum = 0.0f;
        for (i32 i = 0; i < 240; i++) {
            rig.step(0.35f, 0.0f, 0.6f, false, 1);
            sum += f_abs(rig.body().angular_vel.y);
        }
        return sum / 240.0f;
    };

    CHECK(yaw_rate(true) < yaw_rate(false));
}

TEST(differential, an_open_diff_spins_the_unloaded_wheel)
{
    f32 spin_delta = 0.0f;
    split_mu_launch(0.0f, spin_delta);
    CHECK(spin_delta > 5.0f);
}

TEST(differential, a_locking_diff_drives_through_the_gripping_wheel)
{
    f32 open_delta = 0.0f;
    f32 locked_delta = 0.0f;
    const f32 open_speed = split_mu_launch(0.0f, open_delta);
    const f32 locked_speed = split_mu_launch(1.0f, locked_delta);

    CHECK(locked_delta < open_delta);
    CHECK(locked_speed > open_speed);
}

TEST(differential, a_partial_lock_lands_between_open_and_locked)
{
    f32 open_delta = 0.0f;
    f32 partial_delta = 0.0f;
    f32 locked_delta = 0.0f;
    const f32 open_speed = split_mu_launch(0.0f, open_delta);
    const f32 partial_speed = split_mu_launch(0.25f, partial_delta);
    const f32 locked_speed = split_mu_launch(1.0f, locked_delta);

    CHECK(partial_speed > open_speed);
    CHECK(partial_speed < locked_speed);
    CHECK(partial_delta >= locked_delta);
}


TEST(differential, locking_only_redistributes_axle_torque)
{
    Rig rig;
    CHECK(rig.setup());
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    rig.car.effects().tire_grip_mul[WHEEL_RL] = 0.05f;
    rig.step(1.0f, 0.0f, 0.0f, false, 180);

    const auto axle = [&rig](f32 diff_lock, f32& out_split) {
        Wheel wheels[kWheelCount];
        for (u32 i = 0; i < kWheelCount; i++) {
            wheels[i] = rig.car.wheel(i);
        }
        Drivetrain train = rig.car.train();
        VehicleConfig cfg = rig.car.config();
        cfg.diff_lock = diff_lock;
        drivetrain_tick(train, cfg, wheels, 1.0f, 1.0f, true, kDt);
        out_split = wheels[WHEEL_RR].drive_torque - wheels[WHEEL_RL].drive_torque;
        return wheels[WHEEL_RL].drive_torque + wheels[WHEEL_RR].drive_torque;
    };

    f32 open_split = 0.0f;
    f32 locked_split = 0.0f;
    const f32 open_total = axle(0.0f, open_split);
    const f32 locked_total = axle(1.0f, locked_split);

    CHECK_NEAR(locked_total, open_total, 1e-3);
    CHECK(f_abs(locked_split) > f_abs(open_split));
}

TEST(vehicle, polling_an_unchanged_config_does_not_reload)
{
    Rig rig;
    CHECK(rig.setup());
    rig.car.config().tire_peak_mu = 9.0f;

    CHECK(!rig.car.poll_config_reload(rig.world, rig.arena));
    CHECK_NEAR(rig.car.config().tire_peak_mu, 9.0f, 1e-5);

    CHECK(rig.car.reload_config(rig.world, rig.arena));
    CHECK(rig.car.config().tire_peak_mu < 9.0f);
}

TEST(suspension, the_chassis_proxy_rides_clear_of_the_contact_patches)
{
    Rig rig;
    CHECK(rig.setup());
    const RigidBody* body = rig.world.body(rig.car.body());
    CHECK(body != nullptr);

    const BodyDesc& desc = rig.car.body_desc();
    const f32 lowest_sphere = desc.box_offset.y - desc.half_extents.y;
    CHECK(body->jolt_id != kNoJoltBody);

    const VehicleConfig& cfg = rig.car.config();
    f32 lowest_tread = 1.0e9f;
    for (u32 i = 0; i < kWheelCount; i++) {
        lowest_tread = f_min(lowest_tread,
                             cfg.wheels[i].pos.y - cfg.com_offset.y - cfg.wheels[i].radius);
    }

    // The floor must not reach the ground until most of the travel is gone, or it carries
    // load the tyres should have and the car stops steering.
    CHECK(lowest_sphere - lowest_tread > cfg.wheels[0].travel * 0.5f);
}

namespace {

f32 body_slip_deg(const Rig& rig)
{
    const RigidBody& b = rig.body();
    const Vec3 flat{b.vel.x, 0.0f, b.vel.z};
    const f32 speed = length(flat);
    if (speed < 0.5f) {
        return 0.0f;
    }
    const Vec3 fwd = rotate(b.rot, Vec3{0.0f, 0.0f, -1.0f});
    const Vec3 f2 = normalize(Vec3{fwd.x, 0.0f, fwd.z});
    return std::acos(f_clamp(dot(flat * (1.0f / speed), f2), -1.0f, 1.0f)) * kRadToDeg;
}

bool reach_speed(Rig& rig, f32 target_kmh)
{
    for (i32 i = 0; i < 4000; i++) {
        rig.step(1.0f, 0.0f, 0.0f, false, 1);
        if (length(rig.body().vel) * 3.6f >= target_kmh) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(tire, a_sliding_wheel_gives_up_its_cornering_force)
{
    VehicleConfig cfg;
    Arena arena{megabytes(1)};
    CHECK(vehicle_config_load(cfg, arena, kCarPath));
    const TireParams tp = tire_derive_params(cfg);

    // Twenty metres a second down the road at six degrees of drift, rolling then locked.
    const f32 road = 20.0f;
    const f32 angle = 6.0f * kDegToRad;
    const f32 lat = road * std::tan(angle);
    const TireForces rolling = tire_compute(tp, 0.0f, angle, 0.0f, lat, 3000.0f, 1.0f, 1.0f);
    const TireForces locked = tire_compute(tp, -1.0f, angle, -road, lat, 3000.0f, 1.0f, 1.0f);

    // A wheel dragged along the road cannot also be cornering: nearly all of the friction
    // it has left is spent resisting the direction it is actually sliding.
    CHECK(f_abs(rolling.fy) > 1000.0f);
    CHECK(f_abs(locked.fy) < f_abs(rolling.fy) * 0.35f);
    CHECK(f_abs(locked.fx) > f_abs(locked.fy) * 3.0f);
}

TEST(tire, the_friction_circle_is_never_exceeded)
{
    VehicleConfig cfg;
    Arena arena{megabytes(1)};
    CHECK(vehicle_config_load(cfg, arena, kCarPath));
    const TireParams tp = tire_derive_params(cfg);

    const f32 load = 2800.0f;
    const f32 limit = tp.peak_mu * load;
    for (i32 r = -40; r <= 40; r++) {
        for (i32 a = -40; a <= 40; a++) {
            const f32 ratio = static_cast<f32>(r) * 0.1f;
            const f32 angle = static_cast<f32>(a) * 0.05f;
            const TireForces f = tire_compute(tp, ratio, angle, ratio * 20.0f,
                                              std::tan(angle) * 20.0f, load, 1.0f, 1.0f);
            CHECK(std::sqrt(f.fx * f.fx + f.fy * f.fy) <= limit * 1.001f);
        }
    }
}

TEST(handling, the_handbrake_breaks_the_rear_loose_at_speed)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    CHECK(reach_speed(rig, 110.0f));

    f32 peak_slip = 0.0f;
    f32 peak_yaw = 0.0f;
    f32 peak_lock = 0.0f;
    for (i32 i = 0; i < 240; i++) {
        rig.step(0.0f, 0.0f, -1.0f, true, 1);
        peak_slip = f_max(peak_slip, body_slip_deg(rig));
        peak_yaw = f_max(peak_yaw, f_abs(rig.body().angular_vel.y) * kRadToDeg);
        peak_lock = f_max(peak_lock, f_abs(rig.car.wheel(WHEEL_RL).slip_ratio));
    }
    CHECK(peak_slip > 30.0f);
    CHECK(peak_yaw > 60.0f);
    CHECK(peak_lock > 0.8f);
}

TEST(handling, cornering_on_the_throttle_stays_hooked_up)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    CHECK(reach_speed(rig, 90.0f));

    f32 peak_slip = 0.0f;
    for (i32 i = 0; i < 400; i++) {
        rig.step(0.35f, 0.0f, -1.0f, false, 1);
        peak_slip = f_max(peak_slip, body_slip_deg(rig));
    }
    // The same lock without the handbrake has to stay a corner, not become a spin.
    CHECK(peak_slip < 10.0f);
    CHECK(f_abs(rig.body().angular_vel.y) * kRadToDeg > 8.0f);
}

TEST(handling, the_underside_stays_clear_over_a_crest)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));

    // A rise the wheelbase can straddle: the underside must ride over it on the tyres.
    for (u32 z = 0; z < rig.hf.size_z(); z++) {
        for (u32 x = 0; x < rig.hf.size_x(); x++) {
            const f32 wx = rig.hf.origin().x + static_cast<f32>(x) * rig.hf.cell_size();
            rig.hf.set_height(x, z, 0.35f * std::exp(-(wx * wx) / (2.0f * 14.0f * 14.0f)));
        }
    }
    rig.hf.recompute_extents();
    rig.world.sync_jolt();

    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    f32 worst = 1.0e9f;
    const BodyDesc& desc = rig.car.body_desc();
    for (i32 i = 0; i < 900; i++) {
        rig.step(0.5f, 0.0f, 0.0f, false, 1);
        const RigidBody& b = rig.body();
        for (u32 s = 0; s < 4; s++) {
            const Vec3 corner{(s & 1) ? desc.half_extents.x : -desc.half_extents.x,
                              -desc.half_extents.y,
                              (s & 2) ? desc.half_extents.z : -desc.half_extents.z};
            const Vec3 w = b.pos + rotate(b.rot, desc.box_offset + corner);
            worst = f_min(worst, w.y - rig.hf.sample(w.x, w.z));
        }
    }
    CHECK(worst > 0.0f);
}

TEST(handling, the_body_model_clears_the_road_it_rides_on)
{
    Arena arena{megabytes(16)};
    VehicleConfig cfg;
    CHECK(vehicle_config_load(cfg, arena, kCarPath));

    MeshData body;
    CHECK(load_mesh("assets/meshes/excel_body.amsh", arena, body) == MeshParseError::Ok);
    f32 lowest = 1.0e9f;
    for (const AmshVertex& v : body.vertices) {
        lowest = f_min(lowest, v.pos[1]);
    }

    // Where the road sits in mesh space once the springs have taken the car's weight.
    const WheelConfig& wc = cfg.wheels[WHEEL_FL];
    const f32 sag = (cfg.mass * 9.81f * 0.25f) / wc.spring_k;
    const f32 road = wc.pos.y - wc.travel - wc.radius + sag;

    CHECK(lowest - road > 0.08f);
}

TEST(tire, a_sliding_wheel_pushes_back_along_the_way_it_is_sliding)
{
    VehicleConfig cfg;
    Arena arena{megabytes(1)};
    CHECK(vehicle_config_load(cfg, arena, kCarPath));
    const TireParams tp = tire_derive_params(cfg);

    // Locked, and dragged sideways at twenty degrees to where it points. Friction has to
    // come back along that line, so a fifth of it is cornering force -- not none.
    const f32 road = 20.0f;
    const f32 drift = 20.0f * kDegToRad;
    const TireForces f = tire_compute(tp, -1.0f, drift, -road, road * std::tan(drift), 3000.0f,
                                      1.0f, 1.0f);
    const f32 total = std::sqrt(f.fx * f.fx + f.fy * f.fy);
    CHECK(total > 1500.0f);
    CHECK_NEAR(f_abs(f.fy) / total, std::sin(drift), 0.02);
    // Both components oppose the slide: braking force, and cornering force against it.
    CHECK(f.fx < 0.0f);
    CHECK(f.fy < 0.0f);
}

TEST(handling, braking_into_a_corner_stays_catchable)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    CHECK(reach_speed(rig, 85.0f));

    for (i32 i = 0; i < 90; i++) {
        rig.step(0.30f, 0.0f, -0.55f, false, 1);
    }

    f32 peak = 0.0f;
    for (i32 i = 0; i < 300; i++) {
        rig.step(0.0f, 0.55f, -0.55f, false, 1);
        peak = f_max(peak, body_slip_deg(rig));
    }
    // Trail braking may step the back out, but it has to come back, not spin.
    CHECK(peak < 20.0f);
    CHECK(body_slip_deg(rig) < 15.0f);
}

TEST(handling, lifting_mid_corner_does_not_throw_the_car)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    CHECK(reach_speed(rig, 85.0f));

    for (i32 i = 0; i < 90; i++) {
        rig.step(0.30f, 0.0f, -0.55f, false, 1);
    }
    f32 peak = 0.0f;
    for (i32 i = 0; i < 300; i++) {
        rig.step(0.0f, 0.0f, -0.55f, false, 1);
        peak = f_max(peak, body_slip_deg(rig));
    }
    CHECK(peak < 10.0f);
}

TEST(handling, the_rear_brakes_let_go_after_the_front_ones)
{
    const VehicleConfig* cfg = nullptr;
    Rig rig;
    CHECK(rig.setup());
    cfg = &rig.car.config();
    CHECK(cfg->wheels[WHEEL_RL].brake_share < cfg->wheels[WHEEL_FL].brake_share);

    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    rig.step(1.0f, 0.0f, 0.0f, false, 420);
    // Half pedal in a straight line must not lock anything.
    rig.step(0.0f, 0.5f, 0.0f, false, 40);
    for (u32 i = 0; i < kWheelCount; i++) {
        CHECK(f_abs(rig.car.wheel(i).slip_ratio) < 0.9f);
    }
}

TEST(handling, a_hard_lane_change_at_speed_settles)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    CHECK(reach_speed(rig, 100.0f));
    rig.step(0.3f, 0.0f, 0.0f, false, 30);

    f32 peak = 0.0f;
    for (i32 i = 0; i < 360; i++) {
        const f32 steer = i < 60 ? -1.0f : (i < 120 ? 1.0f : 0.0f);
        rig.step(0.3f, 0.0f, steer, false, 1);
        peak = f_max(peak, body_slip_deg(rig));
    }
    CHECK(peak < 16.0f);
    CHECK(body_slip_deg(rig) < 3.0f);
}

TEST(handling, flooring_it_out_of_a_corner_stays_catchable)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    CHECK(reach_speed(rig, 50.0f));
    rig.step(0.2f, 0.0f, -0.6f, false, 120);

    f32 peak = 0.0f;
    for (i32 i = 0; i < 300; i++) {
        rig.step(1.0f, 0.0f, -0.6f, false, 1);
        peak = f_max(peak, body_slip_deg(rig));
    }
    CHECK(peak < 18.0f);
    CHECK(body_slip_deg(rig) < 12.0f);
}

TEST(drivetrain, kickdown_never_drops_into_a_gear_past_the_shift_point)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    CHECK(reach_speed(rig, 50.0f));
    rig.step(0.2f, 0.0f, 0.0f, false, 240);

    for (i32 i = 0; i < 240; i++) {
        rig.step(1.0f, 0.0f, 0.0f, false, 1);
        CHECK(rig.car.train().gear >= 2);
    }
}

TEST(drivetrain, a_third_throttle_launch_does_not_judder)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));
    rig.step(0.0f, 0.0f, 0.0f, false, 240);

    f32 worst_slip = 0.0f;
    f32 worst_tc = 0.0f;
    for (i32 i = 0; i < 600; i++) {
        rig.step(0.35f, 0.0f, 0.0f, false, 1);
        if (i >= 360) {
            worst_slip = f_max(worst_slip, f_abs(rig.car.wheel(WHEEL_RL).slip_ratio));
            worst_tc = f_max(worst_tc, rig.car.train().tc_cut);
        }
    }
    CHECK(worst_slip < 0.15f);
    CHECK(worst_tc < 0.05f);
    CHECK(rig.car.forward_speed(rig.world) > 4.0f);
}

TEST(drivetrain, idle_creep_is_gentle)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));
    rig.step(0.0f, 0.0f, 0.0f, false, 840);
    const f32 speed = rig.car.forward_speed(rig.world);
    CHECK(speed > 0.2f);
    CHECK(speed < 2.3f);
}

TEST(drivetrain, a_light_brake_holds_against_creep)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));
    rig.step(0.0f, 0.3f, 0.0f, false, 600);
    CHECK(f_abs(rig.car.forward_speed(rig.world)) < 0.05f);
    CHECK(rig.car.train().brake_hold);
    CHECK(rig.car.train().engine_omega > 0.0f);
}

TEST(drivetrain, traction_control_does_not_strangle_a_launch)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));
    rig.step(0.0f, 0.0f, 0.0f, false, 240);

    i32 ticks = 0;
    while (ticks < 1200 && length(rig.body().vel) * 3.6f < 50.0f) {
        rig.step(1.0f, 0.0f, 0.0f, false, 1);
        ticks++;
    }
    CHECK(static_cast<f32>(ticks) * kDt < 4.5f);
}

namespace {
void hash_f32(u64& h, f32 v)
{
    u32 bits;
    std::memcpy(&bits, &v, sizeof(bits));
    h ^= bits;
    h *= 1099511628211ull;
}

void hash_vec(u64& h, Vec3 v)
{
    hash_f32(h, v.x);
    hash_f32(h, v.y);
    hash_f32(h, v.z);
}
}

TEST(vehicle, the_chassis_stops_against_a_static_prop)
{
    Rig rig;
    CHECK(rig.setup());
    rig.world.add_static_box(Vec3{0.0f, 1.5f, -14.0f}, Vec3{3.0f, 1.5f, 1.0f}, 0);
    rig.world.statics_build(rig.arena);

    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    rig.step(1.0f, 0.0f, 0.0f, false, 600);

    const RigidBody& body = rig.body();
    CHECK(body_state_valid(body));
    CHECK(body.pos.z > -13.0f + rig.car.config().half_extents.z - 0.3f);
    CHECK(length(body.vel) < 2.0f);
    CHECK(dot(rotate(body.rot, Vec3{0.0f, 1.0f, 0.0f}), Vec3{0.0f, 1.0f, 0.0f}) > 0.9f);
}

TEST(vehicle, wheel_rays_see_a_static_box)
{
    Rig rig;
    CHECK(rig.setup());
    rig.world.add_static_box(Vec3{0.0f, 0.75f, 0.0f}, Vec3{6.0f, 0.75f, 6.0f}, 0);
    rig.world.statics_build(rig.arena);
    rig.car.teleport(rig.world, Vec3{0.0f, 2.5f, 0.0f}, 0.0f);

    rig.step(0.0f, 1.0f, 0.0f, true, 480);

    for (u32 i = 0; i < kWheelCount; i++) {
        CHECK(rig.car.wheel(i).grounded);
        CHECK(rig.car.wheel(i).contact_point.y > 1.4f);
    }
    CHECK(rig.body().pos.y > 1.5f);
    CHECK(length(rig.body().vel) < 0.1f);
}

TEST(vehicle, a_config_reload_resizes_the_chassis)
{
    Rig rig;
    CHECK(rig.setup());
    rig.step(0.0f, 0.0f, 0.0f, false, 240);

    RigidBody* body = rig.world.body(rig.car.body());
    const u32 before = body->jolt_id;
    const f32 mass = rig.car.config().mass * 1.5f;
    rig.car.config().mass = mass;
    rig.car.config().half_extents.y += 0.1f;
    body->vel = Vec3{0.0f, 0.0f, -3.0f};
    rig.car.apply_config(rig.world);

    CHECK(body->jolt_id != before);
    CHECK_NEAR(body->inv_mass, 1.0f / mass, 1e-6);
    CHECK_NEAR(rig.car.body_desc().mass, mass, 1e-6);
    CHECK(rig.world.jolt()->dynamicCount() == 1);

    rig.step(0.0f, 0.0f, 0.0f, false, 1);
    CHECK(body->vel.z < -2.5f);
    CHECK(body->vel.z > -3.5f);

    rig.car.apply_config(rig.world);
    CHECK(body->jolt_id == rig.world.body(rig.car.body())->jolt_id);
}

TEST(vehicle, driving_through_props_and_pickups_is_bit_identical)
{
    u64 sums[2] = {14695981039346656037ull, 14695981039346656037ull};
    for (u32 pass = 0; pass < 2; pass++) {
        Rig rig;
        CHECK(rig.setup());
        rig.world.add_static_box(Vec3{1.2f, 0.5f, -20.0f}, Vec3{0.5f, 0.5f, 0.5f}, 0);
        rig.world.statics_build(rig.arena);
        const auto crate = rig.world.jolt()->addDynamicBox(glm::vec3(-0.3f, 0.3f, -9.0f), glm::vec3(0.25f), 6.0f, 0);

        rig.step(0.0f, 0.0f, 0.0f, false, 120);
        rig.step(1.0f, 0.0f, 0.08f, false, 480);
        rig.step(0.0f, 1.0f, -0.2f, false, 240);

        const RigidBody& body = rig.body();
        hash_vec(sums[pass], body.pos);
        hash_vec(sums[pass], body.vel);
        hash_vec(sums[pass], body.angular_vel);
        hash_f32(sums[pass], body.rot.x);
        hash_f32(sums[pass], body.rot.y);
        hash_f32(sums[pass], body.rot.z);
        hash_f32(sums[pass], body.rot.w);
        for (u32 i = 0; i < kWheelCount; i++) {
            hash_f32(sums[pass], rig.car.wheel(i).omega);
        }
        const ghost::engine::BodyState c = rig.world.jolt()->state(crate);
        hash_f32(sums[pass], c.position.x);
        hash_f32(sums[pass], c.position.y);
        hash_f32(sums[pass], c.position.z);
        CHECK(body_state_valid(body));
        CHECK(body.pos.z < -8.0f);
    }
    CHECK(sums[0] == sums[1]);
}

namespace {
f32 signed_slip_deg(const Rig& rig)
{
    const RigidBody& b = rig.body();
    const Vec3 v = rotate(conjugate(b.rot), b.vel);
    if (std::sqrt(v.x * v.x + v.z * v.z) < 1.0f) {
        return 0.0f;
    }
    return std::atan2(v.x, f_abs(v.z)) * kRadToDeg;
}

struct Climb {
    f32 height = 0.0f;
    f32 rear_slip = 0.0f;
};

Climb climb(f32 slope_deg, f32 grip, f32 fixed_throttle, f32 seconds)
{
    Rig rig;
    if (!rig.setup_ramp(slope_deg)) {
        return Climb{};
    }
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    const f32 ground = rig.body().pos.y;
    const f32 throttle = fixed_throttle;
    Climb out;
    f32 slip_sum = 0.0f;
    i32 slip_ticks = 0;
    const i32 ticks = static_cast<i32>(seconds / kDt);
    for (i32 i = 0; i < ticks; i++) {
        for (f32& g : rig.car.effects().tire_grip_mul) {
            g = grip;
        }
        const f32 slip = f_max(rig.car.wheel(WHEEL_RL).slip_ratio, rig.car.wheel(WHEEL_RR).slip_ratio);
        rig.step(throttle, 0.0f, 0.0f, false, 1);
        if (rig.body().pos.y - ground > 2.0f) {
            slip_sum += slip;
            slip_ticks++;
        }
        out.height = f_max(out.height, rig.body().pos.y - ground);
    }
    out.rear_slip = slip_ticks > 0 ? slip_sum / static_cast<f32>(slip_ticks) : 0.0f;
    return out;
}
}

TEST(climbing, full_throttle_gets_up_a_35_degree_grass_slope)
{
    CHECK(climb(35.0f, 0.95f, 1.0f, 15.0f).height > 25.0f);
}

TEST(climbing, full_throttle_gets_up_a_40_degree_road_slope)
{
    CHECK(climb(40.0f, 1.0f, 1.0f, 15.0f).height > 25.0f);
}

TEST(climbing, half_throttle_is_not_enough_for_a_40_degree_slope)
{
    const Climb c = climb(40.0f, 1.0f, 0.5f, 15.0f);
    CHECK(c.height < 12.0f);
    CHECK(c.rear_slip > 0.15f);
}

TEST(handling, a_handbrake_slide_is_caught_by_countersteering)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    CHECK(reach_speed(rig, 72.0f));
    for (i32 i = 0; i < 240 && f_abs(signed_slip_deg(rig)) < 25.0f; i++) {
        rig.step(0.0f, 0.0f, 1.0f, true, 1);
    }
    CHECK(f_abs(signed_slip_deg(rig)) > 20.0f);
    i32 caught = -1;
    for (i32 i = 0; i < 300 && caught < 0; i++) {
        const f32 slip = signed_slip_deg(rig);
        const f32 steer = f_clamp(slip / 15.0f, -1.0f, 1.0f);
        rig.step(0.15f, 0.0f, steer, false, 1);
        if (f_abs(signed_slip_deg(rig)) < 10.0f && length(rig.body().vel) > 3.0f) {
            caught = i;
        }
    }
    CHECK(caught >= 0);
    CHECK(static_cast<f32>(caught) * kDt < 2.5f);
}

namespace {
f32 handbrake_turn_around(Rig& rig)
{
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    if (!reach_speed(rig, 50.0f)) {
        return 0.0f;
    }
    const Vec3 start = rotate(rig.body().rot, Vec3{0.0f, 0.0f, -1.0f});
    for (i32 i = 0; i < 600; i++) {
        rig.step(1.0f, 0.0f, 1.0f, true, 1);
        if (dot(rotate(rig.body().rot, Vec3{0.0f, 0.0f, -1.0f}), start) < -0.85f) {
            break;
        }
    }
    return drivetrain_rpm(rig.car.train());
}
}

TEST(drivetrain, a_drift_exit_burns_rubber_and_pulls_away)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));
    const f32 rpm = handbrake_turn_around(rig);
    CHECK(rpm > rig.car.config().max_rpm * 0.55f);
    const f32 before = rig.car.forward_speed(rig.world);
    f32 most_spin = 0.0f;
    for (i32 i = 0; i < 240; i++) {
        rig.step(1.0f, 0.0f, 0.0f, false, 1);
        most_spin = f_max(most_spin, rig.car.wheel(WHEEL_RL).slip_ratio);
    }
    CHECK(most_spin > rig.car.config().tire_peak_slip * 3.0f);
    CHECK(rig.car.forward_speed(rig.world) - before > 10.0f);
}

TEST(drivetrain, a_standing_start_lights_up_the_rear)
{
    Rig rig;
    CHECK(rig.setup(512, 8.0f));
    rig.step(0.0f, 0.0f, 0.0f, false, 240);
    f32 most_spin = 0.0f;
    for (i32 i = 0; i < 60; i++) {
        rig.step(1.0f, 0.0f, 0.0f, false, 1);
        most_spin = f_max(most_spin, rig.car.wheel(WHEEL_RL).slip_ratio);
    }
    CHECK(most_spin > 0.3f);
}

namespace {
constexpr const char* kMustangPath = "assets/cars/mustang.cfg";

bool mesh_bounds(std::string_view name, Arena& arena, Vec3& lo, Vec3& hi)
{
    FixedString<96> path;
    path.format("assets/meshes/%.*s.amsh", static_cast<int>(name.size()), name.data());
    MeshData data;
    if (load_mesh(path.view(), arena, data) != MeshParseError::Ok || data.vertices.empty()) {
        return false;
    }
    lo = Vec3{1e9f, 1e9f, 1e9f};
    hi = Vec3{-1e9f, -1e9f, -1e9f};
    for (const AmshVertex& v : data.vertices) {
        lo = Vec3{f_min(lo.x, v.pos[0]), f_min(lo.y, v.pos[1]), f_min(lo.z, v.pos[2])};
        hi = Vec3{f_max(hi.x, v.pos[0]), f_max(hi.y, v.pos[1]), f_max(hi.z, v.pos[2])};
    }
    return true;
}
}

TEST(mustang, its_config_names_meshes_that_exist_and_seats_both_sides)
{
    Arena arena{megabytes(32)};
    VehicleConfig cfg;
    CHECK(vehicle_config_load(cfg, arena, kMustangPath));
    CHECK(cfg.seat_count == 2u);
    CHECK(cfg.seats[0].eye.x < 0.0f);
    CHECK(cfg.seats[1].eye.x > 0.0f);
    const CarLayout& l = cfg.layout;
    const FixedString<32>* names[] = {&cfg.body_mesh, &cfg.wheel_mesh, &l.hood_mesh, &l.trunk_mesh, &l.door_mesh[0], &l.door_mesh[1],
                                      &l.door_glass_mesh[0], &l.door_glass_mesh[1], &l.glass_mesh, &l.brakelight_mesh,
                                      &l.revlight_mesh, &l.interior_mesh, &l.fuelcap_mesh, &l.lever_mesh};
    for (const FixedString<32>* name : names) {
        Vec3 lo;
        Vec3 hi;
        CHECK(!name->empty());
        CHECK(mesh_bounds(name->view(), arena, lo, hi));
    }
    CHECK(l.popup_mesh.empty());
    VehicleConfig excel;
    CHECK(vehicle_config_load(excel, arena, kCarPath));
}

TEST(mustang, the_body_clears_the_road_and_the_wheel_mesh_matches_the_physics)
{
    Arena arena{megabytes(32)};
    VehicleConfig cfg;
    CHECK(vehicle_config_load(cfg, arena, kMustangPath));
    Vec3 lo;
    Vec3 hi;
    CHECK(mesh_bounds(cfg.body_mesh.view(), arena, lo, hi));
    const WheelConfig& wc = cfg.wheels[WHEEL_FL];
    const f32 sag = (cfg.mass * 9.81f * 0.25f) / wc.spring_k;
    const f32 road = wc.pos.y - wc.travel - wc.radius + sag;
    CHECK(lo.y - road > 0.08f);
    CHECK(f_abs(hi.x - cfg.half_extents.x) < 0.05f);

    Vec3 wlo;
    Vec3 whi;
    CHECK(mesh_bounds(cfg.wheel_mesh.view(), arena, wlo, whi));
    CHECK(f_abs((whi.y - wlo.y) * 0.5f - wc.radius) < 0.01f);
    CHECK(f_abs(wlo.y + whi.y) < 0.01f);
    VehicleConfig excel;
    CHECK(vehicle_config_load(excel, arena, kCarPath));
}

TEST(mustang, its_panels_hang_from_their_hinges_and_the_trunk_fits_the_body)
{
    Arena arena{megabytes(32)};
    VehicleConfig cfg;
    CHECK(vehicle_config_load(cfg, arena, kMustangPath));
    const CarLayout& l = cfg.layout;
    Vec3 lo;
    Vec3 hi;
    CHECK(mesh_bounds(l.hood_mesh.view(), arena, lo, hi));
    CHECK(f_abs(hi.z) < 0.01f);
    CHECK(lo.z < -0.8f);
    CHECK(mesh_bounds(l.trunk_mesh.view(), arena, lo, hi));
    CHECK(f_abs(lo.z) < 0.01f);
    CHECK(hi.z > 0.3f);
    CHECK(mesh_bounds(l.door_mesh[0].view(), arena, lo, hi));
    CHECK(f_abs(lo.z) < 0.01f);
    CHECK(f_abs(lo.x) < 0.01f);
    CHECK(hi.z > 1.0f);
    Vec3 blo;
    Vec3 bhi;
    CHECK(mesh_bounds(cfg.body_mesh.view(), arena, blo, bhi));
    CHECK(l.trunk_min.x > blo.x);
    CHECK(l.trunk_max.x < bhi.x);
    CHECK(l.trunk_max.z < bhi.z);
    CHECK(l.trunk_min.y > blo.y);
    VehicleConfig excel;
    CHECK(vehicle_config_load(excel, arena, kCarPath));
}

TEST(mustang, it_launches_climbs_and_catches_a_slide_like_the_excel)
{
    {
        Rig rig;
        rig.path = kMustangPath;
        CHECK(rig.setup(512, 8.0f));
        rig.step(0.0f, 0.0f, 0.0f, false, 240);
        i32 ticks = 0;
        while (ticks < 1200 && length(rig.body().vel) * 3.6f < 50.0f) {
            rig.step(1.0f, 0.0f, 0.0f, false, 1);
            ticks++;
        }
        CHECK(static_cast<f32>(ticks) * kDt < 4.5f);
    }
    {
        Rig rig;
        rig.path = kMustangPath;
        CHECK(rig.setup(512, 8.0f));
        rig.step(0.0f, 0.0f, 0.0f, false, 240);
        CHECK(reach_speed(rig, 72.0f));
        for (i32 i = 0; i < 240 && f_abs(signed_slip_deg(rig)) < 25.0f; i++) {
            rig.step(0.0f, 0.0f, 1.0f, true, 1);
        }
        i32 caught = -1;
        for (i32 i = 0; i < 300 && caught < 0; i++) {
            const f32 slip = signed_slip_deg(rig);
            rig.step(0.15f, 0.0f, f_clamp(slip / 15.0f, -1.0f, 1.0f), false, 1);
            if (f_abs(signed_slip_deg(rig)) < 10.0f && length(rig.body().vel) > 3.0f) {
                caught = i;
            }
        }
        CHECK(caught >= 0);
    }
    Arena arena{megabytes(4)};
    VehicleConfig excel;
    CHECK(vehicle_config_load(excel, arena, kCarPath));
}
