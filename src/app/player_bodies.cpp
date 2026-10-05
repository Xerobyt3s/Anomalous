#include "app/player_bodies.h"
#include "core/log.h"
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

u32 pack_color(Vec3 c, f32 alpha)
{
    const auto byte = [](f32 v) { return static_cast<u32>(f_clamp01(v) * 255.0f + 0.5f); };
    return (byte(alpha) << 24) | (byte(c.z) << 16) | (byte(c.y) << 8) | byte(c.x);
}

}

ghost::game::BodyPose rotate_pose(const ghost::game::BodyPose& pose, Quat frame)
{
    const glm::quat q = to_glm(frame);
    const glm::mat3 r = glm::mat3_cast(q);
    ghost::game::BodyPose out = pose;
    const auto point = [&](const glm::vec3& p) { return r * p; };
    out.body = point(pose.body);
    out.bodyBasis = r * pose.bodyBasis;
    out.head = point(pose.head);
    out.neck = point(pose.neck);
    out.headForward = r * pose.headForward;
    out.headUp = r * pose.headUp;
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

void PlayerBodies::update(const Sim& sim, PlayerId local, f32 alpha, f32 dt)
{
    for (u32 i = 0; i < kMaxPlayers; i++) {
        Shown& shown = shown_[i];
        const PlayerSlot& slot = sim.slots()[i];
        if (!slot.active || slot.id == local) {
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
        const f32 hidden = now.shroud_time > 0.0f ? 1.0f : 0.0f;
        const f32 fade_step = kShroudFadeRate * dt;
        shown.shroud_fade = f_clamp(hidden, shown.shroud_fade - fade_step, shown.shroud_fade + fade_step);

        if (player.state() != PlayerState::OnFoot) {
            shown.visible = false;
            continue;
        }

        shown.frame = now.frame;
        const Quat to_local = conjugate(now.frame);
        const Vec3 feet = lerp(player.prev_pos(), player.pos(), alpha);
        shown.feet = feet;
        shown.height = now.height;
        const ghost::game::PlayerState s = ghost_state(now);
        const glm::vec3 local_feet = to_glm(rotate(to_local, feet));
        const glm::vec3 up{0.0f, 1.0f, 0.0f};
        const glm::vec3 aim = ghost::game::viewForward(s.yaw, s.pitch);
        const glm::vec3 right = ghost::game::flatRight(s.yaw);
        const glm::vec3 eyes = local_feet + up * s.eyeHeight;
        const glm::vec3 grip = s.aiming ? eyes + aim * 0.42f - up * 0.09f
                                        : eyes - up * 0.32f + right * 0.24f + aim * 0.42f;

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
        in.gunGrip = grip;
        in.gunFront = grip + aim * 0.2f;
        in.cylinder = grip + aim * 0.1f;
        in.reach = slot.use_down && !shown.downed;
        in.reachTo = to_glm(rotate(to_local, slot.view_origin + slot.view_dir * 0.8f));
        shown.rig.update(dt, in);
        shown.world = rotate_pose(shown.rig.pose(), now.frame);
        shown.visible = shown.shroud_fade < 0.999f;
    }
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
        blob_->draw(shown.world, shown.rig.shape().headRadius, to_glm(color), shown.seed, shown.shroud_fade);
    }
    blob_->end();
}

void PlayerBodies::draw_names(DebugDraw& debug, const Sim& sim) const
{
    for (u32 i = 0; i < kMaxPlayers; i++) {
        const Shown& shown = shown_[i];
        if (!shown.active || !shown.visible) {
            continue;
        }
        const PlayerSlot& slot = sim.slots()[i];
        const Vec3 up = rotate(shown.frame, Vec3{0.0f, 1.0f, 0.0f});
        const Vec3 at = shown.feet + up * (shown.height + kNameLift);
        const u32 color = pack_color(slot.color, 0.9f * (1.0f - shown.shroud_fade));
        debug.text_3d(at, kNameSize, color, "%s%s", slot.name.c_str(), shown.downed ? " (down)" : "");
    }
}

}
