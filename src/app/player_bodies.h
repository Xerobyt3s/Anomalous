#pragma once

#include "core/types.h"
#include "game/fx/blob_body.h"
#include "game/player/body_rig.h"
#include "math/vmath.h"
#include "sim/player_slot.h"

#include <array>
#include <memory>

namespace anom {

class DebugDraw;
class Sim;
class TextRenderer;
struct Camera;

ghost::game::BodyPose rotate_pose(const ghost::game::BodyPose& pose, Quat frame);

class PlayerBodies {
public:
    void update(const Sim& sim, PlayerId local, f32 alpha, f32 dt);
    void draw(const Mat4& view_proj, Vec3 camera_pos, Vec3 light_dir, f32 time);
    void draw_names(DebugDraw& debug, const Sim& sim) const;
    bool visible(PlayerId id) const { return id < kMaxPlayers && shown_[id].visible; }
    const ghost::game::BodyPose& pose(PlayerId id) const { return shown_[id].world; }

private:
    struct Shown {
        ghost::game::BodyRig rig;
        ghost::game::BodyPose world;
        Quat frame = quat_identity();
        Vec3 feet{};
        Vec3 color{0.8f, 0.8f, 0.8f};
        f32 shroud_fade = 0.0f;
        f32 seed = 0.0f;
        f32 height = 1.56f;
        bool downed = false;
        bool active = false;
        bool visible = false;
    };

    std::array<Shown, kMaxPlayers> shown_{};
    std::unique_ptr<ghost::game::BlobBody> blob_;
    bool blob_failed_ = false;
};

}
