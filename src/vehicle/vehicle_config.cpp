#include "vehicle/vehicle_config.h"

#include <cstdio>
#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "platform/filesystem.h"

namespace anom {
namespace {

void load_wheel(WheelConfig& wheel, const Config& cfg, std::string_view section,
                const WheelConfig& fallback)
{
    FixedString<96> key;
    const auto make = [&](const char* suffix) {
        key.format("%.*s.%s", static_cast<int>(section.size()), section.data(), suffix);
        return key.view();
    };

    wheel.pos = cfg.get_vec3(make("pos"), fallback.pos);
    wheel.radius = cfg.get_f32(make("radius"), fallback.radius);
    wheel.spring_k = cfg.get_f32(make("spring"), fallback.spring_k);
    wheel.damper_c = cfg.get_f32(make("damper"), fallback.damper_c);
    wheel.travel = cfg.get_f32(make("travel"), fallback.travel);
    wheel.brake_share = cfg.get_f32(make("brake_share"), fallback.brake_share);
    wheel.steered = cfg.get_i32(make("steered"), fallback.steered ? 1 : 0) != 0;
    wheel.driven = cfg.get_i32(make("driven"), fallback.driven ? 1 : 0) != 0;
}

} // namespace

bool vehicle_config_load(VehicleConfig& out, Arena& scratch, std::string_view path)
{
    ArenaScope scope(scratch);
    const fs::FileData file = fs::read_entire_file(scratch, path);
    if (!file.valid()) {
        return false;
    }
    Config cfg;
    if (!cfg.parse(scratch, file.text())) {
        return false;
    }

    out = VehicleConfig{};
    out.mass = cfg.get_f32("body.mass", 1400.0f);
    out.com_offset = cfg.get_vec3("body.com_offset", Vec3{0.0f, -0.25f, -0.1f});
    out.half_extents = cfg.get_vec3("body.half_extents", Vec3{0.95f, 0.55f, 2.1f});

    const WheelConfig fallback;
    load_wheel(out.wheels[WHEEL_FL], cfg, "wheel_fl", fallback);
    load_wheel(out.wheels[WHEEL_FR], cfg, "wheel_fr", fallback);
    load_wheel(out.wheels[WHEEL_RL], cfg, "wheel_rl", fallback);
    load_wheel(out.wheels[WHEEL_RR], cfg, "wheel_rr", fallback);
    out.wheel_mass = cfg.get_f32("body.wheel_mass", 22.0f);

    out.tire_peak_slip = cfg.get_f32("tire.peak_slip", 0.12f);
    out.tire_peak_angle_deg = cfg.get_f32("tire.peak_angle_deg", 9.0f);
    out.tire_peak_mu = cfg.get_f32("tire.peak_mu", 1.05f);
    out.tire_slide_mu = cfg.get_f32("tire.slide_mu", 0.8f);
    out.tire_low_speed = cfg.get_f32("tire.low_speed", 0.6f);
    out.tire_grass_grip = cfg.get_f32("tire.grass_grip", 0.80f);
    out.climb_grip = f_max(cfg.get_f32("tire.climb_grip", 1.0f), 1.0f);
    out.climb_slope_start = cfg.get_f32("tire.climb_slope_start", 12.0f);
    out.climb_slope_full = cfg.get_f32("tire.climb_slope_full", 25.0f);
    out.climb_speed = f_max(cfg.get_f32("tire.climb_speed", 6.0f), 0.1f);
    out.tire_load_sens = cfg.get_f32("tire.load_sensitivity", 0.0f);
    out.tire_relax_long = cfg.get_f32("tire.relax_long", 0.0f);
    out.tire_relax_lat = cfg.get_f32("tire.relax_lat", 0.0f);
    out.tire_pneumatic_trail = cfg.get_f32("tire.pneumatic_trail", 0.030f);
    out.tire_mech_trail = cfg.get_f32("tire.mech_trail", 0.018f);

    out.arb_front = cfg.get_f32("suspension.arb_front", 0.0f);
    out.arb_rear = cfg.get_f32("suspension.arb_rear", 0.0f);
    out.damper_rebound_mul = cfg.get_f32("suspension.rebound_mul", 1.0f);
    out.susp_probes = static_cast<u32>(f_clamp(cfg.get_f32("suspension.probes", 5.0f), 1.0f, 7.0f));
    out.bump_stop_zone = f_clamp(cfg.get_f32("suspension.bump_stop_zone", 0.14f), 0.02f, 0.5f);
    out.bump_stop_mul = f_max(cfg.get_f32("suspension.bump_stop_mul", 10.0f), 0.0f);

    f32 curve[kMaxTorquePoints * 2];
    const u32 pair_values = cfg.get_f32_list("engine.torque_curve", curve);
    out.torque_count = pair_values / 2;
    for (u32 i = 0; i < out.torque_count; i++) {
        out.torque_rpm[i] = curve[i * 2];
        out.torque_nm[i] = curve[i * 2 + 1];
    }
    if (out.torque_count < 2) {
        out.torque_count = 2;
        out.torque_rpm[0] = 1000.0f;
        out.torque_nm[0] = 150.0f;
        out.torque_rpm[1] = 6000.0f;
        out.torque_nm[1] = 200.0f;
    }
    out.engine_inertia = cfg.get_f32("engine.inertia", 0.25f);
    out.idle_rpm = cfg.get_f32("engine.idle_rpm", 850.0f);
    out.max_rpm = cfg.get_f32("engine.max_rpm", 6500.0f);
    out.engine_brake = cfg.get_f32("engine.engine_brake", 12.0f);

    out.gear_count = cfg.get_f32_list("gearbox.ratios", out.gear_ratios);
    if (out.gear_count == 0) {
        out.gear_count = 5;
        out.gear_ratios[0] = 3.6f;
        out.gear_ratios[1] = 2.1f;
        out.gear_ratios[2] = 1.4f;
        out.gear_ratios[3] = 1.03f;
        out.gear_ratios[4] = 0.82f;
    }
    out.reverse_ratio = cfg.get_f32("gearbox.reverse_ratio", 3.4f);
    out.final_drive = cfg.get_f32("gearbox.final_drive", 3.9f);
    out.driveline_eff = cfg.get_f32("gearbox.efficiency", 0.9f);
    out.clutch_strength = cfg.get_f32("gearbox.clutch_strength", 8.0f);
    out.clutch_max_torque = cfg.get_f32("gearbox.clutch_max_torque", 450.0f);
    out.clutch_creep_torque = cfg.get_f32("gearbox.clutch_creep_torque", 15.0f);
    out.shift_up_rpm = cfg.get_f32("gearbox.shift_up_rpm", 5800.0f);
    out.shift_down_rpm = cfg.get_f32("gearbox.shift_down_rpm", 2200.0f);
    out.shift_time = cfg.get_f32("gearbox.shift_time", 0.35f);
    out.diff_lock = f_clamp01(cfg.get_f32("gearbox.diff_lock", 0.2f));
    out.diff_preload = f_max(cfg.get_f32("gearbox.diff_preload", 55.0f), 0.0f);
    out.diff_power_ramp = f_max(cfg.get_f32("gearbox.diff_power_ramp", 0.40f), 0.0f);
    out.diff_coast_ramp = f_max(cfg.get_f32("gearbox.diff_coast_ramp", 0.18f), 0.0f);
    out.tc_strength = f_clamp01(cfg.get_f32("gearbox.traction_control", 0.0f));
    out.tc_slip = f_max(cfg.get_f32("gearbox.traction_slip", 1.3f), 0.1f);

    out.brake_torque = cfg.get_f32("brakes.torque", 1700.0f);
    out.handbrake_torque = cfg.get_f32("brakes.handbrake_torque", 2500.0f);
    out.handbrake_grip_mul = cfg.get_f32("brakes.handbrake_grip_mul", 0.85f);
    out.park_latch_speed = f_max(cfg.get_f32("brakes.park_latch_speed", 2.0f), 0.0f);

    out.drag_coef = cfg.get_f32("aero.drag_coef", 0.8f);
    out.rolling_resist = cfg.get_f32("aero.rolling_resist", 0.012f);
    out.offroad_rolling_mul = cfg.get_f32("aero.offroad_rolling_mul", 2.2f);

    out.steer_max_deg = cfg.get_f32("steering.max_deg", 32.0f);
    out.steer_high_deg = cfg.get_f32("steering.high_speed_deg", 8.0f);
    out.steer_high_speed = cfg.get_f32("steering.high_speed", 40.0f);
    out.steer_rate_deg = cfg.get_f32("steering.rate_deg", 240.0f);
    out.steer_time_in = cfg.get_f32("steering.time_in", 0.16f);
    out.steer_time_in_high = cfg.get_f32("steering.time_in_high", 0.45f);
    out.steer_time_out = cfg.get_f32("steering.time_out", 0.09f);

    out.seat_eye = cfg.get_vec3("cabin.seat_eye", Vec3{-0.4f, 0.35f, -0.3f});
    out.seat_hips = cfg.get_vec3("cabin.seat_hips", out.seat_eye - Vec3{0.0f, 0.62f, -0.05f});
    out.pedals = cfg.get_vec3("cabin.pedals", out.seat_hips + Vec3{0.0f, -0.18f, -0.38f});
    out.wheel_center = cfg.get_vec3("cabin.wheel_center", out.seat_eye + Vec3{0.0f, -0.27f, -0.23f});
    out.wheel_normal = normalize(cfg.get_vec3("cabin.wheel_normal", Vec3{0.0f, 0.4f, 0.92f}));
    out.wheel_radius = cfg.get_f32("cabin.wheel_radius", 0.18f);
    out.shifter = cfg.get_vec3("cabin.shifter", Vec3{0.0f, 0.1f, 0.11f});
    out.seat_count = 1;
    SeatConfig& driver = out.seats[0];
    driver.eye = out.seat_eye;
    driver.hips = out.seat_hips;
    driver.feet = out.pedals;
    driver.door_side = out.seat_eye.x < 0.0f ? 0 : 1;
    driver.drives = true;
    const i32 declared = cfg.get_i32("seats.count", 1);
    for (u32 i = 1; i < kMaxSeats && static_cast<i32>(i) < declared; i++) {
        char key[64];
        SeatConfig& seat = out.seats[i];
        std::snprintf(key, sizeof(key), "seats.seat%u_eye", i);
        seat.eye = cfg.get_vec3(key, Vec3{-driver.eye.x, driver.eye.y, driver.eye.z});
        std::snprintf(key, sizeof(key), "seats.seat%u_hips", i);
        seat.hips = cfg.get_vec3(key, Vec3{-driver.hips.x, driver.hips.y, driver.hips.z});
        std::snprintf(key, sizeof(key), "seats.seat%u_feet", i);
        seat.feet = cfg.get_vec3(key, Vec3{-driver.feet.x, driver.feet.y, driver.feet.z});
        std::snprintf(key, sizeof(key), "seats.seat%u_window_cos", i);
        seat.window_cos = cfg.get_f32(key, 0.3f);
        seat.door_side = seat.eye.x < 0.0f ? 0 : 1;
        seat.drives = false;
        std::snprintf(key, sizeof(key), "seats.seat%u_blocked_by", i);
        const ItemKind blocker = item_from_id(cfg.get_str(key, ""));
        seat.blocked_by = PART_COUNT;
        for (u32 k = 0; k < PART_COUNT; k++) {
            if (blocker != ITEM_NONE && item_for_part(static_cast<PartKind>(k)) == blocker) {
                seat.blocked_by = static_cast<PartKind>(k);
            }
        }
        out.seat_count = i + 1;
    }
    out.synth.print_time = cfg.get_f32("synth.print_time", 3.0f);
    out.synth.battery_per_round = cfg.get_f32("synth.battery_per_round", 0.01f);
    out.synth.min_battery = cfg.get_f32("synth.min_battery", 0.15f);
    out.synth.tray_max = static_cast<u32>(cfg.get_i32("synth.tray_max", 24));
    out.synth.tank_capacity = static_cast<u32>(cfg.get_i32("synth.tank_capacity", 60));
    out.synth.start_each = static_cast<u32>(std::max(cfg.get_i32("synth.start_each", 0), 0));
    out.synth.start_propellant = static_cast<u32>(std::max(cfg.get_i32("synth.start_propellant", 0), 0));

    out.body_mesh.assign(cfg.get_str("render.body_mesh", ""));
    out.wheel_mesh.assign(cfg.get_str("render.wheel_mesh", ""));
    return true;
}

} // namespace anom
