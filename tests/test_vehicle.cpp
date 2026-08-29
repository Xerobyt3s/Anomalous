#include "test.h"

#include "core/arena.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "vehicle/tire.h"
#include "vehicle/vehicle.h"

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

struct Rig {
    Arena arena{megabytes(64)};
    Heightfield hf;
    PhysWorld world;
    Vehicle car;

    bool setup()
    {
        hf = make_flat(arena);
        world.init(arena, &hf);
        return car.init(world, arena, kCarPath, Vec3{0.0f, 1.0f, 0.0f}, 0.0f);
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

    const TireForces tf = tire_compute(tp, 0.9f, 0.8f, load, 1.0f, 1.0f);
    const f32 magnitude = std::sqrt(tf.fx * tf.fx + tf.fy * tf.fy);
    CHECK(magnitude <= tp.peak_mu * load * 1.001f);
}

TEST(tire, zero_slip_produces_no_force)
{
    VehicleConfig cfg;
    const TireParams tp = tire_derive_params(cfg);
    const TireForces tf = tire_compute(tp, 0.0f, 0.0f, 4000.0f, 1.0f, 1.0f);
    CHECK_NEAR(tf.fx, 0.0f, 1e-4);
    CHECK_NEAR(tf.fy, 0.0f, 1e-4);
}
