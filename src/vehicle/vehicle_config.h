#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "math/vmath.h"

#include <string_view>

namespace anom {

class Arena;

inline constexpr u32 kWheelCount = 4;
inline constexpr u32 kMaxTorquePoints = 16;
inline constexpr u32 kMaxGears = 8;

enum WheelIndex : u32 {
    WHEEL_FL = 0,
    WHEEL_FR,
    WHEEL_RL,
    WHEEL_RR,
};

struct WheelConfig {
    Vec3 pos{0.78f, -0.25f, 1.25f};
    f32 radius = 0.34f;
    f32 spring_k = 32000.0f;
    f32 damper_c = 2400.0f;
    f32 travel = 0.28f;
    f32 brake_share = 1.0f;
    bool steered = false;
    bool driven = false;
};

struct VehicleConfig {
    f32 mass = 1400.0f;
    Vec3 com_offset{0.0f, -0.25f, -0.1f};
    Vec3 half_extents{0.95f, 0.55f, 2.1f};

    WheelConfig wheels[kWheelCount];
    f32 wheel_mass = 22.0f;

    f32 tire_peak_slip = 0.12f;
    f32 tire_peak_angle_deg = 9.0f;
    f32 tire_peak_mu = 1.05f;
    f32 tire_slide_mu = 0.8f;
    f32 tire_low_speed = 0.6f;
    f32 tire_load_sens = 0.0f;
    f32 tire_relax_long = 0.0f;
    f32 tire_relax_lat = 0.0f;
    f32 tire_pneumatic_trail = 0.030f;
    f32 tire_mech_trail = 0.018f;

    f32 arb_front = 0.0f;
    f32 arb_rear = 0.0f;
    f32 damper_rebound_mul = 1.0f;
    u32 susp_probes = 5;
    f32 bump_stop_zone = 0.14f;
    f32 bump_stop_mul = 10.0f;

    f32 torque_rpm[kMaxTorquePoints]{};
    f32 torque_nm[kMaxTorquePoints]{};
    u32 torque_count = 0;
    f32 engine_inertia = 0.25f;
    f32 idle_rpm = 850.0f;
    f32 max_rpm = 6500.0f;
    f32 engine_brake = 12.0f;

    f32 gear_ratios[kMaxGears]{};
    u32 gear_count = 0;
    f32 reverse_ratio = 3.4f;
    f32 final_drive = 3.9f;
    f32 driveline_eff = 0.9f;
    f32 clutch_strength = 8.0f;
    f32 clutch_max_torque = 450.0f;
    f32 shift_up_rpm = 5800.0f;
    f32 shift_down_rpm = 2200.0f;
    f32 shift_time = 0.35f;
    f32 diff_lock = 0.2f;
    f32 diff_preload = 55.0f;
    f32 diff_power_ramp = 0.40f;
    f32 diff_coast_ramp = 0.18f;

    f32 brake_torque = 1700.0f;
    f32 handbrake_torque = 2500.0f;
    f32 handbrake_grip_mul = 0.85f;

    f32 drag_coef = 0.8f;
    f32 rolling_resist = 0.012f;

    f32 steer_max_deg = 32.0f;
    f32 steer_high_deg = 8.0f;
    f32 steer_high_speed = 40.0f;
    f32 steer_rate_deg = 240.0f;

    Vec3 seat_eye{-0.4f, 0.35f, -0.3f};

    FixedString<32> body_mesh;
    FixedString<32> wheel_mesh;
};

bool vehicle_config_load(VehicleConfig& out, Arena& scratch, std::string_view path);

} // namespace anom
