#include "vehicle/vehicle_config.h"
#include "core/config.h"
#include "core/arena.h"
#include "core/log.h"
#include "platform/platform.h"

#include <stdio.h>

static void wheel_load(WheelConfig* wheel, const Config* cfg, const char* section, const WheelConfig* fallback)
{
    char key[96];
    snprintf(key, sizeof(key), "%s.pos", section);
    wheel->pos = config_get_vec3(cfg, key, fallback->pos);
    snprintf(key, sizeof(key), "%s.radius", section);
    wheel->radius = config_get_f32(cfg, key, fallback->radius);
    snprintf(key, sizeof(key), "%s.spring", section);
    wheel->spring_k = config_get_f32(cfg, key, fallback->spring_k);
    snprintf(key, sizeof(key), "%s.damper", section);
    wheel->damper_c = config_get_f32(cfg, key, fallback->damper_c);
    snprintf(key, sizeof(key), "%s.travel", section);
    wheel->travel = config_get_f32(cfg, key, fallback->travel);
    snprintf(key, sizeof(key), "%s.brake_share", section);
    wheel->brake_share = config_get_f32(cfg, key, fallback->brake_share);
    snprintf(key, sizeof(key), "%s.steered", section);
    wheel->steered = config_get_i32(cfg, key, fallback->steered) != 0;
    snprintf(key, sizeof(key), "%s.driven", section);
    wheel->driven = config_get_i32(cfg, key, fallback->driven) != 0;
}

