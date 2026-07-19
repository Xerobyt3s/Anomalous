#include "vehicle/vehicle.h"
#include "vehicle/tire.h"
#include "physics/physics.h"
#include "core/log.h"
#include "render/debug_draw.h"
#include "platform/platform.h"

#include <stdio.h>

#define VEHICLE_GRAVITY 9.81f
#define VEHICLE_DAMPER_VEL_CLAMP 3.0f
#define VEHICLE_SLIP_DENOM_MIN 2.0f
#define VEHICLE_SLIP_ANGLE_DENOM_MIN 0.8f
#define VEHICLE_TIRE_LOAD_CLAMP_FRAC 0.75f
#define VEHICLE_STICK_STIFFNESS 200000.0f
#define VEHICLE_STICK_MIN_MASS 40.0f

static void effects_reset(VehicleEffects* fx)
{
    fx->engine_power_mul = 1.0f;
    fx->ignition_ok = 1;
    for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
        fx->tire_grip_mul[i] = 1.0f;
        fx->tire_radius_mul[i] = 1.0f;
    }
    fx->brake_mul = 1.0f;
    fx->headlights_on = 0;
}

void vehicle_apply_config(Vehicle* v, struct PhysWorld* world)
{
    RigidBody* body = phys_body(world, v->body);
    if (!body) {
        return;
    }
    const VehicleConfig* cfg = &v->cfg;

    body->inv_mass = 1.0f / cfg->mass;
    f32 hx = cfg->half_extents.x, hy = cfg->half_extents.y, hz = cfg->half_extents.z;
    f32 ix = cfg->mass / 3.0f * (hy * hy + hz * hz);
    f32 iy = cfg->mass / 3.0f * (hx * hx + hz * hz);
    f32 iz = cfg->mass / 3.0f * (hx * hx + hy * hy);
    body->inv_inertia_local = mat3_diag(1.0f / ix, 1.0f / iy, 1.0f / iz);
    body->half_extents = cfg->half_extents;

    f32 min_half = f_min(hx, f_min(hy, hz));
    f32 r = f_max(min_half * 0.4f, 0.05f);
    body->sphere_radius = r;
    body->sphere_count = 8;
    f32 ox = f_max(hx - r, 0.0f);
    f32 oy = f_max(hy - r, 0.0f);
    f32 oz = f_max(hz - r, 0.0f);
    for (u32 i = 0; i < 8; i++) {
        Vec3 corner = v3((i & 1) ? ox : -ox, (i & 2) ? oy : -oy, (i & 4) ? oz : -oz);
        body->sphere_offsets[i] = vec3_sub(corner, cfg->com_offset);
    }
    body->restitution = 0.1f;
    body->friction = 0.5f;
    body->box_offset = vec3_negate(cfg->com_offset);

    for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
        v->wheels[i].attach_local = vec3_sub(cfg->wheels[i].pos, cfg->com_offset);
        v->wheels[i].radius = cfg->wheels[i].radius;
    }
}

b32 vehicle_init(Vehicle* v, struct PhysWorld* world, const char* cfg_path, Vec3 pos, f32 yaw)
{
    snprintf(v->cfg_path, sizeof(v->cfg_path), "%s", cfg_path);
    if (!vehicle_config_load(&v->cfg, cfg_path)) {
        log_error("vehicle: failed to load config %s", cfg_path);
        return 0;
    }
    v->cfg_mtime = platform_file_mtime(cfg_path);

    v->body = phys_body_create_box(world, pos, quat_identity(), v->cfg.half_extents, v->cfg.mass);
    if (!phys_body(world, v->body)) {
        return 0;
    }
    vehicle_apply_config(v, world);
    vehicle_teleport(v, world, pos, yaw);
    effects_reset(&v->effects);
    log_info("vehicle: loaded %s (mass %.0f kg)", cfg_path, (f64)v->cfg.mass);
    return 1;
}

