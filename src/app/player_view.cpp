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
constexpr f32 kFovBaseDeg = 70.0f;
constexpr f32 kFovSpeedDeg = 12.0f;
constexpr f32 kFovSpeedFull = 40.0f;
constexpr f32 kFovRate = 4.0f;
constexpr f32 kCockpitRollFrac = 0.35f;
constexpr f32 kChaseLateral = 0.5f;
constexpr f32 kChaseRoll = 0.05f;
constexpr f32 kLatGRate = 5.0f;
constexpr f32 kShakeTime = 0.35f;
constexpr f32 kShakeDeg = 2.5f;
constexpr f32 kBehindRate = 12.0f;

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

void PlayerView::kick(f32 strength)
{
    shake_t_ = kShakeTime;
    shake_amp_ = kShakeDeg * kDegToRad * f_clamp01(strength);
}

void PlayerView::seated_feel(const RigidBody& body, Quat body_rot, f32 speed_plan, f32 dt, Camera& out)
{
    (void)body;
    (void)body_rot;
    const f32 fov_target = kFovBaseDeg + kFovSpeedDeg * f_clamp01(speed_plan / kFovSpeedFull);
    fov_sm_ = fov_sm_ <= 0.0f ? fov_target : f_approach_exp(fov_sm_, fov_target, kFovRate, dt);
    out.fov_y = fov_sm_ * kDegToRad;

    behind_ = f_approach_exp(behind_, look_behind_ ? 1.0f : 0.0f, kBehindRate, dt);
    out.yaw = f_wrap_angle(out.yaw + behind_ * kPi);

    if (shake_t_ > 0.0f) {
        shake_t_ = f_max(shake_t_ - dt, 0.0f);
        shake_phase_ += dt;
        const f32 s = f_clamp01(shake_t_ / kShakeTime);
        const f32 amp = shake_amp_ * s * s;
        out.yaw += std::sin(shake_phase_ * 91.0f) * amp;
        out.pitch += std::sin(shake_phase_ * 77.0f + 1.3f) * amp;
        out.roll += std::sin(shake_phase_ * 103.0f + 2.1f) * amp * 0.6f;
    }
}

void PlayerView::chase_camera(const Player& player, PhysWorld& phys, const RigidBody& body, Vec3 body_pos,
                              Quat body_rot, f32 car_yaw, f32 speed_plan, f32 dt, Camera& out)
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

    const Vec3 up_f = frame_up(car_frame);
    const f32 yaw_rate = dot(body.angular_vel, up_f);
    lat_g_ = f_approach_exp(lat_g_, f_clamp(yaw_rate * speed_plan / 9.81f, -1.0f, 1.0f), kLatGRate, dt);
    const Vec3 right = normalize(cross(dir, up_f));
    const Vec3 pivot = body_pos + up_f * kChasePivotHeight - right * (kChaseLateral * lat_g_);
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
    out.roll = kChaseRoll * lat_g_;
    cockpit_eye_valid_ = false;
    seated_feel(body, body_rot, speed_plan, dt, out);
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
        out.fov_y = kFovBaseDeg * kDegToRad;
        fov_sm_ = 0.0f;
        behind_ = 0.0f;
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

    const Vec3 v_local = rotate(conjugate(body_rot), body->vel);
    const f32 speed_plan = std::sqrt(v_local.x * v_local.x + v_local.z * v_local.z);
    if (chase) {
        chase_camera(player, phys, *body, body_pos, body_rot, car_yaw, speed_plan, dt, out);
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
    const Vec3 body_right = rotate(body_rot, Vec3{1.0f, 0.0f, 0.0f});
    out.roll = kCockpitRollFrac * std::asin(f_clamp(dot(body_right, frame_up(car_frame)), -1.0f, 1.0f));
    seated_feel(*body, body_rot, speed_plan, dt, out);
}

}
