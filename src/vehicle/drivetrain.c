#include "vehicle/drivetrain.h"
#include "vehicle/vehicle.h"

#define RPM_TO_RAD (2.0f * PI32 / 60.0f)
#define RAD_TO_RPM (60.0f / (2.0f * PI32))
#define DIFF_COUPLE_NMS 60.0f
#define IDLE_GOVERNOR_GAIN 1.5f
#define IDLE_GOVERNOR_MAX 80.0f
#define SHIFT_LOCKOUT_TIME 0.8f
#define SHIFT_SLIP_GATE 0.4f
#define CLUTCH_LOCK_RADS 40.0f

void drivetrain_init(Drivetrain* train, const VehicleConfig* cfg)
{
    train->engine_omega = cfg->idle_rpm * RPM_TO_RAD;
    train->gear = 1;
    train->shift_timer = 0.0f;
    train->shift_lockout = 0.0f;
    train->shifting = 0;
}

f32 drivetrain_rpm(const Drivetrain* train)
{
    return train->engine_omega * RAD_TO_RPM;
}

f32 drivetrain_ratio(const Drivetrain* train, const VehicleConfig* cfg)
{
    if (train->gear >= 1 && train->gear <= (i32)cfg->gear_count) {
        return cfg->gear_ratios[train->gear - 1] * cfg->final_drive;
    }
    if (train->gear == -1) {
        return -cfg->reverse_ratio * cfg->final_drive;
    }
    return 0.0f;
}

f32 drivetrain_torque_curve(const VehicleConfig* cfg, f32 rpm)
{
    if (rpm <= cfg->torque_rpm[0]) {
        return cfg->torque_nm[0];
    }
    for (u32 i = 1; i < cfg->torque_count; i++) {
        if (rpm <= cfg->torque_rpm[i]) {
            f32 t = (rpm - cfg->torque_rpm[i - 1]) / (cfg->torque_rpm[i] - cfg->torque_rpm[i - 1]);
            return f_lerp(cfg->torque_nm[i - 1], cfg->torque_nm[i], t);
        }
    }
    return cfg->torque_nm[cfg->torque_count - 1];
}