void vehicle_poll_config_reload(Vehicle* v, struct PhysWorld* world)
{
    i64 mtime = platform_file_mtime(v->cfg_path);
    if (mtime == v->cfg_mtime) {
        return;
    }
    v->cfg_mtime = mtime;
    VehicleConfig fresh;
    if (vehicle_config_load(&fresh, v->cfg_path)) {
        v->cfg = fresh;
        vehicle_apply_config(v, world);
        log_info("vehicle: config reloaded %s", v->cfg_path);
    } else {
        log_warn("vehicle: config reload failed, keeping previous %s", v->cfg_path);
    }
}

void vehicle_teleport(Vehicle* v, struct PhysWorld* world, Vec3 pos, f32 yaw)
{
    RigidBody* body = phys_body(world, v->body);
    if (!body) {
        return;
    }
    body->pos = pos;
    body->prev_pos = pos;
    body->rot = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), yaw);
    body->prev_rot = body->rot;
    body->vel = vec3_zero();
    body->angular_vel = vec3_zero();
    body->force_accum = vec3_zero();
    body->torque_accum = vec3_zero();

    for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
        Wheel* w = &v->wheels[i];
        w->omega = 0.0f;
        w->spin_angle = 0.0f;
        w->compression = 0.0f;
        w->load = 0.0f;
        w->slip_ratio = 0.0f;
        w->slip_angle = 0.0f;
        w->steer_rad = 0.0f;
        w->drive_torque = 0.0f;
        w->reaction_torque = 0.0f;
        w->grounded = 0;
        w->stick_active = 0;
    }
    v->steer_deg = 0.0f;
    VehicleInput zero = {0};
    v->input = zero;
    drivetrain_init(&v->train, &v->cfg);
}

void vehicle_set_input(Vehicle* v, VehicleInput input)
{
    v->input = input;
}

f32 vehicle_forward_speed(const Vehicle* v, struct PhysWorld* world)
{
    RigidBody* body = phys_body((PhysWorld*)world, v->body);
    if (!body) {
        return 0.0f;
    }
    Vec3 forward = quat_rotate_vec3(body->rot, v3(0.0f, 0.0f, -1.0f));
    return vec3_dot(body->vel, forward);
}

void vehicle_driver_input(Vehicle* v, struct PhysWorld* world, f32 forward_intent, f32 reverse_intent, f32 steer, b32 handbrake)
{
    f32 speed = vehicle_forward_speed(v, world);
    VehicleInput in = {0};
    in.steer = steer;
    in.handbrake = handbrake;

    if (v->train.manual) {
        in.throttle = forward_intent;
        in.brake = reverse_intent;
        v->input = in;
        return;
    }

    if (forward_intent > 0.01f) {
        if (v->train.gear == -1) {
            if (speed > -0.5f) {
                v->train.gear = 1;
                in.throttle = forward_intent;
            } else {
                in.brake = forward_intent;
            }
        } else {
            if (v->train.gear == 0) {
                v->train.gear = 1;
            }
            in.throttle = forward_intent;
        }
    }
    if (reverse_intent > 0.01f) {
        if (speed > 0.5f) {
            in.brake = f_max(in.brake, reverse_intent);
            in.throttle = 0.0f;
        } else if (forward_intent <= 0.01f) {
            v->train.gear = -1;
            in.throttle = reverse_intent;
        }
    }
    v->input = in;
}

