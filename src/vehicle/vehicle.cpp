#include "vehicle/vehicle.h"
#include "core/arena.h"
#include "core/log.h"
#include "physics/world.h"
#include "vehicle/tire.h"

namespace anom {
namespace {

constexpr f32 kGravity = 9.81f;
constexpr f32 kDamperVelClamp = 3.0f;
constexpr f32 kSlipDenomMin = 2.0f;
constexpr f32 kSlipAngleDenomMin = 0.8f;
constexpr f32 kTireLoadClampFrac = 0.75f;
constexpr f32 kStickStiffness = 200000.0f;
constexpr f32 kStickMinMass = 40.0f;
constexpr u32 kMaxSuspProbes = 7;
constexpr f32 kProbeSpread = 0.85f;
constexpr f32 kProbeNormalBand = 0.02f;
constexpr f32 kChassisClearance = 0.02f;

} // namespace

void Vehicle::apply_config(PhysWorld& world)
{
    RigidBody* body = world.body(body_);
    if (!body) {
        return;
    }

    body->inv_mass = 1.0f / cfg_.mass;
    const f32 hx = cfg_.half_extents.x;
    const f32 hy = cfg_.half_extents.y;
    const f32 hz = cfg_.half_extents.z;
    const f32 ix = cfg_.mass / 3.0f * (hy * hy + hz * hz);
    const f32 iy = cfg_.mass / 3.0f * (hx * hx + hz * hz);
    const f32 iz = cfg_.mass / 3.0f * (hx * hx + hy * hy);
    body->inv_inertia_local = mat3_diag(1.0f / ix, 1.0f / iy, 1.0f / iz);
    body->half_extents = cfg_.half_extents;

    const f32 min_half = f_min(hx, f_min(hy, hz));
    const f32 r = f_max(min_half * 0.4f, 0.05f);
    body->sphere_radius = r;
    body->sphere_count = 8;
    const f32 ox = f_max(hx - r, 0.0f);
    const f32 oy = f_max(hy - r, 0.0f);
    const f32 oz = f_max(hz - r, 0.0f);

    f32 lowest_tread = 0.0f;
    for (u32 i = 0; i < kWheelCount; i++) {
        const f32 tread = cfg_.wheels[i].pos.y - cfg_.com_offset.y - cfg_.wheels[i].radius;
        lowest_tread = i == 0 ? tread : f_min(lowest_tread, tread);
    }
    const f32 floor_center = f_max(-oy - cfg_.com_offset.y, lowest_tread + kChassisClearance + r);

    for (u32 i = 0; i < 8; i++) {
        const Vec3 corner{(i & 1) ? ox : -ox, (i & 2) ? oy : -oy, (i & 4) ? oz : -oz};
        Vec3 offset = corner - cfg_.com_offset;
        if ((i & 2) == 0) {
            offset.y = floor_center;
        }
        body->sphere_offsets[i] = offset;
    }
    body->restitution = 0.1f;
    body->friction = 0.5f;
    body->box_offset = -cfg_.com_offset;

    for (u32 i = 0; i < kWheelCount; i++) {
        wheels_[i].attach_local = cfg_.wheels[i].pos - cfg_.com_offset;
        wheels_[i].radius = cfg_.wheels[i].radius;
    }
}

bool Vehicle::init(PhysWorld& world, Arena& scratch, std::string_view cfg_path, Vec3 pos, f32 yaw)
{
    cfg_path_.assign(cfg_path);
    if (!vehicle_config_load(cfg_, scratch, cfg_path)) {
        log_error("vehicle: failed to load config %.*s", static_cast<int>(cfg_path.size()),
                  cfg_path.data());
        return false;
    }

    body_ = world.body_create_box(pos, quat_identity(), cfg_.half_extents, cfg_.mass);
    if (!world.body(body_)) {
        return false;
    }
    apply_config(world);
    teleport(world, pos, yaw);
    effects_ = VehicleEffects{};
    log_info("vehicle: loaded %s (mass %.0f kg)", cfg_path_.c_str(),
             static_cast<f64>(cfg_.mass));
    return true;
}

bool Vehicle::reload_config(PhysWorld& world, Arena& scratch)
{
    VehicleConfig fresh;
    if (!vehicle_config_load(fresh, scratch, cfg_path_.view())) {
        log_warn("vehicle: config reload failed, keeping previous %s", cfg_path_.c_str());
        return false;
    }
    cfg_ = fresh;
    apply_config(world);
    log_info("vehicle: config reloaded %s", cfg_path_.c_str());
    return true;
}

void Vehicle::teleport(PhysWorld& world, Vec3 pos, f32 yaw)
{
    RigidBody* body = world.body(body_);
    if (!body) {
        return;
    }
    body->pos = pos;
    body->prev_pos = pos;
    body->rot = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, yaw);
    body->prev_rot = body->rot;
    body->vel = Vec3{0.0f, 0.0f, 0.0f};
    body->angular_vel = Vec3{0.0f, 0.0f, 0.0f};
    body->force_accum = Vec3{0.0f, 0.0f, 0.0f};
    body->torque_accum = Vec3{0.0f, 0.0f, 0.0f};
    body_wake(*body);

