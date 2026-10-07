#include "vehicle/vehicle.h"

namespace anom {
namespace {

constexpr f32 kRpmToRad = kTau / 60.0f;
constexpr f32 kRadToRpm = 60.0f / kTau;
constexpr f32 kIdleGovernorGain = 1.5f;
constexpr f32 kIdleGovernorMax = 80.0f;
constexpr f32 kShiftLockoutTime = 0.8f;
constexpr f32 kShiftSlipGate = 0.4f;
constexpr f32 kKickdownCeiling = 0.85f;
constexpr f32 kTcAttack = 12.0f;
constexpr f32 kTcRelease = 10.0f;
constexpr f32 kTcSpeedFloor = 8.0f;
constexpr f32 kClutchLockRads = 40.0f;
constexpr f32 kClutchBleedRate = 8.0f;

void axle_lsd(const VehicleConfig& cfg, Wheel* wheels, u32 left, u32 right, f32 wheel_inertia,
              f32 dt)
{
    if (!cfg.wheels[left].driven || !cfg.wheels[right].driven || cfg.diff_lock <= 0.0f) {
        return;
    }
    const Wheel& l = wheels[left];
    const Wheel& r = wheels[right];
    const f32 axle_torque = l.drive_torque + r.drive_torque;
    const f32 ramp = axle_torque >= 0.0f ? cfg.diff_power_ramp : cfg.diff_coast_ramp;
    const f32 capacity = cfg.diff_lock * (cfg.diff_preload + ramp * f_abs(axle_torque));

    const f32 spin_delta = (l.omega - r.omega) * wheel_inertia / f_max(dt, 1e-5f);
    const f32 torque_delta = (l.drive_torque - r.drive_torque)
                           + (l.reaction_torque - r.reaction_torque);
    const f32 transfer = f_clamp(0.5f * (spin_delta + torque_delta), -capacity, capacity);

    wheels[left].drive_torque -= transfer;
    wheels[right].drive_torque += transfer;
}

} // namespace

void drivetrain_init(Drivetrain& train, const VehicleConfig& cfg)
{
    train.engine_omega = cfg.idle_rpm * kRpmToRad;
    train.gear = 1;
    train.shift_timer = 0.0f;
    train.shift_lockout = 0.0f;
    train.shifting = false;
    train.shift_request = 0;
    train.tc_cut = 0.0f;
}

f32 drivetrain_rpm(const Drivetrain& train)
{
    return train.engine_omega * kRadToRpm;
}

void drivetrain_request_shift(Drivetrain& train, i32 dir)
{
    train.shift_request = dir;
}

f32 drivetrain_ratio(const Drivetrain& train, const VehicleConfig& cfg)
{
    if (train.gear >= 1 && train.gear <= static_cast<i32>(cfg.gear_count)) {
        return cfg.gear_ratios[train.gear - 1] * cfg.final_drive;
    }
    if (train.gear == -1) {
        return -cfg.reverse_ratio * cfg.final_drive;
    }
    return 0.0f;
}

f32 drivetrain_torque_curve(const VehicleConfig& cfg, f32 rpm)
{
    if (rpm <= cfg.torque_rpm[0]) {
        return cfg.torque_nm[0];
    }
    for (u32 i = 1; i < cfg.torque_count; i++) {
        if (rpm <= cfg.torque_rpm[i]) {
            const f32 t = (rpm - cfg.torque_rpm[i - 1])
                        / (cfg.torque_rpm[i] - cfg.torque_rpm[i - 1]);
            return f_lerp(cfg.torque_nm[i - 1], cfg.torque_nm[i], t);
        }
    }
    return cfg.torque_nm[cfg.torque_count - 1];
}

void drivetrain_tick(Drivetrain& train, const VehicleConfig& cfg, Wheel* wheels, f32 throttle,
                     f32 power_mul, bool ignition, f32 dt)
{
    if (!ignition) {
        throttle = 0.0f;
    }
    const f32 rpm = drivetrain_rpm(train);
    const f32 idle_omega = cfg.idle_rpm * kRpmToRad;
    const f32 max_omega = cfg.max_rpm * kRpmToRad;

    u32 driven_count = 0;
    f32 avg_driven_omega = 0.0f;
    f32 avg_driven_slip = 0.0f;
    f32 max_drive_spin = 0.0f;
    f32 road_speed = 0.0f;
    for (u32 i = 0; i < kWheelCount; i++) {
        wheels[i].drive_torque = 0.0f;
        if (cfg.wheels[i].driven) {
            avg_driven_omega += wheels[i].omega;
            avg_driven_slip += f_abs(wheels[i].slip_ratio);
            const f32 road = wheels[i].omega * wheels[i].radius - wheels[i].slide_long;
            const f32 spin = wheels[i].slide_long / f_max(f_abs(road), kTcSpeedFloor);
            max_drive_spin = f_max(max_drive_spin, spin * (train.gear < 0 ? -1.0f : 1.0f));
            road_speed += f_abs(road);
            driven_count++;
        }
    }
    if (driven_count) {
        avg_driven_omega /= static_cast<f32>(driven_count);
        avg_driven_slip /= static_cast<f32>(driven_count);
        road_speed /= static_cast<f32>(driven_count);
    }

    train.shift_lockout = f_max(train.shift_lockout - dt, 0.0f);
    if (train.shifting) {
        train.shift_timer -= dt;
        if (train.shift_timer <= 0.0f) {
            train.shifting = false;
        }
    } else if (train.manual) {
        if (train.shift_request != 0) {
            const i32 next = train.gear + train.shift_request;
            if (next >= -1 && next <= static_cast<i32>(cfg.gear_count) && next != train.gear) {
                train.gear = next;
                train.shifting = true;
                train.shift_timer = cfg.shift_time * 0.7f;
            }
            train.shift_request = 0;
        }
    } else if (train.gear >= 1 && train.shift_lockout <= 0.0f) {
        const f32 synced_rpm = avg_driven_omega * drivetrain_ratio(train, cfg) * kRadToRpm;
        const f32 up_rpm = f_lerp(cfg.shift_up_rpm * 0.55f, cfg.shift_up_rpm, throttle);
        if (synced_rpm > up_rpm && train.gear < static_cast<i32>(cfg.gear_count)
            && avg_driven_slip < kShiftSlipGate) {
            train.gear++;
            train.shifting = true;
            train.shift_timer = cfg.shift_time;
            train.shift_lockout = kShiftLockoutTime;
        } else if (synced_rpm < cfg.shift_down_rpm && train.gear > 1) {
            train.gear--;
            train.shifting = true;
            train.shift_timer = cfg.shift_time;
            train.shift_lockout = kShiftLockoutTime;
        } else if (throttle > 0.85f && train.gear > 1
                   && synced_rpm < cfg.shift_down_rpm * 1.9f
                   && synced_rpm * cfg.gear_ratios[train.gear - 2] / cfg.gear_ratios[train.gear - 1]
                          < cfg.shift_up_rpm * kKickdownCeiling) {
            train.gear--;
            train.shifting = true;
            train.shift_timer = cfg.shift_time;
            train.shift_lockout = kShiftLockoutTime;
        }
    } else {
        train.shift_request = 0;
    }

    const f32 tc_start = cfg.tire_peak_slip * cfg.tc_slip;
    const f32 fade = f_clamp01((road_speed - cfg.tc_fade_start) / f_max(cfg.tc_fade_full - cfg.tc_fade_start, 0.01f));
    const f32 tc_strength = f_lerp(cfg.tc_low, cfg.tc_strength, fade * fade * (3.0f - 2.0f * fade));
    const f32 tc_target = tc_strength
                        * f_clamp01((max_drive_spin - tc_start) / f_max(tc_start * 2.0f, 0.01f));
    const f32 tc_rate = tc_target > train.tc_cut ? kTcAttack : kTcRelease;
    train.tc_cut = f_move_toward(train.tc_cut, tc_target, tc_rate * dt);

    f32 engine_torque = drivetrain_torque_curve(cfg, rpm) * throttle * power_mul
                      * (1.0f - train.tc_cut);
    if (train.engine_omega > max_omega) {
        engine_torque = f_min(engine_torque, 0.0f);
    }
    engine_torque -= cfg.engine_brake * f_max(rpm - cfg.idle_rpm, 0.0f) * 0.001f
                   * (1.0f - throttle);
    if (ignition) {
        engine_torque += f_clamp((idle_omega - train.engine_omega) * kIdleGovernorGain, 0.0f,
                                 kIdleGovernorMax);
    } else {
        engine_torque -= cfg.engine_brake * 0.004f * rpm;
    }
    const f32 min_omega = ignition ? idle_omega * 0.5f : 0.0f;

    const f32 ratio = drivetrain_ratio(train, cfg);
    const f32 bite_rpm = f_lerp(cfg.idle_rpm * 1.2f, cfg.max_rpm * 0.55f, throttle);
    f32 clutch_engage = f_clamp01((rpm - cfg.idle_rpm * 0.9f)
                                  / f_max(bite_rpm - cfg.idle_rpm * 0.9f, 100.0f));
    clutch_engage *= clutch_engage;
    if (train.shifting || ratio == 0.0f || train.brake_hold || train.declutch) {
        clutch_engage = 0.0f;
    }

    const f32 wheel_inertia = f_max(0.5f * cfg.wheel_mass * cfg.wheels[0].radius
                                        * cfg.wheels[0].radius,
                                    0.05f);
    const f32 wheelside_omega = avg_driven_omega * ratio;
    const f32 clutch_slip = train.engine_omega - wheelside_omega;
    const bool locked = driven_count && ratio != 0.0f && clutch_engage >= 0.99f
                     && f_abs(clutch_slip) < kClutchLockRads;

    if (locked) {
        f32 reaction_sum = 0.0f;
        for (u32 i = 0; i < kWheelCount; i++) {
            if (cfg.wheels[i].driven) {
                reaction_sum += wheels[i].reaction_torque;
            }
        }
        const f32 inertia_total = static_cast<f32>(driven_count) * wheel_inertia
                                + f_max(cfg.engine_inertia, 0.01f) * ratio * ratio
                                      * cfg.driveline_eff;
        const f32 wheel_accel = (engine_torque * ratio * cfg.driveline_eff + reaction_sum)
                              / inertia_total;
        const f32 carrier_torque = static_cast<f32>(driven_count) * wheel_inertia * wheel_accel
                                 - reaction_sum;
        const f32 share = carrier_torque / static_cast<f32>(driven_count);
        for (u32 i = 0; i < kWheelCount; i++) {
            if (!cfg.wheels[i].driven) {
                continue;
            }
            wheels[i].drive_torque = share;
        }
        axle_lsd(cfg, wheels, WHEEL_FL, WHEEL_FR, wheel_inertia, dt);
        axle_lsd(cfg, wheels, WHEEL_RL, WHEEL_RR, wheel_inertia, dt);
        train.engine_omega = (avg_driven_omega + wheel_accel * dt) * ratio;
        train.engine_omega = f_clamp(train.engine_omega, min_omega, max_omega * 1.05f);
        return;
    }

    f32 clutch_torque = 0.0f;
    if (clutch_engage > 0.0f && driven_count) {
        clutch_torque = f_clamp(clutch_slip * cfg.clutch_strength, -cfg.clutch_max_torque,
                                cfg.clutch_max_torque)
                      * clutch_engage;
        if (clutch_slip > 0.0f) {
            const f32 bite_omega = bite_rpm * kRpmToRad;
            const f32 bleed = f_max(train.engine_omega - bite_omega, 0.0f)
                            * f_max(cfg.engine_inertia, 0.01f) * kClutchBleedRate;
            const f32 cap = f_max(engine_torque, 0.0f) + bleed + cfg.clutch_creep_torque;
            clutch_torque = f_min(clutch_torque, cap);
        }
    }

    train.engine_omega += (engine_torque - clutch_torque) / f_max(cfg.engine_inertia, 0.01f) * dt;
    train.engine_omega = f_clamp(train.engine_omega, min_omega, max_omega * 1.05f);

    if (driven_count && ratio != 0.0f) {
        const f32 drive_total = clutch_torque * ratio * cfg.driveline_eff;
        const f32 per_wheel = drive_total / static_cast<f32>(driven_count);
        const f32 sync_omega = train.engine_omega / ratio;
        for (u32 i = 0; i < kWheelCount; i++) {
            if (!cfg.wheels[i].driven) {
                continue;
            }
            f32 torque = per_wheel;
            const f32 sync_step = (sync_omega - wheels[i].omega) * wheel_inertia / dt;
            if (torque > 0.0f) {
                torque = f_min(torque, f_max(sync_step, 0.0f));
            } else {
                torque = f_max(torque, f_min(sync_step, 0.0f));
            }
            wheels[i].drive_torque = torque;
        }
        axle_lsd(cfg, wheels, WHEEL_FL, WHEEL_FR, wheel_inertia, dt);
        axle_lsd(cfg, wheels, WHEEL_RL, WHEEL_RR, wheel_inertia, dt);
    }
}

} // namespace anom