static void vehicle_update_steering(Vehicle* v, f32 speed, f32 dt)
{
    const VehicleConfig* cfg = &v->cfg;
    f32 max_deg = f_remap(speed, 0.0f, cfg->steer_high_speed, cfg->steer_max_deg, cfg->steer_high_deg);
    f32 target = v->input.steer * max_deg;
    v->steer_deg = f_move_toward(v->steer_deg, target, cfg->steer_rate_deg * dt);

    f32 steer_rad = v->steer_deg * DEG_TO_RAD;
    f32 wheelbase = f_abs(cfg->wheels[WHEEL_FL].pos.z - cfg->wheels[WHEEL_RL].pos.z);
    f32 track = f_abs(cfg->wheels[WHEEL_FL].pos.x - cfg->wheels[WHEEL_FR].pos.x);

    f32 steer_fl = steer_rad;
    f32 steer_fr = steer_rad;
    if (f_abs(steer_rad) > 0.001f) {
        f32 turn_radius = wheelbase / tanf(f_abs(steer_rad));
        f32 inner = atanf(wheelbase / f_max(turn_radius - track * 0.5f, 0.5f));
        f32 outer = atanf(wheelbase / (turn_radius + track * 0.5f));
        if (steer_rad > 0.0f) {
            steer_fr = inner;
            steer_fl = outer;
        } else {
            steer_fr = -outer;
            steer_fl = -inner;
        }
    }
    for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
        if (!cfg->wheels[i].steered) {
            v->wheels[i].steer_rad = 0.0f;
            continue;
        }
        v->wheels[i].steer_rad = (i == WHEEL_FR) ? steer_fr : steer_fl;
    }
}