    for (u32 i = 0; i < kWheelCount; i++) {
        Wheel& w = wheels_[i];
        const Vec3 attach = w.attach_local;
        const f32 radius = w.radius;
        w = Wheel{};
        w.attach_local = attach;
        w.radius = radius;
    }
    steer_deg_ = 0.0f;
    input_ = VehicleInput{};
    drivetrain_init(train_, cfg_);
}

f32 Vehicle::forward_speed(const PhysWorld& world) const
{
    const RigidBody* body = world.body(body_);
    if (!body) {
        return 0.0f;
    }
    return dot(body->vel, rotate(body->rot, Vec3{0.0f, 0.0f, -1.0f}));
}

void Vehicle::driver_input(PhysWorld& world, f32 forward_intent, f32 reverse_intent, f32 steer,
                           bool handbrake)
{
    const f32 speed = forward_speed(world);
    VehicleInput in;
    in.steer = steer;
    in.handbrake = handbrake;

    if (train_.manual) {
        in.throttle = forward_intent;
        in.brake = reverse_intent;
        input_ = in;
        return;
    }

    if (forward_intent > 0.01f) {
        if (train_.gear == -1) {
            if (speed > -0.5f) {
                train_.gear = 1;
                in.throttle = forward_intent;
            } else {
                in.brake = forward_intent;
            }
        } else {
            if (train_.gear == 0) {
                train_.gear = 1;
            }
            in.throttle = forward_intent;
        }
    }
    if (reverse_intent > 0.01f) {
        if (speed > 0.5f) {
            in.brake = f_max(in.brake, reverse_intent);
            in.throttle = 0.0f;
        } else if (forward_intent <= 0.01f) {
            train_.gear = -1;
            in.throttle = reverse_intent;
        }
    }
    input_ = in;
}

void Vehicle::update_steering(f32 speed, f32 dt)
{
    const f32 max_deg = f_remap(speed, 0.0f, cfg_.steer_high_speed, cfg_.steer_max_deg,
                                cfg_.steer_high_deg);
    const f32 target = input_.steer * max_deg;
    steer_deg_ = f_move_toward(steer_deg_, target, cfg_.steer_rate_deg * dt);

    const f32 steer_rad = steer_deg_ * kDegToRad;
    const f32 wheelbase = f_abs(cfg_.wheels[WHEEL_FL].pos.z - cfg_.wheels[WHEEL_RL].pos.z);
    const f32 track = f_abs(cfg_.wheels[WHEEL_FL].pos.x - cfg_.wheels[WHEEL_FR].pos.x);

    f32 steer_fl = steer_rad;
    f32 steer_fr = steer_rad;
    if (f_abs(steer_rad) > 0.001f) {
        const f32 turn_radius = wheelbase / std::tan(f_abs(steer_rad));
        const f32 inner = std::atan(wheelbase / f_max(turn_radius - track * 0.5f, 0.5f));
        const f32 outer = std::atan(wheelbase / (turn_radius + track * 0.5f));
        if (steer_rad > 0.0f) {
            steer_fr = inner;
            steer_fl = outer;
        } else {
            steer_fr = -outer;
            steer_fl = -inner;
        }
    }
    for (u32 i = 0; i < kWheelCount; i++) {
        if (!cfg_.wheels[i].steered) {
            wheels_[i].steer_rad = 0.0f;
            continue;
        }
        wheels_[i].steer_rad = (i == WHEEL_FR) ? steer_fr : steer_fl;
    }
}

