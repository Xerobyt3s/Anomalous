#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "physics/body.h"
#include "physics/world.h"
#include "vehicle/vehicle_config.h"

#include <string_view>

namespace anom {
class Arena;
class PhysWorld;
struct TireParams;
struct WheelFrame;

struct Drivetrain {
    f32 engine_omega = 0.0f;
    i32 gear = 1;
    f32 shift_timer = 0.0f;
    f32 shift_lockout = 0.0f;
    bool shifting = false;
    bool manual = false;
    i32 shift_request = 0;
    f32 tc_cut = 0.0f;
    bool brake_hold = false;
};

struct Wheel {
    Vec3 attach_local;
    f32 radius = 0.34f;
    f32 steer_rad = 0.0f;
    f32 omega = 0.0f;
    f32 spin_angle = 0.0f;
    f32 compression = 0.0f;
    f32 compression_raw = 0.0f;
    f32 bump_force = 0.0f;
    f32 align_torque = 0.0f;
    f32 load = 0.0f;
    f32 slip_ratio = 0.0f;
    f32 slip_angle = 0.0f;
    f32 slide_long = 0.0f;
    f32 slide_lat = 0.0f;
    f32 drive_torque = 0.0f;
    f32 reaction_torque = 0.0f;
    bool grounded = false;
    Vec3 contact_point;
    Vec3 contact_normal;
    Vec3 force_susp;
    Vec3 force_long;
    Vec3 force_lat;
    Vec3 stick_pos;
    bool stick_active = false;
    f32 prev_compression = 0.0f;
    f32 prev_spin_angle = 0.0f;
};

struct VehicleEffects {
    f32 engine_power_mul = 1.0f;
    bool ignition_ok = true;
    f32 tire_grip_mul[kWheelCount] = {1.0f, 1.0f, 1.0f, 1.0f};
    f32 tire_radius_mul[kWheelCount] = {1.0f, 1.0f, 1.0f, 1.0f};
    f32 brake_mul = 1.0f;
    bool headlights_on = false;
    f32 surface_road[kWheelCount] = {1.0f, 1.0f, 1.0f, 1.0f};
    f32 rolling_resist_mul = 1.0f;
};

struct VehicleInput {
    f32 throttle = 0.0f;
    f32 brake = 0.0f;
    f32 steer = 0.0f;
    bool handbrake = false;
};

struct CarWire {
    Vec3 pos{};
    Quat rot = quat_identity();
    Vec3 vel{};
    Vec3 angular_vel{};
    Wheel wheels[kWheelCount];
    Drivetrain train;
    VehicleInput input;
    f32 steer_deg = 0.0f;
};

void drivetrain_init(Drivetrain& train, const VehicleConfig& cfg);
f32 drivetrain_rpm(const Drivetrain& train);
f32 drivetrain_ratio(const Drivetrain& train, const VehicleConfig& cfg);
f32 drivetrain_torque_curve(const VehicleConfig& cfg, f32 rpm);
void drivetrain_request_shift(Drivetrain& train, i32 dir);
void drivetrain_tick(Drivetrain& train, const VehicleConfig& cfg, Wheel* wheels, f32 throttle,
                     f32 power_mul, bool ignition, f32 dt);

Quat wheel_visual_rot(Quat body_rot, const Wheel& wheel, bool right_side);

class Vehicle {
public:
    bool init(PhysWorld& world, Arena& scratch, std::string_view cfg_path, Vec3 pos, f32 yaw);
    void apply_config(PhysWorld& world);
    bool reload_config(PhysWorld& world, Arena& scratch);
    bool poll_config_reload(PhysWorld& world, Arena& scratch);

    void set_input(const VehicleInput& input) { input_ = input; }
    void driver_input(PhysWorld& world, f32 forward_intent, f32 reverse_intent, f32 steer,
                      bool handbrake);
    void tick(PhysWorld& world, f32 dt);
    void reset_contacts();
    void teleport(PhysWorld& world, Vec3 pos, f32 yaw);
    void teleport(PhysWorld& world, Vec3 pos, Quat rot);
    void recover(PhysWorld& world);
    CarWire wire(const PhysWorld& world) const;
    void adopt(PhysWorld& world, const CarWire& wire);

    f32 planar_speed(const PhysWorld& world) const;

    f32 forward_speed(const PhysWorld& world) const;

    BodyHandle body() const { return body_; }
    const BodyDesc& body_desc() const { return body_desc_; }
    BodyDesc make_body_desc() const;
    VehicleConfig& config() { return cfg_; }
    const VehicleConfig& config() const { return cfg_; }
    Wheel& wheel(u32 index) { return wheels_[index]; }
    const Wheel& wheel(u32 index) const { return wheels_[index]; }
    Drivetrain& train() { return train_; }
    const Drivetrain& train() const { return train_; }
    VehicleEffects& effects() { return effects_; }
    const VehicleEffects& effects() const { return effects_; }
    const VehicleInput& input() const { return input_; }
    f32 steer_deg() const { return steer_deg_; }

private:
    void update_steering(f32 speed, f32 slip_deg, f32 dt);
    void probe_suspension(const PhysWorld& world, const RigidBody& body, const Mat3& rot, Vec3 up,
                          u32 probe_count, f32 dt, WheelFrame* frames);
    void compute_arb(f32* arb_force) const;
    void apply_suspension(RigidBody& body, u32 i, const WheelFrame& frame, f32 arb, f32 rebound_mul,
                          f32& total_load);
    void apply_tire(RigidBody& body, u32 i, const WheelFrame& frame, const Mat3& rot, const TireParams& tp,
                    f32 g_mag, f32 tire_load_clamp, f32 nominal_load, f32 wheel_inertia, Vec3 grav_up, f32 dt);
    void integrate_wheels(f32 wheel_inertia, f32 dt);
    void apply_body_drag(RigidBody& body, f32 speed, f32 total_load) const;

    BodyHandle body_;
    BodyDesc body_desc_;
    VehicleConfig cfg_;
    Wheel wheels_[kWheelCount];
    Drivetrain train_;
    VehicleEffects effects_;
    VehicleInput input_;
    f32 steer_deg_ = 0.0f;
    i64 cfg_mtime_ = 0;
    FixedString<128> cfg_path_;
};

}