void vehicle_tick(Vehicle* v, struct PhysWorld* world, f32 dt)
{
    RigidBody* body = phys_body(world, v->body);
    if (!body) {
        return;
    }
    const VehicleConfig* cfg = &v->cfg;

    f32 speed = vec3_length(body->vel);
    vehicle_update_steering(v, speed, dt);

    Mat3 rot = quat_to_mat3(body->rot);
    Vec3 up = mat3_mul_vec3(rot, v3(0.0f, 1.0f, 0.0f));
    TireParams tp = tire_derive_params(cfg);
    f32 tire_load_clamp = cfg->mass * VEHICLE_GRAVITY * VEHICLE_TIRE_LOAD_CLAMP_FRAC;
    f32 wheel_inertia = f_max(0.5f * cfg->wheel_mass * cfg->wheels[0].radius * cfg->wheels[0].radius, 0.05f);
    f32 total_load = 0.0f;

    f32 compression_vel[VEHICLE_WHEEL_COUNT];
    Vec3 attach_ws[VEHICLE_WHEEL_COUNT];

    for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
        Wheel* w = &v->wheels[i];
        const WheelConfig* wc = &cfg->wheels[i];
        f32 prev_compression = w->compression;
        w->force_susp = vec3_zero();
        w->force_long = vec3_zero();
        w->force_lat = vec3_zero();
        compression_vel[i] = 0.0f;

        Vec3 attach_w = vec3_add(body->pos, mat3_mul_vec3(rot, w->attach_local));
        attach_ws[i] = attach_w;
        f32 r_eff = w->radius * v->effects.tire_radius_mul[i];
        f32 ray_len = wc->travel + r_eff;
        Ray ray;
        ray.origin = attach_w;
        ray.dir = vec3_negate(up);

        PhysRayHit hit;
        if (!phys_raycast(world, ray, ray_len, &hit)) {
            w->grounded = 0;
            w->compression = 0.0f;
            w->load = 0.0f;
            w->slip_ratio *= 0.9f;
            w->slip_angle *= 0.9f;
            w->slide_long *= 0.9f;
            w->slide_lat *= 0.9f;
            w->reaction_torque = 0.0f;
            w->stick_active = 0;
            continue;
        }

        w->grounded = 1;
        w->contact_point = hit.point;
        w->contact_normal = hit.normal;
        f32 compression = f_clamp(ray_len - hit.t, 0.0f, wc->travel);
        compression_vel[i] = f_clamp((compression - prev_compression) / dt,
                                     -VEHICLE_DAMPER_VEL_CLAMP, VEHICLE_DAMPER_VEL_CLAMP);
        w->compression = compression;
    }

    f32 arb_force[VEHICLE_WHEEL_COUNT] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (v->wheels[WHEEL_FL].grounded && v->wheels[WHEEL_FR].grounded) {
        f32 d = v->wheels[WHEEL_FL].compression - v->wheels[WHEEL_FR].compression;
        arb_force[WHEEL_FL] = cfg->arb_front * d;
        arb_force[WHEEL_FR] = -cfg->arb_front * d;
    }
    if (v->wheels[WHEEL_RL].grounded && v->wheels[WHEEL_RR].grounded) {
        f32 d = v->wheels[WHEEL_RL].compression - v->wheels[WHEEL_RR].compression;
        arb_force[WHEEL_RL] = cfg->arb_rear * d;
        arb_force[WHEEL_RR] = -cfg->arb_rear * d;
    }

    f32 nominal_load = cfg->mass * VEHICLE_GRAVITY * 0.25f;
    f32 rebound_mul = f_max(cfg->damper_rebound_mul, 0.1f);

    for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
        Wheel* w = &v->wheels[i];
        const WheelConfig* wc = &cfg->wheels[i];
        if (!w->grounded) {
            continue;
        }
        Vec3 attach_w = attach_ws[i];
        f32 r_eff = w->radius * v->effects.tire_radius_mul[i];

        f32 damper_c = wc->damper_c * (compression_vel[i] < 0.0f ? rebound_mul : 1.0f);
        f32 susp_force = f_max(wc->spring_k * w->compression + damper_c * compression_vel[i]
                               + arb_force[i], 0.0f);
        w->load = susp_force;
        total_load += susp_force;
        Vec3 force_susp = vec3_scale(w->contact_normal, susp_force);
        body_apply_force_at_point(body, force_susp, attach_w);
        w->force_susp = force_susp;

        Vec3 fwd_local = v3(sinf(w->steer_rad), 0.0f, -cosf(w->steer_rad));
        Vec3 fwd_w = mat3_mul_vec3(rot, fwd_local);
        Vec3 n = w->contact_normal;
        Vec3 long_dir = vec3_normalize(vec3_sub(fwd_w, vec3_scale(n, vec3_dot(fwd_w, n))));
        Vec3 lat_dir = vec3_cross(n, long_dir);

        Vec3 v_patch = body_velocity_at_point(body, w->contact_point);
        f32 v_long = vec3_dot(v_patch, long_dir);
        f32 v_lat = vec3_dot(v_patch, lat_dir);
        f32 v_wheel = w->omega * r_eff;
        f32 slip_vel = v_wheel - v_long;
        f32 target_ratio = slip_vel / f_max(f_abs(v_long), VEHICLE_SLIP_DENOM_MIN);
        f32 target_angle = atan2f(v_lat, f_max(f_abs(v_long), VEHICLE_SLIP_ANGLE_DENOM_MIN));
        f32 relax_speed = f_max(f_abs(v_long), 1.5f);
        if (cfg->tire_relax_long > 0.001f) {
            f32 a = f_min(dt * relax_speed / cfg->tire_relax_long, 1.0f);
            w->slip_ratio += (target_ratio - w->slip_ratio) * a;
        } else {
            w->slip_ratio = target_ratio;
        }
        if (cfg->tire_relax_lat > 0.001f) {
            f32 a = f_min(dt * relax_speed / cfg->tire_relax_lat, 1.0f);
            w->slip_angle += (target_angle - w->slip_angle) * a;
        } else {
            w->slip_angle = target_angle;
        }
        w->slide_long = slip_vel;
        w->slide_lat = v_lat;
        f32 patch_speed = sqrtf(v_long * v_long + v_lat * v_lat);

        f32 tire_load = f_min(susp_force, tire_load_clamp);
        f32 load_sens = f_clamp(1.0f - cfg->tire_load_sens * (tire_load / nominal_load - 1.0f),
                                0.55f, 1.25f);
        f32 grip_mul = v->effects.tire_grip_mul[i] * load_sens;
        f32 lat_grip_mul = (v->input.handbrake && !wc->steered) ? cfg->handbrake_grip_mul : 1.0f;
        f32 limit = tp.peak_mu * tire_load * grip_mul;
        TireForces tf = tire_compute(&tp, w->slip_ratio, w->slip_angle, tire_load, grip_mul, lat_grip_mul);
        Vec3 force_pacejka = vec3_add(vec3_scale(long_dir, tf.fx), vec3_scale(lat_dir, tf.fy));

        Vec3 force_tire = force_pacejka;
        if (patch_speed < cfg->tire_low_speed) {
            f32 patch_mass = f_max(tire_load / VEHICLE_GRAVITY, VEHICLE_STICK_MIN_MASS);
            f32 brake_request = v->input.brake * cfg->brake_torque * wc->brake_share * v->effects.brake_mul;
            if (v->input.handbrake && !wc->steered) {
                brake_request += cfg->handbrake_torque;
            }
            b32 locked = brake_request > 1.0f && f_abs(v_wheel) < 0.3f;
            f32 spin_mass = wheel_inertia / f_max(r_eff * r_eff, 1e-4f);
            f32 m_eff_long = locked ? patch_mass
                           : (patch_mass * spin_mass) / (patch_mass + spin_mass);
            f32 fx_low = f_clamp(slip_vel * m_eff_long / dt, -limit, limit);
            f32 fy_limit = limit * lat_grip_mul;
            f32 fy_low = f_clamp(-v_lat * patch_mass / dt, -fy_limit, fy_limit);
            if (locked) {
                if (!w->stick_active) {
                    w->stick_active = 1;
                    w->stick_pos = w->contact_point;
                }
                Vec3 error = vec3_sub(w->contact_point, w->stick_pos);
                error = vec3_sub(error, vec3_scale(n, vec3_dot(error, n)));
                f32 fx_spring = -vec3_dot(error, long_dir) * VEHICLE_STICK_STIFFNESS;
                fx_low = f_clamp(fx_low + fx_spring, -limit, limit);
                if (f_abs(fx_low) >= limit * 0.999f) {
                    w->stick_pos = vec3_lerp(w->stick_pos, w->contact_point, 0.05f);
                }
            } else {
                w->stick_active = 0;
            }
            f32 blend = f_clamp01(patch_speed / f_max(cfg->tire_low_speed, 0.05f));
            Vec3 force_low = vec3_add(vec3_scale(long_dir, fx_low), vec3_scale(lat_dir, fy_low));
            force_tire = vec3_lerp(force_low, force_pacejka, blend);
        } else {
            w->stick_active = 0;
        }

        f32 tire_mag = vec3_length(force_tire);
        if (tire_mag > limit && tire_mag > 1e-6f) {
            force_tire = vec3_scale(force_tire, limit / tire_mag);
        }
        body_apply_force_at_point(body, force_tire, w->contact_point);
        f32 fx_applied = vec3_dot(force_tire, long_dir);
        w->force_long = vec3_scale(long_dir, fx_applied);
        w->force_lat = vec3_scale(lat_dir, vec3_dot(force_tire, lat_dir));
        w->reaction_torque = -fx_applied * r_eff;
    }

    drivetrain_tick(&v->train, cfg, v->wheels, v->input.throttle, v->effects.engine_power_mul,
                    v->effects.ignition_ok, dt);

    for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
        Wheel* w = &v->wheels[i];
        const WheelConfig* wc = &cfg->wheels[i];

        w->omega += (w->drive_torque + w->reaction_torque) / wheel_inertia * dt;

        f32 brake_torque = v->input.brake * cfg->brake_torque * wc->brake_share * v->effects.brake_mul;
        if (v->input.handbrake && !wc->steered) {
            brake_torque += cfg->handbrake_torque;
        }
        f32 brake_delta = brake_torque / wheel_inertia * dt;
        if (f_abs(w->omega) <= brake_delta) {
            w->omega = 0.0f;
        } else {
            w->omega -= f_sign(w->omega) * brake_delta;
        }
        w->spin_angle = f_wrap_angle(w->spin_angle + w->omega * dt);
    }

    if (speed > 0.01f) {
        Vec3 drag = vec3_scale(body->vel, -cfg->drag_coef * speed);
        body_apply_force_at_point(body, drag, body->pos);
    }
    if (total_load > 0.0f && speed > 0.2f) {
        Vec3 rolling = vec3_scale(vec3_scale(body->vel, 1.0f / speed), -cfg->rolling_resist * total_load);
        body_apply_force_at_point(body, rolling, body->pos);
    }
}

