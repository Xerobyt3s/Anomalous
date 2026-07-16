#pragma once

#include "core/types.h"
#include "math/vmath.h"

#define VEHICLE_WHEEL_COUNT 4
#define VEHICLE_MAX_TORQUE_POINTS 16
#define VEHICLE_MAX_GEARS 8

typedef enum WheelIndex {
    WHEEL_FL,
    WHEEL_FR,
    WHEEL_RL,
    WHEEL_RR,
} WheelIndex;

typedef struct WheelConfig {
    Vec3 pos;
    f32 radius;
    f32 spring_k;
    f32 damper_c;
    f32 travel;
    f32 brake_share;
    b32 steered;
    b32 driven;
} WheelConfig;

typedef struct VehicleConfig {
    f32 mass;
    Vec3 com_offset;
    Vec3 half_extents;

    WheelConfig wheels[VEHICLE_WHEEL_COUNT];
    f32 wheel_mass;

    f32 tire_peak_slip;
    f32 tire_peak_angle_deg;
    f32 tire_peak_mu;
    f32 tire_slide_mu;
    f32 tire_low_speed;

    f32 torque_rpm[VEHICLE_MAX_TORQUE_POINTS];
    f32 torque_nm[VEHICLE_MAX_TORQUE_POINTS];
    u32 torque_count;
    f32 engine_inertia;
    f32 idle_rpm;
    f32 max_rpm;
    f32 engine_brake;

    f32 gear_ratios[VEHICLE_MAX_GEARS];
    u32 gear_count;
    f32 reverse_ratio;
    f32 final_drive;
    f32 driveline_eff;
    f32 clutch_strength;
    f32 clutch_max_torque;
    f32 shift_up_rpm;
    f32 shift_down_rpm;
    f32 shift_time;
    f32 diff_lock;

    f32 brake_torque;
    f32 handbrake_torque;
    f32 handbrake_grip_mul;

    f32 drag_coef;
    f32 rolling_resist;

    f32 steer_max_deg;
    f32 steer_high_deg;
    f32 steer_high_speed;
    f32 steer_rate_deg;

    Vec3 seat_eye;
} VehicleConfig;

b32 vehicle_config_load(VehicleConfig* cfg, const char* path);
