#include "vehicle/vehicle_config.h"
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
    out.tire_load_sens = cfg.get_f32("tire.load_sensitivity", 0.0f);
    out.tire_relax_long = cfg.get_f32("tire.relax_long", 0.0f);
    out.tire_relax_lat = cfg.get_f32("tire.relax_lat", 0.0f);

    out.arb_front = cfg.get_f32("suspension.arb_front", 0.0f);
    out.arb_rear = cfg.get_f32("suspension.arb_rear", 0.0f);
    out.damper_rebound_mul = cfg.get_f32("suspension.rebound_mul", 1.0f);

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
    out.shift_up_rpm = cfg.get_f32("gearbox.shift_up_rpm", 5800.0f);
    out.shift_down_rpm = cfg.get_f32("gearbox.shift_down_rpm", 2200.0f);
    out.shift_time = cfg.get_f32("gearbox.shift_time", 0.35f);
    out.diff_lock = cfg.get_f32("gearbox.diff_lock", 0.2f);

    out.brake_torque = cfg.get_f32("brakes.torque", 1700.0f);
    out.handbrake_torque = cfg.get_f32("brakes.handbrake_torque", 2500.0f);
    out.handbrake_grip_mul = cfg.get_f32("brakes.handbrake_grip_mul", 0.85f);

    out.drag_coef = cfg.get_f32("aero.drag_coef", 0.8f);
    out.rolling_resist = cfg.get_f32("aero.rolling_resist", 0.012f);

    out.steer_max_deg = cfg.get_f32("steering.max_deg", 32.0f);
    out.steer_high_deg = cfg.get_f32("steering.high_speed_deg", 8.0f);
    out.steer_high_speed = cfg.get_f32("steering.high_speed", 40.0f);
    out.steer_rate_deg = cfg.get_f32("steering.rate_deg", 240.0f);

    out.seat_eye = cfg.get_vec3("cabin.seat_eye", Vec3{-0.4f, 0.35f, -0.3f});

    out.body_mesh.assign(cfg.get_str("render.body_mesh", ""));
    out.wheel_mesh.assign(cfg.get_str("render.wheel_mesh", ""));
    return true;
}

} // namespace anom
