#include "app/player_view.h"
#include "physics/gravity_field.h"
#include "physics/world.h"
#include "player/player.h"
#include "render/camera.h"
#include "vehicle/vehicle.h"

namespace anom {
namespace {
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

f32 smooth01(f32 t)
{
    t = f_clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

Vec3 gravity_up(const PhysWorld& phys, Vec3 p)
{
    const GravityField* field = phys.gravity_field();
    return field ? field->up_at(p) : Vec3{0.0f, 1.0f, 0.0f};
}

}

void PlayerView::reset()
{
    *this = PlayerView{};
}

void PlayerView::chase_look(f32 dx, f32 dy)
{
    if (dx != 0.0f || dy != 0.0f) {
        look_idle_ = 0.0f;
    }
    chase_look_yaw_ = f_wrap_angle(chase_look_yaw_ + dx * kLookSensitivity);
    chase_look_pitch_ = f_clamp(chase_look_pitch_ - dy * kLookSensitivity, -kChaseLookPitchLimit,
                                kChaseLookPitchLimit);
}

void PlayerView::chase_camera(const Player& player, PhysWorld& phys, const RigidBody& body, Vec3 body_pos,
                              Quat body_rot, f32 car_yaw, f32 dt, Camera& out)
{
    const Quat car_frame = player.car_frame();
    const Quat to_local = conjugate(car_frame);
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
    chase_yaw_ = f_wrap_angle(chase_yaw_ + f_wrap_angle(heading - chase_yaw_) * (1.0f - std::exp(-kChaseYawRate * dt)));

    look_idle_ += dt;
    if (look_idle_ < kChaseLookHold) {
        chase_look_yaw_ = f_wrap_angle(chase_look_yaw_ - f_wrap_angle(chase_yaw_ - chase_prev_yaw_));
    } else {
        chase_look_yaw_ = f_approach_exp(chase_look_yaw_, 0.0f, kChaseLookReturn, dt);
        chase_look_pitch_ = f_approach_exp(chase_look_pitch_, 0.0f, kChaseLookReturn, dt);
    }
    chase_prev_yaw_ = chase_yaw_;

    const f32 yaw = f_wrap_angle(chase_yaw_ + chase_look_yaw_);
    const f32 pitch = f_clamp(-kChasePitch + chase_look_pitch_, -kPitchLimit, kPitchLimit);
    const Vec3 dir = frame_view(car_frame, yaw, pitch);

    const Vec3 pivot = body_pos + frame_up(car_frame) * kChasePivotHeight;
    f32 want = f_lerp(kChaseNearDist, kChaseFarDist, f_clamp01(speed / kChaseSpeedFull));

    PhysRayHit hit;
    if (phys.raycast(Ray{pivot, dir * -1.0f}, want + kChaseClearance, &hit)) {
        want = f_max(hit.t - kChaseClearance, kChaseMinDist);
    }
    chase_dist_ = want < chase_dist_ ? want : f_approach_exp(chase_dist_, want, kChaseDistRate, dt);

    out.pos = pivot - dir * chase_dist_;
    out.frame = car_frame;
    out.yaw = yaw;
    out.pitch = pitch;
    out.roll = 0.0f;
    cockpit_eye_valid_ = false;
}

void PlayerView::camera(const Player& player, PhysWorld& phys, const Vehicle* veh, f32 alpha, f32 dt, bool chase,
                        f32 pending_dx, f32 pending_dy, Camera& out)
{
    const RigidBody* body = veh ? phys.body(veh->body()) : nullptr;

    if (player.state() == PlayerState::OnFoot || !body) {
        const MoveState& now = player.movement().state();
        const MoveState& before = player.movement().previous();
        const Quat frame = slerp(before.frame, now.frame, alpha);
        out.pos = lerp(before.pos, now.pos, alpha) + frame_up(frame) * f_lerp(before.eye_height, now.eye_height, alpha);
        out.frame = frame;
        out.yaw = f_wrap_angle(now.yaw + pending_dx * kLookSensitivity);
        out.pitch = f_clamp(now.pitch - pending_dy * kLookSensitivity, -kPitchLimit, kPitchLimit);
        out.roll = now.tilt;
        cockpit_eye_valid_ = false;
        chase_valid_ = false;
        return;
    }

    const Vec3 body_pos = lerp(body->prev_pos, body->pos, alpha);
    const Quat body_rot = slerp(body->prev_rot, body->rot, alpha);
    const Quat car_frame = player.car_frame();
    const VehicleConfig& cfg = veh->config();
    const Vec3 seat_eye = body_pos + rotate(body_rot, cfg.seats[player.seat() < cfg.seat_count ? player.seat() : 0].eye);
    f32 car_yaw = 0.0f;
    f32 car_pitch = 0.0f;
    frame_view_angles(car_frame, rotate(body_rot, Vec3{0.0f, 0.0f, -1.0f}), car_yaw, car_pitch);
    out.roll = 0.0f;

    if (player.state() == PlayerState::Entering) {
        const f32 s = smooth01(player.transition_t());
        out.pos = lerp(player.transition_eye(), seat_eye, s);
        out.frame = slerp(player.transition_frame(), car_frame, s);
        out.yaw = player.transition_yaw() + f_wrap_angle(car_yaw - player.transition_yaw()) * s;
        out.pitch = player.transition_pitch() + (car_pitch - player.transition_pitch()) * s;
        cockpit_eye_valid_ = false;
        return;
    }
    if (player.state() == PlayerState::Exiting) {
        const f32 s = smooth01(player.transition_t());
        const Vec3 exit_eye = player.exit_pos() + gravity_up(phys, player.exit_pos()) * kPlayerEyeHeight;
        out.pos = lerp(player.transition_eye(), exit_eye, s);
        out.frame = player.transition_frame();
        out.yaw = player.transition_yaw();
        out.pitch = player.transition_pitch();
        cockpit_eye_valid_ = false;
        return;
    }

    if (chase) {
        chase_camera(player, phys, *body, body_pos, body_rot, car_yaw, dt, out);
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

    const f32 look_yaw = f_clamp(player.look_yaw() + pending_dx * kLookSensitivity, -kLookYawLimit, kLookYawLimit);
    const f32 look_pitch = f_clamp(player.look_pitch() - pending_dy * kLookSensitivity, -kLookPitchLimit,
                                   kLookPitchLimit);
    out.pos = cockpit_eye_;
    out.frame = car_frame;
    out.yaw = f_wrap_angle(car_yaw + look_yaw);
    out.pitch = f_clamp(car_pitch + look_pitch, -kPitchLimit, kPitchLimit);
}

}