void drivetrain_tick(Drivetrain* train, const VehicleConfig* cfg, struct Wheel* wheels,
                     f32 throttle, f32 power_mul, f32 dt)
{
    f32 rpm = drivetrain_rpm(train);
    f32 idle_omega = cfg->idle_rpm * RPM_TO_RAD;
    f32 max_omega = cfg->max_rpm * RPM_TO_RAD;

    u32 driven_count = 0;
    f32 avg_driven_omega = 0.0f;
    f32 avg_driven_slip = 0.0f;
    for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
        wheels[i].drive_torque = 0.0f;
        if (cfg->wheels[i].driven) {
            avg_driven_omega += wheels[i].omega;
            avg_driven_slip += f_abs(wheels[i].slip_ratio);
            driven_count++;
        }
    }
    if (driven_count) {
        avg_driven_omega /= (f32)driven_count;
        avg_driven_slip /= (f32)driven_count;
    }

    train->shift_lockout = f_max(train->shift_lockout - dt, 0.0f);
    if (train->shifting) {
        train->shift_timer -= dt;
        if (train->shift_timer <= 0.0f) {
            train->shifting = 0;
        }
    } else if (train->gear >= 1 && train->shift_lockout <= 0.0f) {
        f32 synced_rpm = avg_driven_omega * drivetrain_ratio(train, cfg) * RAD_TO_RPM;
        if (synced_rpm > cfg->shift_up_rpm && train->gear < (i32)cfg->gear_count
            && avg_driven_slip < SHIFT_SLIP_GATE) {
            train->gear++;
            train->shifting = 1;
            train->shift_timer = cfg->shift_time;
            train->shift_lockout = SHIFT_LOCKOUT_TIME;
        } else if (synced_rpm < cfg->shift_down_rpm && train->gear > 1) {
            train->gear--;
            train->shifting = 1;
            train->shift_timer = cfg->shift_time;
            train->shift_lockout = SHIFT_LOCKOUT_TIME;
        }
    }

    f32 engine_torque = drivetrain_torque_curve(cfg, rpm) * throttle * power_mul;
    if (train->engine_omega > max_omega) {
        engine_torque = f_min(engine_torque, 0.0f);
    }
    engine_torque -= cfg->engine_brake * f_max(rpm - cfg->idle_rpm, 0.0f) * 0.001f * (1.0f - throttle);
    engine_torque += f_clamp((idle_omega - train->engine_omega) * IDLE_GOVERNOR_GAIN, 0.0f, IDLE_GOVERNOR_MAX);

    f32 ratio = drivetrain_ratio(train, cfg);
    f32 bite_rpm = f_lerp(cfg->idle_rpm * 1.2f, cfg->max_rpm * 0.55f, throttle);
    f32 clutch_engage = f_clamp01((rpm - cfg->idle_rpm * 0.9f) / f_max(bite_rpm - cfg->idle_rpm * 0.9f, 100.0f));
    clutch_engage *= clutch_engage;
    if (train->shifting || ratio == 0.0f) {
        clutch_engage = 0.0f;
    }

    f32 wheel_inertia = f_max(0.5f * cfg->wheel_mass * cfg->wheels[0].radius * cfg->wheels[0].radius, 0.05f);
    f32 wheelside_omega = avg_driven_omega * ratio;
    f32 clutch_slip = train->engine_omega - wheelside_omega;
    b32 locked = driven_count && ratio != 0.0f && clutch_engage >= 0.99f
               && f_abs(clutch_slip) < CLUTCH_LOCK_RADS;

    if (locked) {
        f32 reaction_sum = 0.0f;
        for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
            if (cfg->wheels[i].driven) {
                reaction_sum += wheels[i].reaction_torque;
            }
        }
        f32 inertia_total = (f32)driven_count * wheel_inertia
                          + f_max(cfg->engine_inertia, 0.01f) * ratio * ratio * cfg->driveline_eff;
        f32 wheel_accel = (engine_torque * ratio * cfg->driveline_eff + reaction_sum) / inertia_total;
        for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
            if (!cfg->wheels[i].driven) {
                continue;
            }
            f32 couple = cfg->diff_lock * DIFF_COUPLE_NMS * (avg_driven_omega - wheels[i].omega);
            wheels[i].drive_torque = wheel_inertia * wheel_accel - wheels[i].reaction_torque + couple;
        }
        train->engine_omega = (avg_driven_omega + wheel_accel * dt) * ratio;
        train->engine_omega = f_clamp(train->engine_omega, idle_omega * 0.5f, max_omega * 1.05f);
        return;
    }

    f32 clutch_torque = 0.0f;
    if (clutch_engage > 0.0f && driven_count) {
        clutch_torque = f_clamp(clutch_slip * cfg->clutch_strength,
                                -cfg->clutch_max_torque, cfg->clutch_max_torque) * clutch_engage;
    }

    train->engine_omega += (engine_torque - clutch_torque) / f_max(cfg->engine_inertia, 0.01f) * dt;
    train->engine_omega = f_clamp(train->engine_omega, idle_omega * 0.5f, max_omega * 1.05f);

    if (driven_count && ratio != 0.0f) {
        f32 drive_total = clutch_torque * ratio * cfg->driveline_eff;
        f32 per_wheel = drive_total / (f32)driven_count;
        f32 sync_omega = train->engine_omega / ratio;
        for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
            if (!cfg->wheels[i].driven) {
                continue;
            }
            f32 torque = per_wheel + cfg->diff_lock * DIFF_COUPLE_NMS * (avg_driven_omega - wheels[i].omega);
            f32 sync_step = (sync_omega - wheels[i].omega) * wheel_inertia / dt;
            if (torque > 0.0f) {
                torque = f_min(torque, f_max(sync_step, 0.0f));
            } else {
                torque = f_max(torque, f_min(sync_step, 0.0f));
            }
            wheels[i].drive_torque = torque;
        }
    }
}
