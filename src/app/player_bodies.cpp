#include "app/player_bodies.h"
#include "core/log.h"
#include "engine/assets/asset_path.h"
#include "engine/render/post_process.h"
#include "math/glm_bridge.h"
#include "render/debug_draw.h"
#include "sim/sim.h"

#include <glm/gtc/quaternion.hpp>

#include <exception>

namespace anom {
namespace {

constexpr f32 kShroudFadeRate = 4.0f;
constexpr f32 kNameLift = 0.25f;
constexpr f32 kNameSize = 15.0f;
constexpr f32 kDownedShade = 0.55f;
constexpr f32 kOtherGunScale = 1.2f;
constexpr glm::vec3 kOtherHandOnGrip{0.0f, -0.022f, -0.02f};
constexpr f32 kRecoilDecay = 9.0f;
constexpr f32 kGunFollow = 30.0f;
constexpr f32 kWheelHandTurn = 1.2f;
constexpr f32 kShiftReach = 0.55f;
constexpr f32 kHoldGap = 0.03f;

glm::mat4 basis_facing(const glm::vec3& forward, const glm::vec3& up_hint)
{
    const glm::vec3 f = glm::normalize(forward);
    glm::vec3 side = glm::cross(up_hint, f);
    if (glm::length(side) < 1e-4f) {
        side = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), f);
    }
    const glm::vec3 x = glm::normalize(side);
    const glm::vec3 y = glm::cross(f, x);
    return glm::mat4(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(f, 0.0f), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

u32 pack_color(Vec3 c, f32 alpha)
{
    const auto byte = [](f32 v) { return static_cast<u32>(f_clamp01(v) * 255.0f + 0.5f); };
    return (byte(alpha) << 24) | (byte(c.z) << 16) | (byte(c.y) << 8) | byte(c.x);
}

}

ghost::game::BodyPose rotate_pose(const ghost::game::BodyPose& pose, Quat frame, Vec3 anchor)
{
    const glm::quat q = to_glm(frame);
    const glm::mat3 r = glm::mat3_cast(q);
    const glm::vec3 offset = to_glm(anchor);
    ghost::game::BodyPose out = pose;
    const auto point = [&](const glm::vec3& p) { return r * p + offset; };
    out.body = point(pose.body);
    out.bodyBasis = r * pose.bodyBasis;
    out.head = point(pose.head);
    out.neck = point(pose.neck);
    out.headForward = r * pose.headForward;
    out.headUp = r * pose.headUp;
    out.held = point(pose.held);
    for (size_t a = 0; a < out.arms.size(); a++) {
        for (size_t i = 0; i < out.arms[a].size(); i++) {
            out.arms[a][i] = point(pose.arms[a][i]);
        }
    }
    for (size_t l = 0; l < out.legs.size(); l++) {
        out.legs[l].root = point(pose.legs[l].root);
        out.legs[l].joint = point(pose.legs[l].joint);
        out.legs[l].end = point(pose.legs[l].end);
        out.footForward[l] = r * pose.footForward[l];
    }
    out.carryAim = r * pose.carryAim;
    out.holsterGrip = point(pose.holsterGrip);
    out.holsterAim = r * pose.holsterAim;
    out.holsterUp = r * pose.holsterUp;
    out.feet = point(pose.feet);
    return out;
}

void PlayerBodies::update(const Sim& sim, PlayerId local, bool show_local, f32 alpha, f32 dt)
{
    if (!poncho_loaded_) {
        poncho_loaded_ = true;
        if (const auto text = ghost::engine::readAsset("assets/data/cosmetics.json")) {
            poncho_tuning_ = ghost::game::parsePonchoTuning(*text);
        }
    }
    update_bodies(sim, local, show_local, alpha, dt);
    const glm::vec3 wind = to_glm(sim.weather().wind_velocity());
    for (u32 i = 0; i < kMaxSlots; i++) {
        Shown& shown = shown_[i];
        if (!shown.active || !shown.cowboy) {
            shown.poncho.reset();
            continue;
        }
        shown.poncho.setTuning(poncho_tuning_);
        const glm::vec3 up = to_glm(rotate(shown.frame, Vec3{0.0f, 1.0f, 0.0f}));
        shown.poncho.step(shown.world.body, shown.world.bodyBasis, -up * 9.81f, wind, dt);
    }
}

void PlayerBodies::update_bodies(const Sim& sim, PlayerId local, bool show_local, f32 alpha, f32 dt)
{
    for (u32 i = 0; i < kMaxSlots; i++) {
        Shown& shown = shown_[i];
        const PlayerSlot& slot = sim.slots()[i];
        if (!slot.active || (slot.id == local && !show_local)) {
            shown.active = false;
            shown.visible = false;
            continue;
        }
        if (!shown.active) {
            shown = Shown{};
            shown.active = true;
        }
        const Player& player = slot.player;
        const MoveState& now = player.movement().state();
        const MoveState& before = player.movement().previous();
        shown.color = slot.color;
        shown.seed = static_cast<f32>(slot.id) * 17.3f;
        shown.downed = sim.roster().downed(slot.id);
        shown.cowboy = sim.wears_cowboy(slot.id);
        const f32 hidden = now.shroud_time > 0.0f ? 1.0f : 0.0f;
        const f32 fade_step = kShroudFadeRate * dt;
        shown.shroud_fade = f_clamp(hidden, shown.shroud_fade - fade_step, shown.shroud_fade + fade_step);

        shown.item = slot.interact.hands().kind;
        if (player.state() != PlayerState::OnFoot) {
            update_seated(sim, slot, shown, alpha, dt);
            continue;
        }

        const Quat frame = normalize(slerp(before.frame, now.frame, alpha));
        const Vec3 feet = lerp(player.prev_pos(), player.pos(), alpha);
        shown.rig.setShape(ghost::game::BodyShape{});
        place_rig(shown, frame, feet);
        shown.frame = frame;
        const Quat to_local = conjugate(frame);
        shown.feet = feet;
        shown.height = now.height;
        const ghost::game::PlayerState s = ghost_state(now);
        const glm::vec3 local_feet{0.0f};
        using namespace ghost::game;
        shown.recoil *= std::exp(-kRecoilDecay * dt);
        shown.gun = sim.gun_view(slot.id, alpha);
        const glm::vec3 at = local_feet;
        const float squash = s.height / 1.56f;
        const glm::vec3 up{0.0f, 1.0f, 0.0f};
        const glm::vec3 ahead = flatForward(s.yaw);
        const glm::vec3 right = flatRight(s.yaw);
        const glm::vec3 barrel = viewForward(s.yaw, s.pitch);
        const bool lying = s.stance == Stance::Crawl || s.stance == Stance::Dive;
        shown.lying = lying;
        const float reloading = glm::smoothstep(0.0f, 1.0f, shown.gun.crane);
        const float ejecting = glm::clamp(shown.gun.ejector * 2.0f, 0.0f, 1.0f);
        const glm::vec3 reload_aim =
            glm::normalize(ahead * 0.8f + up * glm::mix(-0.45f, 0.9f, ejecting) * (lying ? 0.5f : 1.0f) - right * 0.35f);
        glm::vec3 direction = glm::normalize(glm::mix(barrel, reload_aim, reloading));
        direction = glm::normalize(direction + up * (0.45f * shown.recoil));
        const BodyShape& shape = shown.rig.shape();
        const BodyPose& prior = shown.rig.pose();
        glm::vec3 shoulder = at + up * ((shape.hip + 2.0f * shape.core - 0.11f) * squash) + right * shape.shoulderWidth;
        const glm::vec3 gun_up = lying ? prior.headUp : up;
        const glm::vec3 gun_side = glm::cross(glm::normalize(barrel + glm::vec3(0.0f, 1e-4f, 0.0f)), gun_up);
        const glm::vec3 gun_right = glm::length(gun_side) > 1e-3f ? glm::normalize(gun_side) : right;
        if (prior.mode == BodyMode::Grounded && (lying || glm::distance(prior.arms[1][0] - prior.feet, shoulder - at) < 0.6f)) {
            shoulder = at + (prior.arms[1][0] - prior.feet);
        }
        const glm::vec3 eyes = at + up * s.eyeHeight;
        const glm::vec3 aim = glm::normalize(barrel + glm::vec3(0.0f, 1e-4f, 0.0f));
        const glm::vec3 stomach = at + (prior.body - prior.feet) + up * 0.2f + ahead * 0.21f;
        const float on_side = lying ? std::abs(std::sin(s.roll)) : 0.0f;
        glm::vec3 grip = s.aiming ? eyes + aim * 0.42f - up * 0.09f + right * 0.02f : shoulder + aim * 0.42f - right * 0.06f;
        const float back_weight = lying ? glm::smoothstep(0.0f, 0.5f, -std::cos(s.roll)) : 0.0f;
        if (lying) {
            const glm::vec3 in_front = prior.head + aim * (shape.headRadius * 0.4f + 0.04f * on_side) - gun_up * (shape.headRadius + 0.06f);
            const glm::vec3 beside = shoulder + aim * 0.42f + gun_right * 0.05f;
            grip = glm::mix(s.aiming ? in_front : glm::mix(beside, in_front, on_side), stomach, back_weight);
        }
        glm::vec3 chest = at + up * (0.95f * squash);
        if (prior.mode == BodyMode::Grounded) {
            chest = at + ((prior.arms[0][0] + prior.arms[1][0]) * 0.5f - prior.feet) - up * 0.15f;
        }
        glm::vec3 reload_grip = chest + ahead * 0.32f + right * 0.05f;
        if (lying) {
            reload_grip = at + (prior.head - prior.feet) + ahead * (shape.headRadius * 0.3f) - up * (shape.headRadius + 0.08f) - right * 0.04f;
            reload_grip.y = std::max(reload_grip.y, at.y + 0.12f);
            reload_grip = glm::mix(reload_grip, stomach + up * 0.08f, back_weight);
        }
        const GunPoints& g = gun_points_;
        const glm::vec3 grip_to_muzzle = (g.muzzle - g.grip - kOtherHandOnGrip) * kOtherGunScale;
        const glm::vec3 held = grip + glm::vec3(basis_facing(aim, gun_up) * glm::vec4(grip_to_muzzle, 0.0f));
        const glm::vec3 reload_held = reload_grip + glm::vec3(basis_facing(direction, gun_up) * glm::vec4(grip_to_muzzle, 0.0f));
        const glm::vec3 gun_target =
            glm::mix(held, reload_held, reloading) - direction * (0.07f * shown.recoil) + up * (0.04f * shown.recoil);
        const float follow = 1.0f - std::exp(-kGunFollow * dt);
        shown.gun_at = glm::distance(shown.gun_at, gun_target) > 1.0f ? gun_target : glm::mix(shown.gun_at, gun_target, follow);
        shown.gun_aim = glm::length(shown.gun_aim) > 0.5f ? glm::normalize(glm::mix(shown.gun_aim, direction, follow)) : direction;
        const glm::mat4 gun = glm::translate(glm::mat4(1.0f), shown.gun_at) * basis_facing(shown.gun_aim, gun_up) *
                              glm::scale(glm::mat4(1.0f), glm::vec3(kOtherGunScale)) * glm::translate(glm::mat4(1.0f), -g.muzzle);

        ghost::game::BodyInput in;
        in.feet = local_feet;
        in.velocity = to_glm(rotate(to_local, lerp(before.vel, now.vel, alpha)));
        in.yaw = s.yaw;
        in.pitch = s.pitch;
        in.grounded = s.grounded;
        in.stance = s.stance;
        in.height = s.height;
        in.downed = shown.downed;
        in.aiming = s.aiming;
        in.sprinting = s.sprinting;
        in.holster = s.holster;
        in.roll = s.roll;
        in.lieYaw = s.lieYaw;
        in.gunGrip = glm::vec3(gun * glm::vec4(g.grip + kOtherHandOnGrip, 1.0f));
        in.gunFront = shown.gun_at - direction * 0.05f;
        in.cylinder = glm::vec3(gun * glm::vec4(g.round, 1.0f));
        in.crane = shown.gun.crane;
        in.ejector = shown.gun.ejector;
        in.loading = shown.gun.speedloadProgress >= 0.0f ? shown.gun.speedloadProgress
                                                         : (shown.gun.loadingChamber >= 0 ? shown.gun.loadProgress : -1.0f);
        in.holdHands = static_cast<int>(item_hold_hands(shown.item));
        in.holdHalfWidth = item_cargo_half(shown.item).x + kHoldGap;
        in.reach = slot.use_down && !shown.downed;
        in.reachTo = to_glm(rotate(to_local, slot.view_origin + slot.view_dir * 0.8f - feet));
        shown.rig.update(dt, in);
        shown.world = rotate_pose(shown.rig.pose(), frame, feet);
        shown.visible = shown.shroud_fade < 0.999f;
    }
}

void PlayerBodies::jolt(PlayerId id, const glm::vec3& direction, float strength, bool head)
{
    if (slot_index(id) >= kMaxSlots) {
        return;
    }
    Shown& shown = shown_[slot_index(id)];
    if (shown.active) {
        shown.rig.jolt(glm::inverse(to_glm(shown.frame)) * direction, strength, head);
    }
}

void PlayerBodies::recoil(PlayerId id)
{
    if (slot_index(id) < kMaxSlots) {
        shown_[slot_index(id)].recoil = 1.0f;
    }
}

bool PlayerBodies::pose_of(PlayerId id, ghost::game::BodyPose& pose, float& head_radius, glm::vec3& color, float& height) const
{
    if (slot_index(id) >= kMaxSlots || !shown_[slot_index(id)].visible) {
        return false;
    }
    const Shown& shown = shown_[slot_index(id)];
    pose = shown.world;
    head_radius = shown.rig.shape().headRadius;
    color = to_glm(shown.color);
    height = shown.height;
    return true;
}

void PlayerBodies::draw(const Mat4& view_proj, Vec3 camera_pos, Vec3 light_dir, f32 time)
{
    bool any = false;
    for (const Shown& shown : shown_) {
        any = any || (shown.active && shown.visible);
    }
    if (!any || blob_failed_) {
        return;
    }
    if (!blob_) {
        try {
            blob_ = std::make_unique<ghost::game::BlobBody>();
        } catch (const std::exception& e) {
            log_error("bodies: blob shader unavailable: %s", e.what());
            blob_failed_ = true;
            return;
        }
    }
    blob_->begin(to_glm(view_proj), to_glm(camera_pos), to_glm(light_dir), ghost::engine::PostProcess::depthSplit(),
                 time);
    for (const Shown& shown : shown_) {
        if (!shown.active || !shown.visible) {
            continue;
        }
        const Vec3 color = shown.downed ? shown.color * kDownedShade : shown.color;
        blob_->draw(shown.world, shown.rig.shape().headRadius, to_glm(color), shown.seed, shown.shroud_fade, shown.cowboy,
                    shown.poncho.offsets());
    }
    blob_->end();
}

void PlayerBodies::update_seated(const Sim& sim, const PlayerSlot& slot, Shown& shown, f32 alpha, f32 dt)
{
    const Player& player = slot.player;
    const RigidBody* car = sim.phys().body(sim.vehicle().body());
    if (!car) {
        shown.visible = false;
        return;
    }
    const VehicleConfig& cfg = sim.vehicle().config();
    const Quat car_rot = slerp(car->prev_rot, car->rot, alpha);
    const Vec3 car_pos = lerp(car->prev_pos, car->pos, alpha);
    const Quat to_local = conjugate(car_rot);
    place_rig(shown, car_rot, car_pos);
    const auto cabin = [&](Vec3 body_local) { return to_glm(body_local); };
    const SeatConfig& seat = cfg.seats[player.seat() < cfg.seat_count ? player.seat() : 0];
    const Vec3 seat_feet = car_pos + rotate(car_rot, seat.eye - Vec3{0.0f, kPlayerEyeHeight, 0.0f});
    f32 seated = 1.0f;
    Vec3 feet = seat_feet;
    if (player.state() == PlayerState::Entering) {
        seated = glm::smoothstep(0.0f, 1.0f, player.transition_t());
        const Vec3 start = player.transition_eye() - rotate(player.transition_frame(), Vec3{0.0f, kPlayerEyeHeight, 0.0f});
        feet = lerp(start, seat_feet, seated);
    } else if (player.state() == PlayerState::Exiting) {
        seated = 1.0f - glm::smoothstep(0.0f, 1.0f, player.transition_t());
        feet = lerp(player.prev_pos(), player.pos(), alpha);
    }
    const bool driver = player.state() == PlayerState::Driving && seat.drives && sim.driver() == slot.id;
    const i32 gear = sim.vehicle().train().gear;
    if (driver && gear != shown.gear) {
        shown.shift_time = kShiftReach;
    }
    shown.gear = gear;
    shown.shift_time = f_max(shown.shift_time - dt, 0.0f);

    shown.rig.setShape(ghost::game::BodyShape{}.scaled(f_lerp(1.0f, cfg.body_scale, seated)));
    ghost::game::BodyInput in;
    in.feet = to_glm(rotate(to_local, feet - car_pos));
    in.yaw = player.state() == PlayerState::Driving ? player.look_yaw() : 0.0f;
    in.pitch = player.state() == PlayerState::Driving ? player.look_pitch() : 0.0f;
    in.grounded = true;
    in.height = kPlayerHeight;
    const MoveState& hands = player.movement().state();
    in.holster = seat.drives ? 1.0f : hands.holster;
    in.aiming = !seat.drives && hands.aiming;
    in.downed = shown.downed;
    in.seated = seated;
    in.seatYaw = 0.0f;
    in.seatHips = cabin(seat.hips);
    in.pedals = cabin(seat.feet);
    if (!seat.drives) {
        const glm::vec3 up{0.0f, 1.0f, 0.0f};
        const glm::vec3 aim = ghost::game::viewForward(in.yaw, in.pitch);
        const glm::vec3 right = ghost::game::flatRight(in.yaw);
        const glm::vec3 eyes = cabin(seat.eye);
        in.gunGrip = in.aiming ? eyes + aim * 0.42f - up * 0.09f : eyes - up * 0.32f + right * 0.2f + aim * 0.36f;
        in.gunFront = in.gunGrip + aim * 0.2f;
        in.cylinder = in.gunGrip + aim * 0.1f;
        shown.gun_aim = aim;
        shown.gun_at = in.gunGrip;
        shown.gun = sim.gun_view(slot.id, alpha);
    }
    in.steering = driver;
    in.wheelCenter = cabin(cfg.wheel_center);
    in.wheelNormal = to_glm(normalize(cfg.wheel_normal));
    in.wheelUp = glm::normalize(glm::cross(in.wheelNormal, glm::vec3(1.0f, 0.0f, 0.0f)));
    in.wheelRadius = cfg.wheel_radius;
    in.steer = sim.vehicle().steer_deg() / f_max(cfg.steer_max_deg, 1.0f) * kWheelHandTurn;
    in.shifting = shown.shift_time > 0.0f ? std::sin(kPi * (1.0f - shown.shift_time / kShiftReach)) : 0.0f;
    in.shifter = cabin(cfg.shifter);
    shown.frame = car_rot;
    shown.feet = feet;
    shown.height = kPlayerHeight;
    shown.rig.update(dt, in);
    shown.world = rotate_pose(shown.rig.pose(), car_rot, car_pos);
    shown.visible = shown.shroud_fade < 0.999f;
}

void PlayerBodies::place_rig(Shown& shown, Quat frame, Vec3 anchor)
{
    if (shown.placed) {
        const Quat back = conjugate(frame);
        const glm::mat3 rotation = glm::mat3_cast(to_glm(normalize(back * shown.frame)));
        const glm::vec3 shift = to_glm(rotate(back, shown.anchor - anchor));
        shown.rig.rebase(rotation, shift);
        shown.gun_at = rotation * shown.gun_at + shift;
        shown.gun_aim = rotation * shown.gun_aim;
    }
    shown.frame = frame;
    shown.anchor = anchor;
    shown.placed = true;
}

void PlayerBodies::held_items(std::vector<HeldItem>& out) const
{
    out.clear();
    for (const Shown& shown : shown_) {
        if (!shown.active || !shown.visible || shown.item == ITEM_NONE || shown.world.holding < 0.05f) {
            continue;
        }
        const ghost::game::BodyPose& pose = shown.world;
        const glm::quat facing = glm::quat_cast(glm::mat3(pose.bodyBasis[0], pose.bodyBasis[1], -pose.bodyBasis[2]));
        HeldItem held;
        held.kind = shown.item;
        held.model = mat4_trs(from_glm(pose.held), from_glm(facing), Vec3{1.0f, 1.0f, 1.0f})
                   * mat4_trs(-item_mesh_center(shown.item), quat_identity(), Vec3{1.0f, 1.0f, 1.0f});
        out.push_back(held);
    }
}

void PlayerBodies::guns(std::vector<OtherGun>& out) const
{
    using namespace ghost::game;
    out.clear();
    if (!gun_points_.known) {
        return;
    }
    for (const Shown& shown : shown_) {
        if (!shown.active || !shown.visible) {
            continue;
        }
        const BodyPose& pose = shown.rig.pose();
        if (pose.mode == BodyMode::Down || pose.mode == BodyMode::Toppling) {
            continue;
        }
        glm::vec3 hand = pose.arms[1].back();
        glm::vec3 along = glm::normalize(glm::mix(shown.gun_aim, pose.carryAim, pose.carry) + glm::vec3(0.0f, 1e-4f, 0.0f));
        along = glm::normalize(glm::mix(along, pose.holsterAim, glm::smoothstep(0.0f, 0.6f, pose.holster)) + glm::vec3(0.0f, 1e-4f, 0.0f));
        if (pose.holster > 0.6f) {
            hand = pose.holsterGrip;
            along = pose.holsterAim;
        } else if (pose.gunFree) {
            along = glm::normalize(hand - pose.arms[1][kArmPoints - 2] + glm::vec3(0.0f, 1e-4f, 0.0f));
        }
        const glm::vec3 hand_up = shown.lying ? pose.headUp : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 up_hint = glm::mix(hand_up, pose.holsterUp, glm::smoothstep(0.0f, 0.6f, pose.holster));
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), hand) * basis_facing(along, up_hint) *
                                glm::scale(glm::mat4(1.0f), glm::vec3(kOtherGunScale)) *
                                glm::translate(glm::mat4(1.0f), -(gun_points_.grip + kOtherHandOnGrip));
        OtherGun gun;
        gun.model = glm::translate(glm::mat4(1.0f), to_glm(shown.anchor)) * glm::mat4_cast(to_glm(shown.frame)) * local;
        gun.view = shown.gun;
        gun.shroud = shown.shroud_fade;
        out.push_back(gun);
    }
}

}
