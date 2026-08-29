#include "player/player.h"
#include "physics/collide.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "render/camera.h"
#include "vehicle/vehicle.h"

namespace anom {
namespace {

constexpr f32 kGroundAccel = 45.0f;
constexpr f32 kAirAccel = 10.0f;
constexpr f32 kJumpSpeed = 4.2f;
constexpr f32 kSnapDown = 0.28f;
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
constexpr f32 kExitClearance = 0.03f;
constexpr f32 kCockpitLagRate = 18.0f;
constexpr f32 kCockpitLagMax = 0.15f;

f32 smooth01(f32 t)
{
    t = f_clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

void sphere_centers(Vec3 foot, Vec3 out[kPlayerSpheres])
{
    const f32 bottom = kPlayerStepHeight + kPlayerRadius;
    const f32 top = kPlayerHeight - kPlayerRadius;
    out[0] = foot + Vec3{0.0f, bottom, 0.0f};
    out[1] = foot + Vec3{0.0f, (bottom + top) * 0.5f, 0.0f};
    out[2] = foot + Vec3{0.0f, top, 0.0f};
}

bool sphere_vs_car(const RigidBody& car, Vec3 center, f32 radius, SphereContact& out)
{
    const Vec3 local = rotate(conjugate(car.rot), center - car.pos);
    const Vec3 he = car.half_extents;
    const Vec3 clamped{f_clamp(local.x, -he.x, he.x), f_clamp(local.y, -he.y, he.y),
                       f_clamp(local.z, -he.z, he.z)};
    const Vec3 delta = local - clamped;
    const f32 dist_sq = length_sq(delta);
    if (dist_sq > radius * radius) {
        return false;
    }
    if (dist_sq > 1e-8f) {
        const f32 dist = std::sqrt(dist_sq);
        out.normal = rotate(car.rot, delta * (1.0f / dist));
        out.depth = radius - dist;
        out.point = car.pos + rotate(car.rot, clamped);
        return true;
    }

    const f32 pen_x = he.x - f_abs(local.x);
    const f32 pen_y = he.y - f_abs(local.y);
    const f32 pen_z = he.z - f_abs(local.z);
    Vec3 axis;
    f32 pen;
    if (pen_x <= pen_y && pen_x <= pen_z) {
        axis = Vec3{local.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
        pen = pen_x;
    } else if (pen_y <= pen_z) {
        axis = Vec3{0.0f, local.y >= 0.0f ? 1.0f : -1.0f, 0.0f};
        pen = pen_y;
    } else {
        axis = Vec3{0.0f, 0.0f, local.z >= 0.0f ? 1.0f : -1.0f};
        pen = pen_z;
    }
    out.normal = rotate(car.rot, axis);
    out.depth = pen + radius;
    out.point = center;
    return true;
}

bool deepest_contact(PhysWorld& phys, const RigidBody* car, Vec3 foot, f32 radius,
                     SphereContact* out)
{
    Vec3 centers[kPlayerSpheres];
    sphere_centers(foot, centers);

    bool found = false;
    SphereContact best{};
    for (u32 s = 0; s < kPlayerSpheres; s++) {
        Sphere sphere;
        sphere.center = centers[s];
        sphere.radius = radius;

        SphereContact contact{};
        if (phys.heightfield() && collide_sphere_heightfield(*phys.heightfield(), sphere, contact)) {
            if (!found || contact.depth > best.depth) {
                best = contact;
                found = true;
            }
        }
        SphereContact statics[4];
        const u32 count = collide_sphere_statics(phys.statics(), sphere, statics, 4);
        for (u32 i = 0; i < count; i++) {
            if (!found || statics[i].depth > best.depth) {
                best = statics[i];
                found = true;
            }
        }
        if (car && sphere_vs_car(*car, centers[s], radius, contact)) {
            if (!found || contact.depth > best.depth) {
                best = contact;
                found = true;
            }
        }
    }
    if (found && out) {
        *out = best;
    }
    return found;
}

Vec3 seat_eye_world(const Vehicle& veh, const RigidBody& body)
{
    return body.pos + rotate(body.rot, veh.config().seat_eye);
}

Vec3 seat_foot_world(const Vehicle& veh, const RigidBody& body)
{
    return seat_eye_world(veh, body) - Vec3{0.0f, kPlayerEyeHeight, 0.0f};
}

f32 body_yaw(const RigidBody& body)
{
    const Vec3 fwd = rotate(body.rot, Vec3{0.0f, 0.0f, -1.0f});
    return std::atan2(fwd.x, -fwd.z);
}

f32 body_pitch(const RigidBody& body)
{
    const Vec3 fwd = rotate(body.rot, Vec3{0.0f, 0.0f, -1.0f});
    return std::asin(f_clamp(fwd.y, -1.0f, 1.0f));
}

bool player_fits(PhysWorld& phys, const RigidBody* car, Vec3 foot)
{
    SphereContact contact{};
    if (!deepest_contact(phys, car, foot, kPlayerRadius, &contact)) {
        return true;
    }
    return contact.depth <= kExitClearance;
}

} // namespace

void Player::init(Vec3 pos, f32 yaw)
{
    *this = Player{};
    state_ = PlayerState::OnFoot;
    pos_ = pos;
    prev_pos_ = pos;
    yaw_ = yaw;
    speed_mul_ = 1.0f;
}

void Player::resolve_collisions(PhysWorld& phys, const RigidBody* car)
{
    for (u32 iter = 0; iter < 8; iter++) {
        SphereContact contact{};
        if (!deepest_contact(phys, car, pos_, kPlayerRadius, &contact) || contact.depth < 1e-4f) {
            break;
        }
        pos_ += contact.normal * contact.depth;
        const f32 into = dot(vel_, contact.normal);
        if (into < 0.0f) {
            vel_ -= contact.normal * into;
        }
    }
}

void Player::ground_snap(PhysWorld& phys, bool was_grounded)
{
    grounded_ = false;
    if (vel_.y > 0.01f) {
        return;
    }
    const f32 probe_up = kPlayerStepHeight + 0.05f;
    Ray ray;
    ray.origin = pos_ + Vec3{0.0f, probe_up, 0.0f};
    ray.dir = Vec3{0.0f, -1.0f, 0.0f};

    const f32 max_t = probe_up + (was_grounded ? kSnapDown : 0.02f);
    PhysRayHit hit{};
    if (!phys.raycast(ray, max_t, &hit)) {
        return;
    }
    if (hit.normal.y < kWalkableNy) {
        return;
    }
    pos_.y = ray.origin.y - hit.t;
    vel_.y = 0.0f;
    grounded_ = true;
}

void Player::move_on_foot(PhysWorld& phys, const RigidBody* car, const PlayerCommand& cmd, f32 dt)
{
    const Vec3 forward{std::sin(yaw_), 0.0f, -std::cos(yaw_)};
    const Vec3 right{std::cos(yaw_), 0.0f, std::sin(yaw_)};
    Vec3 wish = right * cmd.move_x + forward * cmd.move_z;
    const f32 wish_len = length(wish);
    if (wish_len > 1.0f) {
        wish *= 1.0f / wish_len;
    }
    const f32 target_speed = (cmd.run ? kPlayerRunSpeed : kPlayerWalkSpeed) * speed_mul_;

    bool was_grounded = grounded_;
    if (grounded_) {
        Vec3 hvel{vel_.x, 0.0f, vel_.z};
        Vec3 delta = wish * target_speed - hvel;
        const f32 delta_len = length(delta);
        const f32 max_change = kGroundAccel * dt;
        if (delta_len > max_change) {
            delta *= max_change / delta_len;
        }
        hvel += delta;
        vel_.x = hvel.x;
        vel_.z = hvel.z;
        if (cmd.jump) {
            vel_.y = kJumpSpeed;
            grounded_ = false;
            was_grounded = false;
        }
    } else {
        vel_ += wish * (kAirAccel * dt);
    }
    vel_.y += phys.gravity().y * dt;
    pos_ += vel_ * dt;

    resolve_collisions(phys, car);
    ground_snap(phys, was_grounded);

    if (phys.heightfield()) {
        const f32 surface = phys.heightfield()->sample(pos_.x, pos_.z);
        if (pos_.y < surface - 1.0f) {
            pos_.y = surface;
            vel_.y = 0.0f;
        }
    }
}

bool Player::probe_exit(PhysWorld& phys, const Vehicle& veh, Vec3* out_foot) const
{
    const RigidBody* body = phys.body(veh.body());
    if (!body) {
        return false;
    }
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

    for (u32 i = 0; i < candidate_count; i++) {
        const Vec3 world = body->pos + rotate(body->rot, candidates[i]);
        Ray ray;
        ray.origin = world + Vec3{0.0f, 1.5f, 0.0f};
        ray.dir = Vec3{0.0f, -1.0f, 0.0f};

        PhysRayHit hit{};
        if (!phys.raycast(ray, 4.0f, &hit)) {
            continue;
        }
        if (hit.normal.y < kWalkableNy) {
            continue;
        }
        const Vec3 foot{world.x, ray.origin.y - hit.t, world.z};
        if (player_fits(phys, body, foot)) {
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
    const Vec3 waist = pos_ + Vec3{0.0f, 0.9f, 0.0f};
    for (i32 side = -1; side <= 1; side += 2) {
        const Vec3 anchor_local{static_cast<f32>(side) * (he.x + 0.4f), 0.0f,
                                veh->config().seat_eye.z};
        const Vec3 anchor = body->pos + rotate(body->rot, anchor_local);
        if (distance(waist, anchor) < kEnterRange) {
            return true;
        }
    }
    return false;
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
    prev_pos_ = pos_;
    RigidBody* body = veh ? phys.body(veh->body()) : nullptr;

    switch (state_) {
    case PlayerState::OnFoot:
        if (cmd.interact && can_enter(phys, veh)) {
            state_ = PlayerState::Entering;
            transition_t_ = 0.0f;
            transition_eye_ = pos_ + Vec3{0.0f, kPlayerEyeHeight, 0.0f};
            transition_yaw_ = yaw_;
            transition_pitch_ = pitch_;
            look_yaw_ = 0.0f;
            look_pitch_ = 0.0f;
            vel_ = Vec3{0.0f, 0.0f, 0.0f};
            grounded_ = false;
        } else {
            move_on_foot(phys, body, cmd, dt);
        }
        break;

    case PlayerState::Entering:
        transition_t_ += dt / kEnterTime;
        if (body && veh) {
            pos_ = seat_foot_world(*veh, *body);
        }
        if (transition_t_ >= 1.0f) {
            transition_t_ = 1.0f;
            state_ = PlayerState::Driving;
            cockpit_eye_valid_ = false;
        }
        break;

    case PlayerState::Driving:
        if (body && veh) {
            pos_ = seat_foot_world(*veh, *body);
            if (cmd.interact && length(body->vel) <= kExitMaxSpeed) {
                Vec3 foot;
                if (probe_exit(phys, *veh, &foot)) {
                    state_ = PlayerState::Exiting;
                    transition_t_ = 0.0f;
                    exit_pos_ = foot;
                    transition_eye_ = cockpit_eye_valid_ ? cockpit_eye_
                                                         : seat_eye_world(*veh, *body);
                    yaw_ = f_wrap_angle(body_yaw(*body) + look_yaw_);
                    pitch_ = f_clamp(body_pitch(*body) + look_pitch_, -kPitchLimit, kPitchLimit);
                }
            }
        }
        break;

    case PlayerState::Exiting: {
        transition_t_ += dt / kExitTime;
        const f32 s = smooth01(transition_t_);
        const Vec3 from = (body && veh) ? seat_foot_world(*veh, *body) : exit_pos_;
        pos_ = lerp(from, exit_pos_, s);
        if (transition_t_ >= 1.0f) {
            state_ = PlayerState::OnFoot;
            pos_ = exit_pos_;
            prev_pos_ = exit_pos_;
            vel_ = Vec3{0.0f, 0.0f, 0.0f};
            grounded_ = false;
            ground_snap(phys, true);
        }
        break;
    }
    }
}

void Player::look(f32 dx, f32 dy)
{
    if (state_ == PlayerState::OnFoot) {
        yaw_ = f_wrap_angle(yaw_ + dx * kLookSensitivity);
        pitch_ = f_clamp(pitch_ - dy * kLookSensitivity, -kPitchLimit, kPitchLimit);
    } else if (state_ == PlayerState::Driving) {
        look_yaw_ = f_clamp(look_yaw_ + dx * kLookSensitivity, -kLookYawLimit, kLookYawLimit);
        look_pitch_ = f_clamp(look_pitch_ - dy * kLookSensitivity, -kLookPitchLimit,
                              kLookPitchLimit);
    }
}

void Player::camera(PhysWorld& phys, const Vehicle* veh, f32 alpha, f32 dt, Camera& out)
{
    const RigidBody* body = veh ? phys.body(veh->body()) : nullptr;

    if (state_ == PlayerState::OnFoot || !body) {
        out.pos = lerp(prev_pos_, pos_, alpha) + Vec3{0.0f, kPlayerEyeHeight, 0.0f};
        out.yaw = yaw_;
        out.pitch = pitch_;
        cockpit_eye_valid_ = false;
        return;
    }

    const Vec3 body_pos = lerp(body->prev_pos, body->pos, alpha);
    const Quat body_rot = slerp(body->prev_rot, body->rot, alpha);
    const Vec3 seat_eye = body_pos + rotate(body_rot, veh->config().seat_eye);
    const Vec3 fwd = rotate(body_rot, Vec3{0.0f, 0.0f, -1.0f});
    const f32 car_yaw = std::atan2(fwd.x, -fwd.z);
    const f32 car_pitch = std::asin(f_clamp(fwd.y, -1.0f, 1.0f));

    if (state_ == PlayerState::Entering) {
        const f32 s = smooth01(transition_t_);
        out.pos = lerp(transition_eye_, seat_eye, s);
        out.yaw = transition_yaw_ + f_wrap_angle(car_yaw - transition_yaw_) * s;
        out.pitch = transition_pitch_ + (car_pitch - transition_pitch_) * s;
        cockpit_eye_valid_ = false;
        return;
    }
    if (state_ == PlayerState::Exiting) {
        const f32 s = smooth01(transition_t_);
        const Vec3 exit_eye = exit_pos_ + Vec3{0.0f, kPlayerEyeHeight, 0.0f};
        out.pos = lerp(transition_eye_, exit_eye, s);
        out.yaw = yaw_;
        out.pitch = pitch_;
        cockpit_eye_valid_ = false;
        return;
    }

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
    out.yaw = f_wrap_angle(car_yaw + look_yaw_);
    out.pitch = f_clamp(car_pitch + look_pitch_, -kPitchLimit, kPitchLimit);
}

} // namespace anom