void vehicle_debug_draw(Vehicle* v, struct PhysWorld* world, f32 alpha, b32 detail)
{
    RigidBody* body = phys_body(world, v->body);
    if (!body) {
        return;
    }
    const VehicleConfig* cfg = &v->cfg;

    Vec3 pos = vec3_lerp(body->prev_pos, body->pos, alpha);
    Quat rot = quat_slerp(body->prev_rot, body->rot, alpha);
    Mat3 m = quat_to_mat3(rot);
    Vec3 up = mat3_mul_vec3(m, v3(0.0f, 1.0f, 0.0f));

    Vec3 chassis_center = vec3_add(pos, mat3_mul_vec3(m, vec3_negate(cfg->com_offset)));
    dd_obb(chassis_center, rot, cfg->half_extents, DD_WHITE);
    Vec3 fwd = mat3_mul_vec3(m, v3(0.0f, 0.0f, -1.0f));
    Vec3 nose = vec3_add(chassis_center, vec3_add(mat3_mul_vec3(m, v3(0.0f, cfg->half_extents.y, 0.0f)),
                                                  vec3_scale(fwd, cfg->half_extents.z * 0.6f)));
    dd_arrow(nose, vec3_add(nose, vec3_scale(fwd, 1.2f)), 0.2f, DD_CYAN);

    for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
        Wheel* w = &v->wheels[i];
        const WheelConfig* wc = &cfg->wheels[i];
        Vec3 attach_w = vec3_add(pos, mat3_mul_vec3(m, w->attach_local));
        Vec3 center = vec3_sub(attach_w, vec3_scale(up, wc->travel - w->compression));

        Vec3 fwd_local = v3(sinf(w->steer_rad), 0.0f, -cosf(w->steer_rad));
        Vec3 lat_local = v3(-cosf(w->steer_rad), 0.0f, -sinf(w->steer_rad));
        Vec3 fwd_w = mat3_mul_vec3(m, fwd_local);
        Vec3 lat_w = mat3_mul_vec3(m, lat_local);

        f32 slip_t = f_clamp01(f_max(f_abs(w->slip_ratio) / (cfg->tire_peak_slip * 2.0f),
                                     f_abs(w->slip_angle) / (cfg->tire_peak_angle_deg * DEG_TO_RAD * 2.0f)));
        if (!w->grounded) {
            slip_t = 0.0f;
        }
        u32 color = dd_rgba((u8)(f_min(slip_t * 2.0f, 1.0f) * 255.0f),
                            (u8)(f_min(2.0f - slip_t * 2.0f, 1.0f) * 255.0f),
                            60, 255);
        dd_circle(center, lat_w, w->radius, color);
        Vec3 rim = vec3_add(vec3_scale(fwd_w, cosf(w->spin_angle)), vec3_scale(up, sinf(w->spin_angle)));
        dd_line(center, vec3_add(center, vec3_scale(rim, w->radius)), color);

        if (detail) {
            dd_line(attach_w, center, w->grounded ? DD_GREEN : DD_GRAY);
            if (w->grounded) {
                dd_cross(w->contact_point, 0.15f, DD_ORANGE);
                dd_arrow(w->contact_point, vec3_add(w->contact_point, vec3_scale(w->force_susp, 1.0f / 4000.0f)), 0.1f, DD_GREEN);
                dd_arrow(w->contact_point, vec3_add(w->contact_point, vec3_scale(w->force_long, 1.0f / 4000.0f)), 0.1f, DD_BLUE);
                dd_arrow(w->contact_point, vec3_add(w->contact_point, vec3_scale(w->force_lat, 1.0f / 4000.0f)), 0.1f, DD_RED);
            }
        }
    }

    if (detail) {
        dd_arrow(pos, vec3_add(pos, vec3_scale(body->vel, 0.25f)), 0.15f, DD_CYAN);
    }
}
