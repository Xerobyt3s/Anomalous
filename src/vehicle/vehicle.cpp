#include "vehicle/vehicle.h"
#include "core/arena.h"
#include "core/log.h"
#include "physics/world.h"
#include "platform/filesystem.h"
#include "vehicle/tire.h"

namespace anom {
namespace {
constexpr f32 kMinGravity = 1.0f;
constexpr f32 kRecoverLift = 1.2f;
constexpr f32 kDamperVelClamp = 3.0f;
constexpr f32 kSlipDenomMin = 2.0f;
constexpr f32 kSlipAngleDenomMin = 0.8f;
constexpr f32 kTireLoadClampFrac = 0.75f;
constexpr f32 kStickStiffness = 200000.0f;
constexpr f32 kStickMinMass = 40.0f;
constexpr u32 kMaxSuspProbes = 7;
constexpr f32 kProbeSpread = 0.85f;
constexpr f32 kProbeNormalBand = 0.02f;

constexpr f32 kChassisClearance = 0.25f;
constexpr f32 kChassisEndAngleDeg = 37.0f;
constexpr f32 kChassisEndMargin = 0.04f;
constexpr f32 kCountersteerMinSlip = 3.0f;
constexpr f32 kCountersteerExtraDeg = 5.0f;
constexpr f32 kSlideBrakeSpeed = 1.5f;
constexpr f32 kAdoptSnapDistance = 2.0f;
constexpr f32 kBrakeHoldSpeed = 0.6f;

}

BodyDesc Vehicle::make_body_desc() const
{
    const f32 hx = cfg_.half_extents.x;
    const f32 hy = cfg_.half_extents.y;
    const f32 hz = cfg_.half_extents.z;

    f32 lowest_tread = 0.0f;
    for (u32 i = 0; i < kWheelCount; i++) {
        const f32 tread = cfg_.wheels[i].pos.y - cfg_.com_offset.y - cfg_.wheels[i].radius;
        lowest_tread = i == 0 ? tread : f_min(lowest_tread, tread);
    }
    const f32 top = hy - cfg_.com_offset.y;
    const f32 bottom = f_min(f_max(-hy - cfg_.com_offset.y, lowest_tread + kChassisClearance), top - 0.05f);

    BodyDesc desc;
    desc.half_extents = Vec3{hx, (top - bottom) * 0.5f, hz};
    desc.box_offset = Vec3{-cfg_.com_offset.x, (top + bottom) * 0.5f, -cfg_.com_offset.z};

    const f32 x0 = -hx - cfg_.com_offset.x;
    const f32 x1 = hx - cfg_.com_offset.x;
    const f32 z_min = -hz - cfg_.com_offset.z;
    const f32 z_max = hz - cfg_.com_offset.z;
    f32 axle_min = 0.0f;
    f32 axle_max = 0.0f;
    for (u32 i = 0; i < kWheelCount; i++) {
        const f32 z = cfg_.wheels[i].pos.z - cfg_.com_offset.z;
        axle_min = i == 0 ? z : f_min(axle_min, z);
        axle_max = i == 0 ? z : f_max(axle_max, z);
    }
    const f32 slope = std::tan(kChassisEndAngleDeg * kDegToRad);
    const f32 rise = bottom - lowest_tread;
    const f32 bottom_min = f_max(axle_min - rise / slope, z_min);
    const f32 bottom_max = f_min(axle_max + rise / slope, z_max);
    const f32 nose_min = f_min(lowest_tread + slope * (axle_min - z_min) + kChassisEndMargin, top - 0.05f);
    const f32 nose_max = f_min(lowest_tread + slope * (z_max - axle_max) + kChassisEndMargin, top - 0.05f);
    const Vec3 hull[] = {
        {x0, top, z_min},        {x1, top, z_min},        {x0, top, z_max},        {x1, top, z_max},
        {x0, nose_min, z_min},   {x1, nose_min, z_min},   {x0, nose_max, z_max},   {x1, nose_max, z_max},
        {x0, bottom, bottom_min}, {x1, bottom, bottom_min}, {x0, bottom, bottom_max}, {x1, bottom, bottom_max},
    };
    for (const Vec3& p : hull) {
        desc.hull[desc.hull_count++] = p;
    }
    desc.mass = cfg_.mass;
    desc.inertia_diag = solid_box_inertia(cfg_.half_extents, cfg_.mass);
    desc.friction = 0.5f;
    desc.restitution = 0.1f;
    desc.linear_cast = true;
    desc.allow_sleeping = false;
    return desc;
}

void Vehicle::apply_config(PhysWorld& world)
{
    RigidBody* body = world.body(body_);
    if (!body) {
        return;
    }

    const BodyDesc desc = make_body_desc();
    if (!body_desc_equal(desc, body_desc_)) {
        world.body_reshape(body_, desc);
        body_desc_ = desc;
    }
    body->half_extents = cfg_.half_extents;
    body->box_offset = -cfg_.com_offset;

    for (u32 i = 0; i < kWheelCount; i++) {
        wheels_[i].attach_local = cfg_.wheels[i].pos - cfg_.com_offset;
        wheels_[i].radius = cfg_.wheels[i].radius;
    }
}

bool Vehicle::init(PhysWorld& world, Arena& scratch, std::string_view cfg_path, Vec3 pos, f32 yaw)
{
    cfg_path_.assign(cfg_path);
    cfg_mtime_ = fs::file_mtime(cfg_path);
    if (!vehicle_config_load(cfg_, scratch, cfg_path)) {
        log_error("vehicle: failed to load config %.*s", static_cast<int>(cfg_path.size()),
                  cfg_path.data());
        return false;
    }

    body_desc_ = make_body_desc();
    body_ = world.body_create_box(pos, quat_identity(), body_desc_);
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

bool Vehicle::poll_config_reload(PhysWorld& world, Arena& scratch)
{
    const i64 mtime = fs::file_mtime(cfg_path_.view());
    if (mtime == cfg_mtime_) {
        return false;
    }
    cfg_mtime_ = mtime;
    return reload_config(world, scratch);
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
    teleport(world, pos, quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, yaw));
}

void Vehicle::teleport(PhysWorld& world, Vec3 pos, Quat rot)
{
    RigidBody* body = world.body(body_);
    if (!body) {
        return;
    }
    body->pos = pos;
    body->prev_pos = pos;
    body->rot = normalize(rot);
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

CarWire Vehicle::wire(const PhysWorld& world) const
{
    CarWire out;
    if (const RigidBody* body = world.body(body_)) {
        out.pos = body->pos;
        out.rot = body->rot;
        out.vel = body->vel;
        out.angular_vel = body->angular_vel;
    }
    for (u32 i = 0; i < kWheelCount; i++) {
        out.wheels[i] = wheels_[i];
    }
    out.train = train_;
    out.input = input_;
    out.steer_deg = steer_deg_;
    return out;
}

void Vehicle::adopt(PhysWorld& world, const CarWire& wire)
{
    if (RigidBody* body = world.body(body_)) {
        const bool snapped = distance_sq(wire.pos, body->pos) > kAdoptSnapDistance * kAdoptSnapDistance;
        body->pos = wire.pos;
        body->rot = wire.rot;
        body->vel = wire.vel;
        body->angular_vel = wire.angular_vel;
        body->force_accum = Vec3{};
        body->torque_accum = Vec3{};
        body->asleep = 0;
        body->wake_request = 1;
        if (snapped) {
            body->prev_pos = body->pos;
            body->prev_rot = body->rot;
        }
    }
    for (u32 i = 0; i < kWheelCount; i++) {
        wheels_[i] = wire.wheels[i];
    }
    train_ = wire.train;
    input_ = wire.input;
    steer_deg_ = wire.steer_deg;
}

void Vehicle::recover(PhysWorld& world)
{
    const RigidBody* body = world.body(body_);
    if (!body) {
        return;
    }
    const Vec3 up = world.up_at(body->pos);
    Vec3 forward = rotate(body->rot, Vec3{0.0f, 0.0f, -1.0f});
    forward -= up * dot(forward, up);
    forward = length_sq(forward) > 1e-4f ? normalize(forward) : any_perpendicular(up);
    const Quat upright = quat_from_to(Vec3{0.0f, 1.0f, 0.0f}, up);
    const Vec3 facing = rotate(upright, Vec3{0.0f, 0.0f, -1.0f});
    const f32 turn = std::atan2(dot(cross(facing, forward), up), dot(facing, forward));
    teleport(world, body->pos + up * kRecoverLift, quat_from_axis_angle(up, turn) * upright);
}

f32 Vehicle::forward_speed(const PhysWorld& world) const
{
    const RigidBody* body = world.body(body_);
    if (!body) {
        return 0.0f;
    }
    return dot(body->vel, rotate(body->rot, Vec3{0.0f, 0.0f, -1.0f}));
}

f32 Vehicle::planar_speed(const PhysWorld& world) const
{
    const RigidBody* body = world.body(body_);
    if (!body) {
        return 0.0f;
    }
    const Vec3 v = rotate(conjugate(body->rot), body->vel);
    return std::sqrt(v.x * v.x + v.z * v.z);
}

void Vehicle::driver_input(PhysWorld& world, f32 forward_intent, f32 reverse_intent, f32 steer,
                           bool handbrake)
{
    const f32 speed = forward_speed(world);
    const f32 sliding = planar_speed(world);
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
        if (speed > 0.5f || (train_.gear >= 0 && sliding > kSlideBrakeSpeed)) {
            in.brake = f_max(in.brake, reverse_intent);
            in.throttle = 0.0f;
        } else if (forward_intent <= 0.01f) {
            train_.gear = -1;
            in.throttle = reverse_intent;
        }
    }
    input_ = in;
}

void Vehicle::update_steering(f32 speed, f32 slip_deg, f32 dt)
{
    f32 max_deg = f_remap(speed, 0.0f, cfg_.steer_high_speed, cfg_.steer_max_deg, cfg_.steer_high_deg);
    const bool countersteer = input_.steer * slip_deg > 0.0f && f_abs(slip_deg) > kCountersteerMinSlip;
    if (countersteer) {
        max_deg = f_min(cfg_.steer_max_deg, f_max(max_deg, f_abs(slip_deg) + kCountersteerExtraDeg));
    }
    const f32 target = input_.steer * max_deg;
    const f32 t_in = f_lerp(cfg_.steer_time_in, cfg_.steer_time_in_high,
                            f_clamp01(speed / f_max(cfg_.steer_high_speed, 0.1f)));
    const f32 rate_in = max_deg / f_max(t_in, 0.01f);
    const f32 rate_out = max_deg / f_max(cfg_.steer_time_out, 0.01f);
    const bool toward_centre = f_abs(target) < f_abs(steer_deg_) || target * steer_deg_ < 0.0f;
    const f32 rate = f_min(toward_centre || countersteer ? rate_out : rate_in, cfg_.steer_rate_deg);
    steer_deg_ = f_move_toward(steer_deg_, target, rate * dt);

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

struct WheelFrame {
    Vec3 attach_w;
    Vec3 roll_dir;
    f32 r_eff;
    f32 ray_len;
    f32 compression_vel;
};

void Vehicle::probe_suspension(const PhysWorld& world, const RigidBody& body, const Mat3& rot, Vec3 up,
                               u32 probe_count, f32 dt, WheelFrame* frames)
{
    for (u32 i = 0; i < kWheelCount; i++) {
        Wheel& w = wheels_[i];
        const WheelConfig& wc = cfg_.wheels[i];
        WheelFrame& f = frames[i];
        const f32 prev_raw = w.compression_raw;
        w.force_susp = Vec3{0.0f, 0.0f, 0.0f};
        w.force_long = Vec3{0.0f, 0.0f, 0.0f};
        w.force_lat = Vec3{0.0f, 0.0f, 0.0f};
        w.bump_force = 0.0f;
        w.align_torque = 0.0f;
        f.compression_vel = 0.0f;

        const Vec3 attach_w = body.pos + rot * w.attach_local;
        f.attach_w = attach_w;
        const f32 r_eff = w.radius * effects_.tire_radius_mul[i];
        f.r_eff = r_eff;
        const f32 ray_len = wc.travel + r_eff;
        f.ray_len = ray_len;
        const Vec3 roll_dir = rot * Vec3{std::sin(w.steer_rad), 0.0f, -std::cos(w.steer_rad)};
        f.roll_dir = roll_dir;

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
        f.compression_vel = f_clamp((raw - prev_raw) / dt, -kDamperVelClamp, kDamperVelClamp);
        w.compression_raw = raw;
        w.compression = f_min(raw, wc.travel);
    }
}

void Vehicle::compute_arb(f32* arb_force) const
{
    for (u32 i = 0; i < kWheelCount; i++) {
        arb_force[i] = 0.0f;
    }
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
}

void Vehicle::apply_suspension(RigidBody& body, u32 i, const WheelFrame& frame, f32 arb, f32 rebound_mul,
                               f32& total_load)
{
    Wheel& w = wheels_[i];
    const WheelConfig& wc = cfg_.wheels[i];
    const f32 damper_c = wc.damper_c * (frame.compression_vel < 0.0f ? rebound_mul : 1.0f);
    const f32 bump_span = f_max(wc.travel * cfg_.bump_stop_zone, 0.005f);
    const f32 bump_travel = f_max(w.compression_raw - (wc.travel - bump_span), 0.0f);
    w.bump_force = wc.spring_k * cfg_.bump_stop_mul * bump_travel * (bump_travel / bump_span);
    const f32 susp_force = f_max(wc.spring_k * w.compression + damper_c * frame.compression_vel
                                     + arb + w.bump_force,
                                 0.0f);
    w.load = susp_force;
    total_load += susp_force;
    const Vec3 force_susp = w.contact_normal * susp_force;
    body_apply_force_at_point(body, force_susp, frame.attach_w);
    w.force_susp = force_susp;
}

void Vehicle::apply_tire(RigidBody& body, u32 i, const WheelFrame& frame, const Mat3& rot, const TireParams& tp,
                         f32 g_mag, f32 tire_load_clamp, f32 nominal_load, f32 wheel_inertia, Vec3 grav_up, f32 dt)
{
    Wheel& w = wheels_[i];
    const WheelConfig& wc = cfg_.wheels[i];
    const f32 r_eff = frame.r_eff;
    const f32 susp_force = w.load;

    const Vec3 fwd_local{std::sin(w.steer_rad), 0.0f, -std::cos(w.steer_rad)};
    const Vec3 fwd_w = rot * fwd_local;
    const Vec3 n = w.contact_normal;
    const Vec3 long_dir = normalize(fwd_w - n * dot(fwd_w, n));
    const Vec3 lat_dir = cross(n, long_dir);

    const Vec3 v_patch = body_velocity_at_point(body, w.contact_point);
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
    f32 climb = 1.0f;
    if (wc.driven && cfg_.climb_grip > 1.0f) {
        const f32 slope_deg = std::acos(f_clamp(dot(n, grav_up), -1.0f, 1.0f)) * kRadToDeg;
        const f32 steep = f_clamp01((slope_deg - cfg_.climb_slope_start)
                                    / f_max(cfg_.climb_slope_full - cfg_.climb_slope_start, 0.1f));
        const f32 half = cfg_.climb_speed * 0.5f;
        const f32 slow = 1.0f - f_clamp01((f_abs(v_long) - half) / f_max(half, 0.05f));
        climb = 1.0f + (cfg_.climb_grip - 1.0f) * steep * slow;
    }
    const f32 grip_mul = effects_.tire_grip_mul[i] * load_sens * climb;
    const f32 lat_grip_mul = (input_.handbrake && !wc.steered) ? cfg_.handbrake_grip_mul
                                                              : 1.0f;
    const f32 limit = tp.peak_mu * tire_load * grip_mul;
    const TireForces tf = tire_compute(tp, w.slip_ratio, w.slip_angle, slip_vel, v_lat,
                                       tire_load, grip_mul, lat_grip_mul);
    const Vec3 force_pacejka = long_dir * tf.fx + lat_dir * tf.fy;

    Vec3 force_tire = force_pacejka;
    if (patch_speed < cfg_.tire_low_speed) {
        const f32 patch_mass = f_max(tire_load / g_mag, kStickMinMass);
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
    body_apply_force_at_point(body, force_tire, w.contact_point);

    const f32 fx_applied = dot(force_tire, long_dir);
    const f32 fy_applied = dot(force_tire, lat_dir);
    w.force_long = long_dir * fx_applied;
    w.force_lat = lat_dir * fy_applied;
    w.reaction_torque = -fx_applied * r_eff;

    const f32 peak_angle = f_max(cfg_.tire_peak_angle_deg * kDegToRad, 0.01f);
    const f32 trail_fade = f_clamp01(1.0f - f_abs(w.slip_angle) / (peak_angle * 2.0f));
    const f32 trail = cfg_.tire_mech_trail + cfg_.tire_pneumatic_trail * trail_fade;
    w.align_torque = -fy_applied * trail;
    body_apply_torque(body, n * w.align_torque);
}

void Vehicle::integrate_wheels(f32 wheel_inertia, f32 dt)
{
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
}

void Vehicle::apply_body_drag(RigidBody& body, f32 speed, f32 total_load) const
{
    if (speed > 0.01f) {
        body_apply_force_at_point(body, body.vel * (-cfg_.drag_coef * speed), body.pos);
    }
    if (total_load > 0.0f && speed > 0.2f) {
        const Vec3 rolling = body.vel * (1.0f / speed)
                           * (-cfg_.rolling_resist * effects_.rolling_resist_mul * total_load);
        body_apply_force_at_point(body, rolling, body.pos);
    }
}

void Vehicle::tick(PhysWorld& world, f32 dt)
{
    RigidBody* body = world.body(body_);
    if (!body) {
        return;
    }

    const f32 speed = length(body->vel);
    const Vec3 v_local = rotate(conjugate(body->rot), body->vel);
    const f32 ahead = -v_local.z;
    const f32 slip_deg = std::sqrt(v_local.x * v_local.x + v_local.z * v_local.z) > kSlideBrakeSpeed
                           ? std::atan2(v_local.x, f_abs(ahead)) * kRadToDeg
                           : 0.0f;
    update_steering(f_abs(ahead), slip_deg, dt);

    const Mat3 rot = quat_to_mat3(body->rot);
    const Vec3 up = rot * Vec3{0.0f, 1.0f, 0.0f};
    const TireParams tp = tire_derive_params(cfg_);
    const Vec3 gravity = world.gravity_at(body->pos);
    const f32 g_mag = f_max(length(gravity), kMinGravity);
    const Vec3 grav_up = length(gravity) > kMinGravity ? -gravity / length(gravity) : up;
    const f32 tire_load_clamp = cfg_.mass * g_mag * kTireLoadClampFrac;
    const f32 wheel_inertia = f_max(0.5f * cfg_.wheel_mass * cfg_.wheels[0].radius
                                        * cfg_.wheels[0].radius,
                                    0.05f);
    f32 total_load = 0.0f;

    const u32 probe_count = cfg_.susp_probes < 1u ? 1u
                          : (cfg_.susp_probes > kMaxSuspProbes ? kMaxSuspProbes : cfg_.susp_probes);

    for (Wheel& w : wheels_) {
        w.prev_compression = w.compression;
        w.prev_spin_angle = w.spin_angle;
    }
    WheelFrame frames[kWheelCount];
    probe_suspension(world, *body, rot, up, probe_count, dt, frames);

    f32 arb_force[kWheelCount];
    compute_arb(arb_force);

    const f32 nominal_load = cfg_.mass * g_mag * 0.25f;
    const f32 rebound_mul = f_max(cfg_.damper_rebound_mul, 0.1f);

    for (u32 i = 0; i < kWheelCount; i++) {
        if (!wheels_[i].grounded) {
            continue;
        }
        apply_suspension(*body, i, frames[i], arb_force[i], rebound_mul, total_load);
        apply_tire(*body, i, frames[i], rot, tp, g_mag, tire_load_clamp, nominal_load, wheel_inertia, grav_up, dt);
    }

    train_.brake_hold = input_.brake > 0.1f && planar_speed(world) < kBrakeHoldSpeed;
    drivetrain_tick(train_, cfg_, wheels_, input_.throttle, effects_.engine_power_mul,
                    effects_.ignition_ok, dt);

    integrate_wheels(wheel_inertia, dt);
    apply_body_drag(*body, speed, total_load);
}

void Vehicle::reset_contacts()
{
    for (Wheel& w : wheels_) {
        w.compression = 0.0f;
        w.compression_raw = 0.0f;
        w.bump_force = 0.0f;
        w.align_torque = 0.0f;
        w.load = 0.0f;
        w.slip_ratio = 0.0f;
        w.slip_angle = 0.0f;
        w.slide_long = 0.0f;
        w.slide_lat = 0.0f;
        w.grounded = false;
        w.contact_point = Vec3{};
        w.contact_normal = Vec3{0.0f, 1.0f, 0.0f};
        w.force_susp = Vec3{};
        w.force_long = Vec3{};
        w.force_lat = Vec3{};
    }
}

Quat wheel_visual_rot(Quat body_rot, const Wheel& wheel, bool right_side)
{
    Quat q = body_rot * quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, -wheel.steer_rad);
    q = q * quat_from_axis_angle(Vec3{1.0f, 0.0f, 0.0f}, wheel.spin_angle);
    if (right_side) {
        q = q * quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, kPi);
    }
    return q;
}

}
