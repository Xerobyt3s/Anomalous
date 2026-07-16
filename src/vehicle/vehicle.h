#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "physics/physics.h"
#include "vehicle/vehicle_config.h"
#include "vehicle/drivetrain.h"

struct PhysWorld;

typedef struct VehicleEffects {
    f32 engine_power_mul;
    b32 ignition_ok;
    f32 tire_grip_mul[VEHICLE_WHEEL_COUNT];
    f32 tire_radius_mul[VEHICLE_WHEEL_COUNT];
    f32 brake_mul;
    b32 headlights_on;
} VehicleEffects;

typedef struct VehicleInput {
    f32 throttle;
    f32 brake;
    f32 steer;
    b32 handbrake;
} VehicleInput;

typedef struct Wheel {
    Vec3 attach_local;
    f32 radius;
    f32 steer_rad;
    f32 omega;
    f32 spin_angle;
    f32 compression;
    f32 load;
    f32 slip_ratio;
    f32 slip_angle;
    f32 drive_torque;
    f32 reaction_torque;
    b32 grounded;
    Vec3 contact_point;
    Vec3 contact_normal;
    Vec3 force_susp;
    Vec3 force_long;
    Vec3 force_lat;
    Vec3 stick_pos;
    b32 stick_active;
} Wheel;

typedef struct Vehicle {
    BodyHandle body;
    VehicleConfig cfg;
    Wheel wheels[VEHICLE_WHEEL_COUNT];
    Drivetrain train;
    VehicleEffects effects;
    VehicleInput input;
    f32 steer_deg;
    char cfg_path[128];
    i64 cfg_mtime;
} Vehicle;

b32  vehicle_init(Vehicle* v, struct PhysWorld* world, const char* cfg_path, Vec3 pos, f32 yaw);
void vehicle_apply_config(Vehicle* v, struct PhysWorld* world);
void vehicle_poll_config_reload(Vehicle* v, struct PhysWorld* world);
void vehicle_set_input(Vehicle* v, VehicleInput input);
void vehicle_driver_input(Vehicle* v, struct PhysWorld* world, f32 forward_intent, f32 reverse_intent, f32 steer, b32 handbrake);
void vehicle_tick(Vehicle* v, struct PhysWorld* world, f32 dt);
void vehicle_teleport(Vehicle* v, struct PhysWorld* world, Vec3 pos, f32 yaw);
f32  vehicle_forward_speed(const Vehicle* v, struct PhysWorld* world);
void vehicle_debug_draw(Vehicle* v, struct PhysWorld* world, f32 alpha, b32 detail);