void Vehicle::tick(PhysWorld& world, f32 dt)
{
    RigidBody* body = world.body(body_);
    if (!body) {
        return;
    }

    const f32 speed = length(body->vel);
    update_steering(speed, dt);

    const Mat3 rot = quat_to_mat3(body->rot);
    const Vec3 up = rot * Vec3{0.0f, 1.0f, 0.0f};
    const TireParams tp = tire_derive_params(cfg_);
    const f32 tire_load_clamp = cfg_.mass * kGravity * kTireLoadClampFrac;
    const f32 wheel_inertia = f_max(0.5f * cfg_.wheel_mass * cfg_.wheels[0].radius
                                        * cfg_.wheels[0].radius,
                                    0.05f);
    f32 total_load = 0.0f;

    f32 compression_vel[kWheelCount] = {};
    Vec3 attach_ws[kWheelCount];

    const u32 probe_count = cfg_.susp_probes < 1u ? 1u
                          : (cfg_.susp_probes > kMaxSuspProbes ? kMaxSuspProbes : cfg_.susp_probes);

    for (u32 i = 0; i < kWheelCount; i++) {
        Wheel& w = wheels_[i];
        const WheelConfig& wc = cfg_.wheels[i];
        const f32 prev_raw = w.compression_raw;
        w.force_susp = Vec3{0.0f, 0.0f, 0.0f};
        w.force_long = Vec3{0.0f, 0.0f, 0.0f};
        w.force_lat = Vec3{0.0f, 0.0f, 0.0f};
        w.bump_force = 0.0f;
        w.align_torque = 0.0f;

        const Vec3 attach_w = body->pos + rot * w.attach_local;
        attach_ws[i] = attach_w;
        const f32 r_eff = w.radius * effects_.tire_radius_mul[i];
        const f32 ray_len = wc.travel + r_eff;
        const Vec3 roll_dir = rot * Vec3{std::sin(w.steer_rad), 0.0f, -std::cos(w.steer_rad)};

        f32 best = -1.0f;
        Vec3 normal_sum{0.0f, 0.0f, 0.0f};
        f32 best_normal_weight = 0.0f;
        Vec3 best_normal = up;

        for (u32 p = 0; p < probe_count; p++) {
            const f32 u = probe_count == 1
                            ? 0.0f
                            : (2.0f * static_cast<f32>(p) / static_cast<f32>(probe_count - 1)
                               - 1.0f);
            const f32 offset = u * r_eff * kProbeSpread;
            const f32 r_probe = std::sqrt(f_max(r_eff * r_eff - offset * offset, 0.0f));

            Ray ray;
            ray.origin = attach_w + roll_dir * offset;
            ray.dir = -up;

            PhysRayHit hit{};
            if (!world.raycast(ray, ray_len, &hit)) {
                continue;
            }
            const f32 reach = wc.travel + r_probe - hit.t;
            if (reach > best) {
                best = reach;
                best_normal = hit.normal;
                best_normal_weight = 0.0f;
                normal_sum = Vec3{0.0f, 0.0f, 0.0f};
            }
            if (reach > best - kProbeNormalBand) {
                normal_sum += hit.normal;
                best_normal_weight += 1.0f;
            }
        }

        if (best < 0.0f) {
            w.grounded = false;
            w.compression = 0.0f;
            w.compression_raw = 0.0f;
            w.load = 0.0f;
            w.slip_ratio *= 0.9f;
            w.slip_angle *= 0.9f;
            w.slide_long *= 0.9f;
            w.slide_lat *= 0.9f;
            w.reaction_torque = 0.0f;
            w.stick_active = false;
            continue;
        }

        const f32 bump_span = f_max(wc.travel * cfg_.bump_stop_zone, 0.005f);
        const f32 raw = f_clamp(best, 0.0f, wc.travel + bump_span);

        w.grounded = true;
        w.contact_normal = best_normal_weight > 0.0f ? normalize(normal_sum) : best_normal;
        w.contact_point = attach_w - up * (ray_len - raw);
        compression_vel[i] = f_clamp((raw - prev_raw) / dt, -kDamperVelClamp, kDamperVelClamp);
        w.compression_raw = raw;
        w.compression = f_min(raw, wc.travel);
    }

    f32 arb_force[kWheelCount] = {};
    if (wheels_[WHEEL_FL].grounded && wheels_[WHEEL_FR].grounded) {
        const f32 d = wheels_[WHEEL_FL].compression - wheels_[WHEEL_FR].compression;
        arb_force[WHEEL_FL] = cfg_.arb_front * d;
        arb_force[WHEEL_FR] = -cfg_.arb_front * d;
    }
    if (wheels_[WHEEL_RL].grounded && wheels_[WHEEL_RR].grounded) {
        const f32 d = wheels_[WHEEL_RL].compression - wheels_[WHEEL_RR].compression;
        arb_force[WHEEL_RL] = cfg_.arb_rear * d;
        arb_force[WHEEL_RR] = -cfg_.arb_rear * d;
    }

    const f32 nominal_load = cfg_.mass * kGravity * 0.25f;
    const f32 rebound_mul = f_max(cfg_.damper_rebound_mul, 0.1f);

    for (u32 i = 0; i < kWheelCount; i++) {
        Wheel& w = wheels_[i];
        const WheelConfig& wc = cfg_.wheels[i];
        if (!w.grounded) {
            continue;
        }
        const Vec3 attach_w = attach_ws[i];
        const f32 r_eff = w.radius * effects_.tire_radius_mul[i];

        const f32 damper_c = wc.damper_c * (compression_vel[i] < 0.0f ? rebound_mul : 1.0f);
        const f32 bump_span = f_max(wc.travel * cfg_.bump_stop_zone, 0.005f);
        const f32 bump_travel = f_max(w.compression_raw - (wc.travel - bump_span), 0.0f);
        w.bump_force = wc.spring_k * cfg_.bump_stop_mul * bump_travel * (bump_travel / bump_span);
        const f32 susp_force = f_max(wc.spring_k * w.compression + damper_c * compression_vel[i]
                                         + arb_force[i] + w.bump_force,
                                     0.0f);
        w.load = susp_force;
        total_load += susp_force;
        const Vec3 force_susp = w.contact_normal * susp_force;
        body_apply_force_at_point(*body, force_susp, attach_w);
        w.force_susp = force_susp;

        const Vec3 fwd_local{std::sin(w.steer_rad), 0.0f, -std::cos(w.steer_rad)};
        const Vec3 fwd_w = rot * fwd_local;
        const Vec3 n = w.contact_normal;
        const Vec3 long_dir = normalize(fwd_w - n * dot(fwd_w, n));
        const Vec3 lat_dir = cross(n, long_dir);

        const Vec3 v_patch = body_velocity_at_point(*body, w.contact_point);
        const f32 v_long = dot(v_patch, long_dir);
        const f32 v_lat = dot(v_patch, lat_dir);
        const f32 v_wheel = w.omega * r_eff;
        const f32 slip_vel = v_wheel - v_long;
        const f32 target_ratio = slip_vel / f_max(f_abs(v_long), kSlipDenomMin);
        const f32 target_angle = std::atan2(v_lat, f_max(f_abs(v_long), kSlipAngleDenomMin));
        const f32 relax_speed = f_max(f_abs(v_long), 1.5f);

        if (cfg_.tire_relax_long > 0.001f) {
            const f32 a = f_min(dt * relax_speed / cfg_.tire_relax_long, 1.0f);
            w.slip_ratio += (target_ratio - w.slip_ratio) * a;
        } else {
            w.slip_ratio = target_ratio;
        }
        if (cfg_.tire_relax_lat > 0.001f) {
            const f32 a = f_min(dt * relax_speed / cfg_.tire_relax_lat, 1.0f);
            w.slip_angle += (target_angle - w.slip_angle) * a;
        } else {
            w.slip_angle = target_angle;
        }
        w.slide_long = slip_vel;
        w.slide_lat = v_lat;
        const f32 patch_speed = std::sqrt(v_long * v_long + v_lat * v_lat);

        const f32 tire_load = f_min(susp_force, tire_load_clamp);
        const f32 load_sens = f_clamp(1.0f - cfg_.tire_load_sens * (tire_load / nominal_load - 1.0f),
                                      0.55f, 1.25f);
        const f32 grip_mul = effects_.tire_grip_mul[i] * load_sens;
        const f32 lat_grip_mul = (input_.handbrake && !wc.steered) ? cfg_.handbrake_grip_mul
                                                                  : 1.0f;
        const f32 limit = tp.peak_mu * tire_load * grip_mul;
        const TireForces tf = tire_compute(tp, w.slip_ratio, w.slip_angle, tire_load, grip_mul,
                                           lat_grip_mul);
        const Vec3 force_pacejka = long_dir * tf.fx + lat_dir * tf.fy;

        Vec3 force_tire = force_pacejka;
        if (patch_speed < cfg_.tire_low_speed) {
            const f32 patch_mass = f_max(tire_load / kGravity, kStickMinMass);
            f32 brake_request = input_.brake * cfg_.brake_torque * wc.brake_share
                              * effects_.brake_mul;
            if (input_.handbrake && !wc.steered) {
                brake_request += cfg_.handbrake_torque;
            }
            const bool locked = brake_request > 1.0f && f_abs(v_wheel) < 0.3f;
            const f32 spin_mass = wheel_inertia / f_max(r_eff * r_eff, 1e-4f);
            const f32 m_eff_long = locked ? patch_mass
                                          : (patch_mass * spin_mass) / (patch_mass + spin_mass);
            f32 fx_low = f_clamp(slip_vel * m_eff_long / dt, -limit, limit);
            const f32 fy_limit = limit * lat_grip_mul;
            const f32 fy_low = f_clamp(-v_lat * patch_mass / dt, -fy_limit, fy_limit);

            if (locked) {
                if (!w.stick_active) {
                    w.stick_active = true;
                    w.stick_pos = w.contact_point;
                }
                Vec3 error = w.contact_point - w.stick_pos;
                error -= n * dot(error, n);
                const f32 fx_spring = -dot(error, long_dir) * kStickStiffness;
                fx_low = f_clamp(fx_low + fx_spring, -limit, limit);
                if (f_abs(fx_low) >= limit * 0.999f) {
                    w.stick_pos = lerp(w.stick_pos, w.contact_point, 0.05f);
                }
            } else {
                w.stick_active = false;
            }

            const f32 blend = f_clamp01(patch_speed / f_max(cfg_.tire_low_speed, 0.05f));
            const Vec3 force_low = long_dir * fx_low + lat_dir * fy_low;
            force_tire = lerp(force_low, force_pacejka, blend);
        } else {
            w.stick_active = false;
        }

        const f32 tire_mag = length(force_tire);
        if (tire_mag > limit && tire_mag > 1e-6f) {
            force_tire *= limit / tire_mag;
        }
        body_apply_force_at_point(*body, force_tire, w.contact_point);

        const f32 fx_applied = dot(force_tire, long_dir);
        const f32 fy_applied = dot(force_tire, lat_dir);
        w.force_long = long_dir * fx_applied;
        w.force_lat = lat_dir * fy_applied;
        w.reaction_torque = -fx_applied * r_eff;

        const f32 peak_angle = f_max(cfg_.tire_peak_angle_deg * kDegToRad, 0.01f);
        const f32 trail_fade = f_clamp01(1.0f - f_abs(w.slip_angle) / (peak_angle * 2.0f));
        const f32 trail = cfg_.tire_mech_trail + cfg_.tire_pneumatic_trail * trail_fade;
        w.align_torque = -fy_applied * trail;
        body_apply_torque(*body, n * w.align_torque);
    }

    drivetrain_tick(train_, cfg_, wheels_, input_.throttle, effects_.engine_power_mul,
                    effects_.ignition_ok, dt);

    for (u32 i = 0; i < kWheelCount; i++) {
        Wheel& w = wheels_[i];
        const WheelConfig& wc = cfg_.wheels[i];

        w.omega += (w.drive_torque + w.reaction_torque) / wheel_inertia * dt;

        f32 brake_torque = input_.brake * cfg_.brake_torque * wc.brake_share * effects_.brake_mul;
        if (input_.handbrake && !wc.steered) {
            brake_torque += cfg_.handbrake_torque;
        }
        const f32 brake_delta = brake_torque / wheel_inertia * dt;
        if (f_abs(w.omega) <= brake_delta) {
            w.omega = 0.0f;
        } else {
            w.omega -= f_sign(w.omega) * brake_delta;
        }
        w.spin_angle = f_wrap_angle(w.spin_angle + w.omega * dt);
    }

    if (speed > 0.01f) {
        body_apply_force_at_point(*body, body->vel * (-cfg_.drag_coef * speed), body->pos);
    }
    if (total_load > 0.0f && speed > 0.2f) {
        const Vec3 rolling = body->vel * (1.0f / speed) * (-cfg_.rolling_resist * total_load);
        body_apply_force_at_point(*body, rolling, body->pos);
    }
}

} // namespace anom