b32 vehicle_config_load(VehicleConfig* out, const char* path)
{
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    FileData file = platform_read_entire_file(&g_frame_arena, path);
    if (!file.data) {
        arena_temp_end(temp);
        return 0;
    }
    Config cfg;
    if (!config_parse(&cfg, &g_frame_arena, (const char*)file.data)) {
        arena_temp_end(temp);
        return 0;
    }

    out->mass = config_get_f32(&cfg, "body.mass", 1400.0f);
    out->com_offset = config_get_vec3(&cfg, "body.com_offset", v3(0.0f, -0.25f, -0.1f));
    out->half_extents = config_get_vec3(&cfg, "body.half_extents", v3(0.95f, 0.55f, 2.1f));

    WheelConfig fallback = {0};
    fallback.pos = v3(0.78f, -0.25f, 1.25f);
    fallback.radius = 0.34f;
    fallback.spring_k = 32000.0f;
    fallback.damper_c = 2400.0f;
    fallback.travel = 0.28f;
    fallback.brake_share = 1.0f;
    wheel_load(&out->wheels[WHEEL_FL], &cfg, "wheel_fl", &fallback);
    wheel_load(&out->wheels[WHEEL_FR], &cfg, "wheel_fr", &fallback);
    wheel_load(&out->wheels[WHEEL_RL], &cfg, "wheel_rl", &fallback);
    wheel_load(&out->wheels[WHEEL_RR], &cfg, "wheel_rr", &fallback);
    out->wheel_mass = config_get_f32(&cfg, "body.wheel_mass", 22.0f);

    out->tire_peak_slip = config_get_f32(&cfg, "tire.peak_slip", 0.12f);
    out->tire_peak_angle_deg = config_get_f32(&cfg, "tire.peak_angle_deg", 9.0f);
    out->tire_peak_mu = config_get_f32(&cfg, "tire.peak_mu", 1.05f);
    out->tire_slide_mu = config_get_f32(&cfg, "tire.slide_mu", 0.8f);
    out->tire_low_speed = config_get_f32(&cfg, "tire.low_speed", 0.6f);
    out->tire_load_sens = config_get_f32(&cfg, "tire.load_sensitivity", 0.0f);
    out->tire_relax_long = config_get_f32(&cfg, "tire.relax_long", 0.0f);
    out->tire_relax_lat = config_get_f32(&cfg, "tire.relax_lat", 0.0f);

    out->arb_front = config_get_f32(&cfg, "suspension.arb_front", 0.0f);
    out->arb_rear = config_get_f32(&cfg, "suspension.arb_rear", 0.0f);
    out->damper_rebound_mul = config_get_f32(&cfg, "suspension.rebound_mul", 1.0f);

    f32 curve[VEHICLE_MAX_TORQUE_POINTS * 2];
    u32 pair_values = config_get_f32_list(&cfg, "engine.torque_curve", curve, VEHICLE_MAX_TORQUE_POINTS * 2);
    out->torque_count = pair_values / 2;
    for (u32 i = 0; i < out->torque_count; i++) {
        out->torque_rpm[i] = curve[i * 2];
        out->torque_nm[i] = curve[i * 2 + 1];
    }
    if (out->torque_count < 2) {
        out->torque_count = 2;
        out->torque_rpm[0] = 1000.0f; out->torque_nm[0] = 150.0f;
        out->torque_rpm[1] = 6000.0f; out->torque_nm[1] = 200.0f;
    }
    out->engine_inertia = config_get_f32(&cfg, "engine.inertia", 0.25f);
    out->idle_rpm = config_get_f32(&cfg, "engine.idle_rpm", 850.0f);
    out->max_rpm = config_get_f32(&cfg, "engine.max_rpm", 6500.0f);
    out->engine_brake = config_get_f32(&cfg, "engine.engine_brake", 12.0f);

    out->gear_count = config_get_f32_list(&cfg, "gearbox.ratios", out->gear_ratios, VEHICLE_MAX_GEARS);
    if (out->gear_count == 0) {
        out->gear_count = 5;
        out->gear_ratios[0] = 3.6f;
        out->gear_ratios[1] = 2.1f;
        out->gear_ratios[2] = 1.4f;
        out->gear_ratios[3] = 1.03f;
        out->gear_ratios[4] = 0.82f;
    }
    out->reverse_ratio = config_get_f32(&cfg, "gearbox.reverse_ratio", 3.4f);
    out->final_drive = config_get_f32(&cfg, "gearbox.final_drive", 3.9f);
    out->driveline_eff = config_get_f32(&cfg, "gearbox.efficiency", 0.9f);
    out->clutch_strength = config_get_f32(&cfg, "gearbox.clutch_strength", 8.0f);
    out->clutch_max_torque = config_get_f32(&cfg, "gearbox.clutch_max_torque", 450.0f);
    out->shift_up_rpm = config_get_f32(&cfg, "gearbox.shift_up_rpm", 5800.0f);
    out->shift_down_rpm = config_get_f32(&cfg, "gearbox.shift_down_rpm", 2200.0f);
    out->shift_time = config_get_f32(&cfg, "gearbox.shift_time", 0.35f);
    out->diff_lock = config_get_f32(&cfg, "gearbox.diff_lock", 0.2f);

    out->brake_torque = config_get_f32(&cfg, "brakes.torque", 1700.0f);
    out->handbrake_torque = config_get_f32(&cfg, "brakes.handbrake_torque", 2500.0f);
    out->handbrake_grip_mul = config_get_f32(&cfg, "brakes.handbrake_grip_mul", 0.85f);

    out->drag_coef = config_get_f32(&cfg, "aero.drag_coef", 0.8f);
    out->rolling_resist = config_get_f32(&cfg, "aero.rolling_resist", 0.012f);

    out->steer_max_deg = config_get_f32(&cfg, "steering.max_deg", 32.0f);
    out->steer_high_deg = config_get_f32(&cfg, "steering.high_speed_deg", 8.0f);
    out->steer_high_speed = config_get_f32(&cfg, "steering.high_speed", 40.0f);
    out->steer_rate_deg = config_get_f32(&cfg, "steering.rate_deg", 240.0f);

    out->seat_eye = config_get_vec3(&cfg, "cabin.seat_eye", v3(-0.4f, 0.35f, -0.3f));

    snprintf(out->body_mesh, sizeof(out->body_mesh), "%s",
             config_get_str(&cfg, "render.body_mesh", ""));
    snprintf(out->wheel_mesh, sizeof(out->wheel_mesh), "%s",
             config_get_str(&cfg, "render.wheel_mesh", ""));

    arena_temp_end(temp);
    return 1;
}
