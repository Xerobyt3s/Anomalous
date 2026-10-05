#include "player/player.h"
#include "physics/gravity_field.h"
#include "physics/jolt_world.h"
#include "physics/world.h"
#include "render/camera.h"
#include "vehicle/vehicle.h"

namespace anom {
namespace {

constexpr f32 kWalkableNy = 0.64f;
constexpr f32 kEnterTime = 0.45f;
constexpr f32 kExitTime = 0.4f;
constexpr f32 kExitMaxSpeed = 1.5f;
constexpr f32 kEnterRange = 2.2f;
constexpr f32 kEnterMaxCarSpeed = 2.0f;
constexpr f32 kLookSensitivity = 0.0022f;
constexpr f32 kLookYawLimit = 2.4f;
constexpr f32 kLookPitchLimit = 1.0f;
constexpr f32 kPitchLimit = 89.0f * kDegToRad;
constexpr f32 kCockpitLagRate = 18.0f;
constexpr f32 kCockpitLagMax = 0.15f;
constexpr f32 kChasePivotHeight = 1.30f;
constexpr f32 kChaseNearDist = 5.0f;
constexpr f32 kChaseFarDist = 7.2f;
constexpr f32 kChaseSpeedFull = 38.0f;
constexpr f32 kChasePitch = 10.0f * kDegToRad;
constexpr f32 kChaseYawRate = 3.2f;
constexpr f32 kChaseDistRate = 3.5f;
constexpr f32 kChaseFacingBlend = 0.28f;
constexpr f32 kChaseHeadingSpeed = 2.0f;
constexpr f32 kChaseClearance = 0.34f;
constexpr f32 kChaseMinDist = 0.9f;
constexpr f32 kChaseLookHold = 1.4f;
constexpr f32 kChaseLookReturn = 2.4f;
constexpr f32 kChaseLookPitchLimit = 1.15f;
constexpr f32 kCarUpEase = 3.0f;
constexpr f32 kCarUpMaxRate = 2.2f;

f32 smooth01(f32 t)
{
    t = f_clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

Vec3 seat_eye_world(const Vehicle& veh, const RigidBody& body)
{
    return body.pos + rotate(body.rot, veh.config().seat_eye);
}

Vec3 seat_foot_world(const Vehicle& veh, const RigidBody& body)
{
    return seat_eye_world(veh, body) - rotate(body.rot, Vec3{0.0f, kPlayerEyeHeight, 0.0f});
}

Vec3 gravity_up(const PhysWorld& phys, Vec3 p)
{
    const GravityField* field = phys.gravity_field();
    return field ? field->up_at(p) : Vec3{0.0f, 1.0f, 0.0f};
}

} // namespace

void Player::init(Vec3 pos, f32 yaw)
{
    *this = Player{};
    state_ = PlayerState::OnFoot;
    movement_.init(nullptr, pos, Vec3{0.0f, 1.0f, 0.0f}, yaw);
}

void Player::teleport(Vec3 pos, f32 yaw)
{
    seat_pos_ = pos;
    seat_prev_pos_ = pos;
    cockpit_eye_valid_ = false;
    chase_valid_ = false;
    movement_.teleport(pos);
    if (state_ == PlayerState::OnFoot) {
        movement_.look(f_wrap_angle(yaw - movement_.state().yaw), 0.0f);
    }
}

Vec3 Player::pos() const
{
    return state_ == PlayerState::OnFoot ? movement_.state().pos : seat_pos_;
}

Vec3 Player::prev_pos() const
{
    return state_ == PlayerState::OnFoot ? movement_.previous().pos : seat_prev_pos_;
}

Vec3 Player::vel() const
{
    return state_ == PlayerState::OnFoot ? movement_.state().vel : seat_vel_;
}

Vec3 Player::up() const
{
    return state_ == PlayerState::OnFoot ? movement_.up() : frame_up(car_frame_);
}

f32 Player::up_turn_rate() const
{
    return state_ == PlayerState::OnFoot ? movement_.state().up_turn_rate : car_turn_rate_;
}

f32 Player::field_presence() const
{
    return state_ == PlayerState::OnFoot ? movement_.state().field_presence : car_presence_;
}

void Player::sync_jolt(PhysWorld& phys, const RigidBody* car)
{
    JoltWorld* jolt = phys.jolt();
    if (!jolt) {
        return;
    }
    if (car) {
        jolt->set_car(car->pos, car->rot, car->half_extents, car->box_offset, car->vel,
                      car->angular_vel);
    } else {
        jolt->clear_car();
    }
}

bool Player::probe_exit(PhysWorld& phys, const Vehicle& veh, Vec3* out_foot) const
{
    const RigidBody* body = phys.body(veh.body());
    if (!body) {
        return false;
    }
    const Vec3 up = gravity_up(phys, body->pos);
    const Vec3 he = body->half_extents;
    f32 side = veh.config().seat_eye.x < 0.0f ? -1.0f : 1.0f;
    if (exit_pref_ != 0) {
        side = static_cast<f32>(exit_pref_);
    }
    const f32 out_x = he.x + kPlayerRadius + 0.45f;
    const f32 out_z = he.z + kPlayerRadius + 0.6f;

    Vec3 candidates[4];
    u32 candidate_count = 4;
    candidates[0] = Vec3{side * out_x, 0.0f, veh.config().seat_eye.z};
    candidates[1] = Vec3{-side * out_x, 0.0f, veh.config().seat_eye.z};
    candidates[2] = Vec3{0.0f, 0.0f, out_z};
    candidates[3] = Vec3{0.0f, 0.0f, -out_z};
    if (exit_pref_ != 0) {
        candidates[1] = candidates[2];
        candidates[2] = candidates[3];
        candidate_count = 3;
    }

    const JoltWorld* jolt = phys.jolt();
    for (u32 i = 0; i < candidate_count; i++) {
        const Vec3 world = body->pos + rotate(body->rot, candidates[i]);
        Ray ray;
        ray.origin = world + up * 1.5f;
        ray.dir = up * -1.0f;

        PhysRayHit hit{};
        if (!phys.raycast(ray, 4.0f, &hit)) {
            continue;
        }
        if (dot(hit.normal, up) < kWalkableNy) {
            continue;
        }
        const Vec3 foot = ray.origin + ray.dir * hit.t;
        if (!jolt || jolt->character_fits(foot, up, kPlayerHeight)) {
            if (out_foot) {
                *out_foot = foot;
            }
            return true;
        }
    }
    return false;
}

bool Player::can_enter(PhysWorld& phys, const Vehicle* veh) const
{
    if (state_ != PlayerState::OnFoot || !veh) {
        return false;
    }
    const RigidBody* body = phys.body(veh->body());
    if (!body || length(body->vel) > kEnterMaxCarSpeed) {
        return false;
    }
    const Vec3 he = body->half_extents;
    const Vec3 waist = movement_.state().pos + movement_.up() * 0.9f;
    const f32 side = veh->config().seat_eye.x < 0.0f ? -1.0f : 1.0f;
    const Vec3 anchor_local{side * (he.x + 0.4f), 0.0f, veh->config().seat_eye.z};
    const Vec3 anchor = body->pos + rotate(body->rot, anchor_local);
    return distance(waist, anchor) < kEnterRange;
}

bool Player::can_exit(PhysWorld& phys, const Vehicle* veh) const
{
    if (state_ != PlayerState::Driving || !veh) {
        return false;
    }
    const RigidBody* body = phys.body(veh->body());
    if (!body || length(body->vel) > kExitMaxSpeed) {
        return false;
    }
    return probe_exit(phys, *veh, nullptr);
}

void Player::tick(PhysWorld& phys, Vehicle* veh, const PlayerCommand& cmd, f32 dt)
{
    RigidBody* body = veh ? phys.body(veh->body()) : nullptr;
    sync_jolt(phys, body);
    seat_prev_pos_ = seat_pos_;

    switch (state_) {
    case PlayerState::OnFoot:
        if (cmd.interact && can_enter(phys, veh)) {
            state_ = PlayerState::Entering;
            transition_t_ = 0.0f;
            transition_eye_ = movement_.eye();
            transition_frame_ = movement_.state().frame;
            transition_yaw_ = movement_.state().yaw;
            transition_pitch_ = movement_.state().pitch;
            car_frame_ = movement_.state().frame;
            look_yaw_ = 0.0f;
            look_pitch_ = 0.0f;
            seat_pos_ = movement_.state().pos;
            seat_prev_pos_ = seat_pos_;
        } else {
            movement_.attach(phys.jolt());
            MoveCommand move;
            move.move = Vec2{cmd.move_x, cmd.move_z};
            move.sprint = cmd.run;
            move.jump = cmd.jump;
            move.crouch = cmd.crouch;
            move.crawl = cmd.crawl;
            movement_.tick(move, phys.gravity_field(), dt);
        }
        break;

    case PlayerState::Entering:
        transition_t_ += dt / kEnterTime;
        if (body && veh) {
            seat_pos_ = seat_foot_world(*veh, *body);
        }
        if (transition_t_ >= 1.0f) {
            transition_t_ = 1.0f;
            state_ = PlayerState::Driving;
            cockpit_eye_valid_ = false;
        }
        break;

    case PlayerState::Driving:
        if (body && veh) {
            seat_pos_ = seat_foot_world(*veh, *body);
            seat_vel_ = body->vel;
            if (cmd.interact && length(body->vel) <= kExitMaxSpeed) {
                Vec3 foot;
                if (probe_exit(phys, *veh, &foot)) {
                    state_ = PlayerState::Exiting;
                    transition_t_ = 0.0f;
                    exit_pos_ = foot;
                    transition_eye_ = cockpit_eye_valid_ ? cockpit_eye_ : seat_eye_world(*veh, *body);
                    const Vec3 fwd = rotate(body->rot, Vec3{0.0f, 0.0f, -1.0f});
                    f32 car_yaw = 0.0f;
                    f32 car_pitch = 0.0f;
                    frame_view_angles(car_frame_, fwd, car_yaw, car_pitch);
                    transition_frame_ = car_frame_;
                    transition_yaw_ = f_wrap_angle(car_yaw + look_yaw_);
                    transition_pitch_ = f_clamp(car_pitch + look_pitch_, -kPitchLimit, kPitchLimit);
                }
            }
        }
        break;

    case PlayerState::Exiting: {
        transition_t_ += dt / kExitTime;
        const f32 s = smooth01(transition_t_);
        const Vec3 from = (body && veh) ? seat_foot_world(*veh, *body) : exit_pos_;
        seat_pos_ = lerp(from, exit_pos_, s);
        if (transition_t_ >= 1.0f) {
            state_ = PlayerState::OnFoot;
            movement_.attach(phys.jolt());
            movement_.align_up(gravity_up(phys, exit_pos_));
            movement_.teleport(exit_pos_);
            movement_.face(frame_view(transition_frame_, transition_yaw_, transition_pitch_));
            movement_.settle(phys.gravity_field());
        }
        break;
    }
    }
}

void Player::look(f32 dx, f32 dy)
{
    if (state_ == PlayerState::OnFoot) {
        movement_.look(dx * kLookSensitivity, -dy * kLookSensitivity);
    } else if (state_ == PlayerState::Driving) {
        if (dx != 0.0f || dy != 0.0f) {
            look_idle_ = 0.0f;
        }
        if (chase_active_) {
            look_yaw_ = f_wrap_angle(look_yaw_ + dx * kLookSensitivity);
            look_pitch_ = f_clamp(look_pitch_ - dy * kLookSensitivity, -kChaseLookPitchLimit,
                                  kChaseLookPitchLimit);
        } else {
            look_yaw_ = f_clamp(look_yaw_ + dx * kLookSensitivity, -kLookYawLimit, kLookYawLimit);
            look_pitch_ = f_clamp(look_pitch_ - dy * kLookSensitivity, -kLookPitchLimit,
                                  kLookPitchLimit);
        }
    }
}

void Player::update_car_frame(PhysWorld& phys, Vec3 body_pos, f32 dt)
{
    const GravityField* field = phys.gravity_field();
    const GravitySample g = field ? field->sample(body_pos) : GravitySample{Vec3{0.0f, -kDefaultGravity, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 0.0f};
    car_presence_ = g.presence;
    const Vec3 current = frame_up(car_frame_);
    const f32 angle = std::acos(f_clamp(dot(current, g.up), -1.0f, 1.0f));
    car_turn_rate_ = 0.0f;
    if (angle < 1e-4f || dt <= 0.0f) {
        return;
    }
    const f32 step = f_min(angle, f_min(angle * (1.0f - std::exp(-kCarUpEase * dt)) + 0.1f * dt,
                                        kCarUpMaxRate * dt));
    car_frame_ = frame_turn_up(car_frame_, rotate_toward(current, g.up, step));
    car_turn_rate_ = step / dt;
}

void Player::chase_camera(PhysWorld& phys, const RigidBody& body, Vec3 body_pos, Quat body_rot,
                          f32 car_yaw, f32 dt, Camera& out)
{
    const Quat to_local = conjugate(car_frame_);
    const Vec3 fwd = rotate(to_local, rotate(body_rot, Vec3{0.0f, 0.0f, -1.0f}));
    const Vec3 local_vel = rotate(to_local, body.vel);
    const Vec3 flat{local_vel.x, 0.0f, local_vel.z};
    const f32 speed = length(flat);

    f32 heading = car_yaw;
    if (speed > kChaseHeadingSpeed && dot(flat, fwd) > 0.0f) {
        const f32 travel = std::atan2(flat.x, -flat.z);
        const f32 settled = f_clamp01((speed - kChaseHeadingSpeed) / kChaseHeadingSpeed);
        const f32 facing = f_lerp(1.0f, kChaseFacingBlend, settled);
        heading = f_wrap_angle(travel + f_wrap_angle(car_yaw - travel) * facing);
    }

    if (!chase_valid_) {
        chase_yaw_ = heading;
        chase_prev_yaw_ = heading;
        chase_dist_ = kChaseNearDist;
        look_idle_ = kChaseLookHold;
        chase_valid_ = true;
    }
    chase_yaw_ = f_wrap_angle(chase_yaw_ + f_wrap_angle(heading - chase_yaw_) *
                                               (1.0f - std::exp(-kChaseYawRate * dt)));

    look_idle_ += dt;
    if (look_idle_ < kChaseLookHold) {
        look_yaw_ = f_wrap_angle(look_yaw_ - f_wrap_angle(chase_yaw_ - chase_prev_yaw_));
    } else {
        look_yaw_ = f_approach_exp(look_yaw_, 0.0f, kChaseLookReturn, dt);
        look_pitch_ = f_approach_exp(look_pitch_, 0.0f, kChaseLookReturn, dt);
    }
    chase_prev_yaw_ = chase_yaw_;

    const f32 yaw = f_wrap_angle(chase_yaw_ + look_yaw_);
    const f32 pitch = f_clamp(-kChasePitch + look_pitch_, -kPitchLimit, kPitchLimit);
    const Vec3 dir = frame_view(car_frame_, yaw, pitch);

    const Vec3 pivot = body_pos + frame_up(car_frame_) * kChasePivotHeight;
    f32 want = f_lerp(kChaseNearDist, kChaseFarDist, f_clamp01(speed / kChaseSpeedFull));

    PhysRayHit hit;
    if (phys.raycast(Ray{pivot, dir * -1.0f}, want + kChaseClearance, &hit)) {
        want = f_max(hit.t - kChaseClearance, kChaseMinDist);
    }
    chase_dist_ = want < chase_dist_ ? want
                                     : f_approach_exp(chase_dist_, want, kChaseDistRate, dt);

    out.pos = pivot - dir * chase_dist_;
    out.frame = car_frame_;
    out.yaw = yaw;
    out.pitch = pitch;
    out.roll = 0.0f;
    cockpit_eye_valid_ = false;
}

void Player::camera(PhysWorld& phys, const Vehicle* veh, f32 alpha, f32 dt, bool chase,
                    Camera& out)
{
    const RigidBody* body = veh ? phys.body(veh->body()) : nullptr;

    if (state_ == PlayerState::OnFoot || !body) {
        const MoveState& now = movement_.state();
        const MoveState& before = movement_.previous();
        const Quat frame = slerp(before.frame, now.frame, alpha);
        out.pos = lerp(before.pos, now.pos, alpha)
                + frame_up(frame) * f_lerp(before.eye_height, now.eye_height, alpha);
        out.frame = frame;
        out.yaw = now.yaw;
        out.pitch = now.pitch;
        out.roll = now.tilt;
        cockpit_eye_valid_ = false;
        chase_valid_ = false;
        car_frame_ = now.frame;
        car_turn_rate_ = now.up_turn_rate;
        car_presence_ = now.field_presence;
        return;
    }

    const Vec3 body_pos = lerp(body->prev_pos, body->pos, alpha);
    const Quat body_rot = slerp(body->prev_rot, body->rot, alpha);
    update_car_frame(phys, body_pos, dt);
    const Vec3 seat_eye = body_pos + rotate(body_rot, veh->config().seat_eye);
    f32 car_yaw = 0.0f;
    f32 car_pitch = 0.0f;
    frame_view_angles(car_frame_, rotate(body_rot, Vec3{0.0f, 0.0f, -1.0f}), car_yaw, car_pitch);
    out.roll = 0.0f;

    if (state_ == PlayerState::Entering) {
        const f32 s = smooth01(transition_t_);
        out.pos = lerp(transition_eye_, seat_eye, s);
        out.frame = slerp(transition_frame_, car_frame_, s);
        out.yaw = transition_yaw_ + f_wrap_angle(car_yaw - transition_yaw_) * s;
        out.pitch = transition_pitch_ + (car_pitch - transition_pitch_) * s;
        cockpit_eye_valid_ = false;
        return;
    }
    if (state_ == PlayerState::Exiting) {
        const f32 s = smooth01(transition_t_);
        const Vec3 exit_eye = exit_pos_ + gravity_up(phys, exit_pos_) * kPlayerEyeHeight;
        out.pos = lerp(transition_eye_, exit_eye, s);
        out.frame = transition_frame_;
        out.yaw = transition_yaw_;
        out.pitch = transition_pitch_;
        cockpit_eye_valid_ = false;
        return;
    }

    chase_active_ = chase;
    if (chase) {
        chase_camera(phys, *body, body_pos, body_rot, car_yaw, dt, out);
        return;
    }
    chase_valid_ = false;

    if (!cockpit_eye_valid_) {
        cockpit_eye_ = seat_eye;
        cockpit_eye_valid_ = true;
    }
    cockpit_eye_ = Vec3{f_approach_exp(cockpit_eye_.x, seat_eye.x, kCockpitLagRate, dt),
                        f_approach_exp(cockpit_eye_.y, seat_eye.y, kCockpitLagRate, dt),
                        f_approach_exp(cockpit_eye_.z, seat_eye.z, kCockpitLagRate, dt)};

    Vec3 offset = cockpit_eye_ - seat_eye;
    const f32 offset_len = length(offset);
    if (offset_len > kCockpitLagMax) {
        offset *= kCockpitLagMax / offset_len;
        cockpit_eye_ = seat_eye + offset;
    }

    out.pos = cockpit_eye_;
    out.frame = car_frame_;
    out.yaw = f_wrap_angle(car_yaw + look_yaw_);
    out.pitch = f_clamp(car_pitch + look_pitch_, -kPitchLimit, kPitchLimit);
}

} // namespace anom
