#include "player/movement.h"
#include "physics/gravity_field.h"
#include "engine/physics/physics_world.h"
#include "math/glm_bridge.h"

namespace anom {
namespace {
constexpr f32 kPitchLimit = 89.0f * kDegToRad;
constexpr f32 kUpEase = 4.0f;
constexpr f32 kUpCreep = 0.15f;
constexpr f32 kSlideLeanDeg = 4.0f;
constexpr f32 kDiveLeanDeg = 6.0f;
constexpr f32 kLeanRate = 10.0f;
constexpr f32 kWallNy = 0.3f;
constexpr f32 kGroundHug = 1.0f;
constexpr f32 kSlopeFacingMin = 0.35f;
constexpr f32 kSettleDt = 1.0f / 60.0f;
constexpr f32 kRiseFromGround = 0.1f;

Vec3 approach(Vec3 current, Vec3 target, f32 max_delta)
{
    const Vec3 delta = target - current;
    const f32 dist = length(delta);
    if (dist <= max_delta || dist < 1e-6f) {
        return target;
    }
    return current + delta * (max_delta / dist);
}

}

Vec3 frame_up(Quat frame)
{
    return rotate(frame, Vec3{0.0f, 1.0f, 0.0f});
}

Vec3 frame_forward(Quat frame, f32 yaw)
{
    return rotate(frame, Vec3{std::sin(yaw), 0.0f, -std::cos(yaw)});
}

Vec3 frame_right(Quat frame, f32 yaw)
{
    return rotate(frame, Vec3{std::cos(yaw), 0.0f, std::sin(yaw)});
}

Vec3 frame_view(Quat frame, f32 yaw, f32 pitch)
{
    const f32 cp = std::cos(pitch);
    return rotate(frame, Vec3{cp * std::sin(yaw), std::sin(pitch), -cp * std::cos(yaw)});
}

Quat frame_turn_up(Quat frame, Vec3 new_up)
{
    const Vec3 up = frame_up(frame);
    const Quat turn = quat_from_to(up, normalize(new_up), rotate(frame, Vec3{1.0f, 0.0f, 0.0f}));
    return normalize(turn * frame);
}

void frame_view_angles(Quat frame, Vec3 dir, f32& yaw, f32& pitch)
{
    const Vec3 local = normalize(rotate(conjugate(frame), dir));
    yaw = std::atan2(local.x, -local.z);
    pitch = f_clamp(std::asin(f_clamp(local.y, -1.0f, 1.0f)), -kPitchLimit, kPitchLimit);
}

ghost::game::PlayerState ghost_state(const MoveState& s)
{
    const Quat to_local = conjugate(s.frame);
    const Vec3 pos = rotate(to_local, s.pos);
    const Vec3 vel = rotate(to_local, s.vel);
    ghost::game::PlayerState out;
    out.position = glm::vec3(pos.x, pos.y, pos.z);
    out.velocity = glm::vec3(vel.x, vel.y, vel.z);
    out.yaw = s.yaw;
    out.pitch = s.pitch;
    out.grounded = s.grounded;
    out.aiming = s.aiming;
    out.sprinting = s.sprinting;
    out.hasteTime = s.haste_time;
    out.hasteScale = s.haste_scale;
    out.shroudTime = s.shroud_time;
    out.health = s.health;
    out.sinceHurt = s.since_hurt;
    out.stance = s.stance;
    out.height = s.height;
    out.eyeHeight = s.eye_height;
    out.slideCooldown = s.slide_cooldown;
    out.crouchHeld = s.crouch_held;
    out.diveUsed = s.dive_used;
    out.holstered = s.holstered;
    out.holster = s.holster;
    out.lieYaw = s.lie_yaw;
    out.roll = s.roll;
    out.downed = s.downed;
    return out;
}

void Movement::adopt(const Movement& other)
{
    ghost::engine::PhysicsWorld* const jolt = jolt_;
    const u32 character = character_;
    *this = other;
    jolt_ = jolt;
    character_ = character;
}

void Movement::add_impulse(Vec3 delta_v)
{
    state_.vel += delta_v;
    if (dot(delta_v, up()) > 0.0f) {
        state_.grounded = false;
    }
}

void Movement::apply_haste(f32 duration, f32 scale)
{
    state_.haste_time = duration;
    state_.haste_scale = scale;
}

void Movement::apply_shroud(f32 duration)
{
    state_.shroud_time = duration;
}

void Movement::set_vitals(f32 health, f32 since_hurt, bool downed)
{
    state_.health = health;
    state_.since_hurt = since_hurt;
    state_.downed = downed;
}

void Movement::update_lying(Stance before, f32 speed_before, Vec3 planar, f32 dt)
{
    MoveState& s = state_;
    if (s.stance != Stance::Crawl && s.stance != Stance::Dive) {
        s.roll = 0.0f;
        s.lie_yaw = s.yaw;
        return;
    }
    if (before != Stance::Crawl && before != Stance::Dive && s.stance == Stance::Crawl) {
        s.lie_yaw = s.yaw;
        s.roll = 0.0f;
    }
    if (s.grounded && speed_before > 0.3f) {
        const Vec3 local = rotate(conjugate(s.frame), planar);
        const f32 travel = std::atan2(local.x, -local.z);
        const f32 turn = f_wrap_angle(travel - s.lie_yaw);
        const f32 most = kPi * dt;
        s.lie_yaw += f_clamp(turn, -most, most);
    }
    const f32 target = s.roll + f_wrap_angle(f_wrap_angle(s.yaw - s.lie_yaw) - s.roll);
    const f32 rate = (kPi * 0.5f) / f_max(tuning_.roll_time, 0.01f);
    s.roll += f_clamp(target - s.roll, -rate * dt, rate * dt);
}

void Movement::init(ghost::engine::PhysicsWorld* jolt, Vec3 feet, Vec3 up, f32 yaw)
{
    jolt_ = jolt;
    state_ = MoveState{};
    state_.pos = feet;
    state_.frame = quat_from_to(Vec3{0.0f, 1.0f, 0.0f}, normalize(up));
    state_.yaw = yaw;
    state_.height = tuning_.height;
    state_.eye_height = tuning_.eye_height;
    if (jolt_) {
        jolt_->characterCreate(id(), to_glm(feet), to_glm(up), tuning_.radius, tuning_.height);
    }
    previous_ = state_;
}

void Movement::attach(ghost::engine::PhysicsWorld* jolt)
{
    const bool fresh = jolt != jolt_;
    jolt_ = jolt;
    if (!jolt_) {
        return;
    }
    if (!jolt_->characterValid(id())) {
        jolt_->characterCreate(id(), to_glm(state_.pos), to_glm(up()), tuning_.radius, state_.height);
    } else if (fresh) {
        jolt_->characterSetHeight(id(), state_.height, to_glm(up()));
        jolt_->characterTeleport(id(), to_glm(state_.pos), to_glm(up()));
    }
}

void Movement::align_up(Vec3 new_up)
{
    state_.frame = frame_turn_up(state_.frame, new_up);
}

void Movement::settle(const GravityField* field)
{
    if (!jolt_ || !jolt_->characterValid(id())) {
        return;
    }
    const Vec3 g = field ? field->gravity_at(state_.pos) : Vec3{0.0f, -kDefaultGravity, 0.0f};
    const ghost::engine::CharacterMove moved = jolt_->characterMove(id(), to_glm(g * kSettleDt), to_glm(up()), to_glm(g), kSettleDt, true);
    state_.pos = from_glm(moved.position);
    state_.grounded = moved.grounded;
    state_.vel = Vec3{};
    previous_ = state_;
}

void Movement::teleport(Vec3 feet)
{
    state_.pos = feet;
    state_.vel = Vec3{};
    state_.ground_vel = Vec3{};
    state_.grounded = false;
    if (jolt_) {
        if (!jolt_->characterValid(id())) {
            jolt_->characterCreate(id(), to_glm(feet), to_glm(up()), tuning_.radius, state_.height);
        } else {
            jolt_->characterTeleport(id(), to_glm(feet), to_glm(up()));
        }
    }
    previous_ = state_;
}

void Movement::look(f32 dyaw, f32 dpitch)
{
    state_.yaw = f_wrap_angle(state_.yaw + dyaw);
    state_.pitch = f_clamp(state_.pitch + dpitch, -kPitchLimit, kPitchLimit);
}

void Movement::face(Vec3 dir)
{
    frame_view_angles(state_.frame, dir, state_.yaw, state_.pitch);
}

bool Movement::fits(Vec3 feet, f32 height) const
{
    return !jolt_ || jolt_->characterFits(id(), to_glm(feet), to_glm(up()), height);
}

void Movement::turn_up(Vec3 target, f32 dt)
{
    const Vec3 current = up();
    const f32 angle = std::acos(f_clamp(dot(current, target), -1.0f, 1.0f));
    state_.up_turn_rate = 0.0f;
    if (angle < 1e-4f || dt <= 0.0f) {
        return;
    }
    const f32 cap = (state_.grounded ? tuning_.up_turn_ground : tuning_.up_turn_air) * dt;
    const f32 eased = angle * (1.0f - std::exp(-kUpEase * dt)) + kUpCreep * dt;
    const f32 step = f_min(angle, f_min(eased, cap));
    const Vec3 next = rotate_toward(current, target, step);
    state_.frame = frame_turn_up(state_.frame, next);
    state_.up_turn_rate = step / dt;
}

void Movement::apply_stance_height()
{
    if (jolt_ && !jolt_->characterSetHeight(id(), state_.height, to_glm(up()))) {
        state_.height = jolt_->characterHeight(id());
    }
}

void Movement::tick(const MoveCommand& cmd, const GravityField* field, f32 dt)
{
    previous_ = state_;
    MoveState& s = state_;
    const MoveTuning& t = tuning_;

    GravitySample g;
    if (field) {
        g = field->sample(s.pos + up() * (s.height * 0.5f));
    } else {
        g.gravity = Vec3{0.0f, -kDefaultGravity, 0.0f};
        g.up = Vec3{0.0f, 1.0f, 0.0f};
        g.presence = 0.0f;
    }
    s.field_presence = g.presence;
    turn_up(g.up, dt);

    const Vec3 u = up();
    const Vec3 forward = frame_forward(s.frame, s.yaw);
    const Vec3 right = frame_right(s.frame, s.yaw);

    Vec2 move = cmd.move;
    if (length_sq(move) > 1.0f) {
        move = normalize(move);
    }
    if (s.downed) {
        move = Vec2{};
    }
    const Stance stance_before = s.stance;

    f32 v_up = dot(s.vel, u);
    Vec3 planar = s.vel - u * v_up;
    const f32 speed_before = length(planar);
    const bool pressed = cmd.crouch && !s.crouch_held;
    s.crouch_held = cmd.crouch;
    s.slide_cooldown = f_max(s.slide_cooldown - dt, 0.0f);
    const bool can_stand = fits(s.pos, t.height);
    const auto rise = [&] { s.stance = (cmd.crouch || !can_stand) ? Stance::Crouch : Stance::Stand; };
    if (s.grounded) {
        s.dive_used = false;
    }

    const bool dive = cmd.jump && !s.grounded && !s.dive_used && !s.downed && s.stance != Stance::Crawl
                   && s.stance != Stance::Dive;
    bool jump_taken = false;
    if (dive) {
        s.stance = Stance::Dive;
        s.dive_used = true;
        jump_taken = true;
        const Vec3 wished = forward * move.y + right * move.x;
        const Vec3 dir = length(wished) > 0.1f ? normalize(wished) : forward;
        const f32 along = dot(planar, dir);
        planar = dir * f_max(along, t.dive_speed) + (planar - dir * along) * 0.5f;
        v_up = f_max(v_up, t.dive_lift);
    } else if (cmd.crawl && !s.downed && s.stance != Stance::Crawl && s.stance != Stance::Dive) {
        s.stance = Stance::Crawl;
    } else {
        switch (s.stance) {
        case Stance::Stand:
            if (pressed && s.grounded && s.sprinting && speed_before >= t.slide_min_speed
                && s.slide_cooldown <= 0.0f) {
                s.stance = Stance::Slide;
                planar *= t.slide_boost;
            } else if (cmd.crouch) {
                s.stance = Stance::Crouch;
            }
            break;
        case Stance::Crouch:
            rise();
            break;
        case Stance::Slide:
            if (!cmd.crouch || (s.grounded && speed_before < t.slide_end_speed)
                || (s.grounded && cmd.jump)) {
                rise();
                s.slide_cooldown = t.slide_cooldown;
            }
            break;
        case Stance::Crawl:
            if (cmd.crawl || cmd.jump) {
                jump_taken = cmd.jump;
                if (can_stand || fits(s.pos, t.crouch_height)) {
                    rise();
                }
            }
            break;
        case Stance::Dive:
            if (s.grounded) {
                s.stance = Stance::Crawl;
            }
            break;
        }
    }

    if (s.downed && (s.stance == Stance::Crawl || s.stance == Stance::Dive)) {
        s.stance = Stance::Stand;
    }
    update_lying(stance_before, speed_before, planar, dt);

    const bool flat = s.stance == Stance::Crawl || s.stance == Stance::Dive;
    const bool sliding = s.stance == Stance::Slide;
    const bool low = s.stance != Stance::Stand;
    s.height = s.downed ? t.downed_height : (flat ? t.crawl_height : (low ? t.crouch_height : t.height));
    const f32 eye_target = s.downed                    ? t.downed_eye_height
                         : s.stance == Stance::Dive    ? t.dive_eye_height
                         : s.stance == Stance::Crawl   ? t.crawl_eye_height
                         : sliding                     ? t.slide_eye_height
                         : low                         ? t.crouch_eye_height
                                                       : t.eye_height;
    s.eye_height += (eye_target - s.eye_height) * (1.0f - std::exp(-t.stance_rate * dt));

    s.sprinting = cmd.sprint && !cmd.aim && !cmd.hands_busy && move.y > 0.5f && !low;

    if (cmd.holster && !cmd.hands_busy && !s.downed) {
        s.holstered = !s.holstered;
    }
    const f32 holster_rate = 1.0f / f_max(s.holstered ? t.holster_time : t.draw_time, 0.01f);
    s.holster = f_clamp01(s.holster + (s.holstered ? dt : -dt) * holster_rate);
    s.aiming = cmd.aim && !cmd.hands_busy && s.holster <= 0.0f;
    s.haste_time = f_max(s.haste_time - dt, 0.0f);
    s.shroud_time = f_max(s.shroud_time - dt, 0.0f);
    s.since_hurt += dt;

    f32 speed = s.sprinting ? t.sprint_speed : (s.aiming ? t.aim_speed : t.walk_speed);
    if (low) {
        speed = f_min(speed, s.stance == Stance::Crawl ? t.crawl_speed : t.crouch_speed);
    }
    if (s.haste_time > 0.0f) {
        speed *= s.haste_scale;
    }
    if (s.holster >= 1.0f) {
        speed *= t.holstered_speed;
    }
    if (cmd.hands_busy) {
        speed = f_min(speed, t.busy_speed);
    }
    speed *= speed_mul_;
    Vec3 wish = (forward * move.y + right * move.x) * speed;
    const f32 wind_up = dot(cmd.wind, u);
    const Vec3 drift = cmd.wind - u * wind_up;
    const bool windy = length_sq(drift) > 1e-4f;
    wish += drift;
    const bool has_wish = length_sq(wish) > 0.0f;
    f32 accel = s.grounded ? (has_wish ? t.ground_accel : t.ground_decel) : t.air_accel;
    if (s.stance == Stance::Dive) {
        accel = 0.0f;
    }
    if (!s.grounded && windy) {
        accel = f_max(accel, t.wind_air_accel);
    }

    const bool belly_sliding = s.stance == Stance::Crawl && s.grounded
                            && speed_before > t.crawl_speed + 0.3f;
    if ((sliding || belly_sliding) && s.grounded) {
        const f32 sliding0 = length(planar);
        if (sliding0 > 1e-4f) {
            const Vec3 along = planar * (1.0f / sliding0);
            Vec3 lean = forward * move.y + right * move.x;
            lean -= along * dot(lean, along);
            planar += lean * (t.slide_steer * dt);
            const f32 kept = f_max(sliding0 - (belly_sliding ? t.belly_friction : t.slide_friction) * dt, 0.0f);
            planar = length(planar) > 1e-4f ? normalize(planar) * kept : Vec3{};
        }
    } else {
        planar = approach(planar, wish, accel * dt);
    }

    bool jumped = false;
    if (s.grounded && cmd.jump && !s.downed && !jump_taken && !flat) {
        v_up = t.jump_speed;
        s.grounded = false;
        jumped = true;
    }
    if (wind_up > t.lift_threshold) {
        s.grounded = false;
        v_up += (wind_up - v_up) * t.lift_coupling * dt;
    }
    if (s.grounded) {
        v_up = 0.0f;
    }
    s.vel = planar + u * v_up;
    if (!s.grounded) {
        s.vel += g.gravity * dt;
    }

    f32 lean = sliding ? kSlideLeanDeg : 0.0f;
    if (s.stance == Stance::Dive) {
        const f32 going = length(planar);
        const f32 sideways = going > 0.5f ? dot(planar * (1.0f / going), right) : 0.0f;
        lean = -kDiveLeanDeg * sideways;
    }
    s.tilt += (lean * kDegToRad - s.tilt) * (1.0f - std::exp(-kLeanRate * dt));

    if (!jolt_ || !jolt_->characterValid(id())) {
        s.pos += s.vel * dt;
        return;
    }

    apply_stance_height();
    Vec3 move_vel = s.vel;
    if (s.grounded) {
        const Vec3 n = s.ground_normal;
        const f32 move_speed = length(move_vel);
        const f32 facing = f_max(dot(u, n), kSlopeFacingMin);
        Vec3 along = move_vel - u * (dot(move_vel, n) / facing);
        const f32 along_len = length(along);
        if (along_len > 1e-4f) {
            along *= move_speed / along_len;
        }
        move_vel = along + s.ground_vel - n * kGroundHug;
    }
    const ghost::engine::CharacterMove moved = jolt_->characterMove(id(), to_glm(move_vel), to_glm(u), to_glm(g.gravity), dt,
                                                      s.grounded && !jumped);
    s.pos = from_glm(moved.position);
    for (int i = 0; i < moved.normalCount && !moved.stepped; i++) {
        const Vec3 wall = from_glm(moved.normals[static_cast<size_t>(i)]);
        if (dot(wall, u) > kWallNy) {
            continue;
        }
        const f32 into = dot(s.vel, wall);
        if (into < 0.0f) {
            s.vel -= wall * into;
        }
    }
    const bool rising = dot(s.vel, u) > kRiseFromGround;
    const bool landed = moved.grounded && !jumped && !rising;
    if (landed) {
        s.vel -= u * dot(s.vel, u);
    }
    s.grounded = landed;
    s.ground_vel = landed ? from_glm(moved.groundVelocity) : Vec3{};
    s.ground_normal = landed ? from_glm(moved.groundNormal) : u;
}

void Movement::tick_hands(bool holster, bool aim, bool busy, bool force_holster, f32 dt)
{
    MoveState& s = state_;
    if (force_holster) {
        s.holstered = true;
    } else if (holster && !busy && !s.downed) {
        s.holstered = !s.holstered;
    }
    const f32 holster_rate = 1.0f / f_max(s.holstered ? tuning_.holster_time : tuning_.draw_time, 0.01f);
    s.holster = f_clamp01(s.holster + (s.holstered ? dt : -dt) * holster_rate);
    s.aiming = aim && !busy && s.holster <= 0.0f;
    previous_.holster = s.holster;
    previous_.holstered = s.holstered;
}

}
