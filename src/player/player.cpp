#include "player/player.h"
#include "physics/gravity_field.h"
#include "engine/physics/physics_world.h"
#include "game/ballistics/surface.h"
#include "math/glm_bridge.h"
#include "physics/world.h"
#include "vehicle/vehicle.h"

namespace anom {
namespace {
constexpr f32 kWalkableNy = 0.64f;
constexpr f32 kEnterTime = 0.45f;
constexpr f32 kExitTime = 0.4f;
constexpr f32 kExitMaxSpeed = 1.5f;
constexpr f32 kEnterRange = 2.2f;
constexpr f32 kEnterMaxCarSpeed = 2.0f;
constexpr f32 kPitchLimit = 89.0f * kDegToRad;
constexpr f32 kCarUpEase = 3.0f;
constexpr f32 kCarUpMaxRate = 2.2f;

f32 smooth01(f32 t)
{
    t = f_clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

const SeatConfig& seat_of(const Vehicle& veh, u32 seat)
{
    return veh.config().seats[seat < veh.config().seat_count ? seat : 0];
}

Vec3 seat_eye_world(const Vehicle& veh, const RigidBody& body, u32 seat)
{
    return body.pos + rotate(body.rot, seat_of(veh, seat).eye);
}

Vec3 seat_foot_world(const Vehicle& veh, const RigidBody& body, u32 seat)
{
    return seat_eye_world(veh, body, seat) - rotate(body.rot, Vec3{0.0f, kPlayerEyeHeight, 0.0f});
}

Vec3 gravity_up(const PhysWorld& phys, Vec3 p)
{
    const GravityField* field = phys.gravity_field();
    return field ? field->up_at(p) : Vec3{0.0f, 1.0f, 0.0f};
}

}

void Player::init(Vec3 pos, f32 yaw)
{
    const u32 character = movement_.character();
    *this = Player{};
    state_ = PlayerState::OnFoot;
    movement_.set_character(character);
    movement_.init(nullptr, pos, Vec3{0.0f, 1.0f, 0.0f}, yaw);
}

void Player::adopt(const Player& other)
{
    Movement movement = movement_;
    *this = other;
    movement.adopt(other.movement_);
    movement_ = movement;
}

void Player::eject(PhysWorld& phys, const Vehicle& veh)
{
    if (state_ == PlayerState::OnFoot) {
        return;
    }
    Vec3 foot = seat_pos_;
    if (!probe_exit(phys, veh, &foot)) {
        foot = seat_pos_;
    }
    state_ = PlayerState::OnFoot;
    transition_t_ = 0.0f;
    movement_.attach(phys.jolt());
    movement_.align_up(gravity_up(phys, foot));
    movement_.teleport(foot);
    movement_.settle(phys.gravity_field());
}

void Player::teleport(Vec3 pos, f32 yaw)
{
    seat_pos_ = pos;
    seat_prev_pos_ = pos;
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
    ghost::engine::PhysicsWorld* jolt = phys.jolt();
    if (!jolt) {
        return;
    }
    jolt->characterSetSolid(static_cast<int>(movement_.character()), state_ == PlayerState::OnFoot);
    if (car) {
        jolt->setVehicle(to_glm(car->pos), to_glm(car->rot), to_glm(car->half_extents), to_glm(car->box_offset),
                         to_glm(car->vel), to_glm(car->angular_vel),
                         static_cast<std::uint64_t>(ghost::game::Surface::Steel));
    } else {
        jolt->clearVehicle();
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
    const SeatConfig& seat = seat_of(veh, seat_);
    f32 side = seat.door_side == 0 ? -1.0f : 1.0f;
    if (exit_pref_ != 0) {
        side = static_cast<f32>(exit_pref_);
    }
    const f32 out_x = he.x + kPlayerRadius + 0.45f;
    const f32 out_z = he.z + kPlayerRadius + 0.6f;

    Vec3 candidates[4];
    u32 candidate_count = 4;
    candidates[0] = Vec3{side * out_x, 0.0f, seat.eye.z};
    candidates[1] = Vec3{-side * out_x, 0.0f, seat.eye.z};
    candidates[2] = Vec3{0.0f, 0.0f, out_z};
    candidates[3] = Vec3{0.0f, 0.0f, -out_z};
    if (exit_pref_ != 0) {
        candidates[1] = candidates[2];
        candidates[2] = candidates[3];
        candidate_count = 3;
    }

    const ghost::engine::PhysicsWorld* jolt = phys.jolt();
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
        if (!jolt || jolt->characterFits(static_cast<int>(movement_.character()), to_glm(foot), to_glm(up), kPlayerHeight)) {
            if (out_foot) {
                *out_foot = foot;
            }
            return true;
        }
    }
    return false;
}

bool Player::can_enter(PhysWorld& phys, const Vehicle* veh, u32 seat_index) const
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
    const SeatConfig& seat = seat_of(*veh, seat_index);
    const f32 side = seat.door_side == 0 ? -1.0f : 1.0f;
    const Vec3 anchor_local{side * (he.x + 0.4f), 0.0f, seat.eye.z};
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
    look(cmd.look_dx, cmd.look_dy);
    if (cmd.face_view && state_ == PlayerState::OnFoot && length_sq(cmd.view_dir) > 1e-8f) {
        movement_.face(normalize(cmd.view_dir));
    }
    if (state_ == PlayerState::OnFoot || !body) {
        car_frame_ = movement_.state().frame;
        car_turn_rate_ = movement_.state().up_turn_rate;
        car_presence_ = movement_.state().field_presence;
    } else {
        update_car_frame(phys, body->pos, dt);
    }

    switch (state_) {
    case PlayerState::OnFoot:
        if (cmd.interact && can_enter(phys, veh, seat_)) {
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
            move.aim = cmd.aim;
            move.holster = cmd.holster;
            move.hands_busy = cmd.hands_busy;
            move.wind = cmd.wind;
            movement_.tick(move, phys.gravity_field(), dt);
        }
        break;

    case PlayerState::Entering:
        transition_t_ += dt / kEnterTime;
        if (body && veh) {
            seat_pos_ = seat_foot_world(*veh, *body, seat_);
            movement_.tick_hands(false, false, cmd.hands_busy, seat_of(*veh, seat_).drives, dt);
        }
        if (transition_t_ >= 1.0f) {
            transition_t_ = 1.0f;
            state_ = PlayerState::Driving;
        }
        break;

    case PlayerState::Driving:
        if (body && veh) {
            seat_pos_ = seat_foot_world(*veh, *body, seat_);
            movement_.tick_hands(cmd.holster, cmd.aim, cmd.hands_busy, seat_of(*veh, seat_).drives, dt);
            seat_vel_ = body->vel;
            if (cmd.interact && length(body->vel) <= kExitMaxSpeed) {
                Vec3 foot;
                if (probe_exit(phys, *veh, &foot)) {
                    state_ = PlayerState::Exiting;
                    transition_t_ = 0.0f;
                    exit_pos_ = foot;
                    transition_eye_ = seat_eye_world(*veh, *body, seat_);
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
        const Vec3 from = (body && veh) ? seat_foot_world(*veh, *body, seat_) : exit_pos_;
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
        look_yaw_ = f_clamp(look_yaw_ + dx * kLookSensitivity, -kLookYawLimit, kLookYawLimit);
        look_pitch_ = f_clamp(look_pitch_ - dy * kLookSensitivity, -kLookPitchLimit, kLookPitchLimit);
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

}
