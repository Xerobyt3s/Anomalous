#pragma once

#include "carsys/items.h"
#include "core/types.h"
#include "game/fx/blob_body.h"
#include "game/player/body_rig.h"
#include "game/weapons/mechanism_view.h"
#include "math/vmath.h"
#include "sim/player_slot.h"

#include <array>
#include <memory>
#include <vector>

namespace anom {

class DebugDraw;
class Sim;
class TextRenderer;
struct Camera;

struct OtherGun {
    glm::mat4 model{1.0f};
    ghost::game::MechanismView view;
    f32 shroud = 0.0f;
};

struct HeldItem {
    ItemKind kind = ITEM_NONE;
    Mat4 model = mat4_identity();
};

struct GunPoints {
    glm::vec3 muzzle{0.0f};
    glm::vec3 grip{0.0f};
    glm::vec3 round{0.0f};
    bool known = false;
};

ghost::game::BodyPose rotate_pose(const ghost::game::BodyPose& pose, Quat frame);

class PlayerBodies {
public:
    void update(const Sim& sim, PlayerId local, bool show_local, f32 alpha, f32 dt);
    void draw(const Mat4& view_proj, Vec3 camera_pos, Vec3 light_dir, f32 time);
    bool visible(PlayerId id) const { return slot_index(id) < kMaxSlots && shown_[slot_index(id)].visible; }
    const ghost::game::BodyPose& pose(PlayerId id) const { return shown_[slot_index(id)].world; }
    void jolt(PlayerId id, const glm::vec3& direction, float strength, bool head);
    void recoil(PlayerId id);
    bool pose_of(PlayerId id, ghost::game::BodyPose& pose, float& head_radius, glm::vec3& color, float& height) const;
    void set_gun_points(const GunPoints& points) { gun_points_ = points; }
    void guns(std::vector<OtherGun>& out) const;
    void held_items(std::vector<HeldItem>& out) const;

private:
    struct Shown;
    void update_seated(const Sim& sim, const PlayerSlot& slot, Shown& shown, f32 alpha, f32 dt);
    static void place_rig(Shown& shown, Quat frame, Vec3 anchor);

    struct Shown {
        ghost::game::BodyRig rig;
        ghost::game::BodyPose world;
        Quat frame = quat_identity();
        Vec3 anchor{};
        bool placed = false;
        Vec3 feet{};
        Vec3 color{0.8f, 0.8f, 0.8f};
        f32 shroud_fade = 0.0f;
        f32 seed = 0.0f;
        f32 height = 1.56f;
        f32 recoil = 0.0f;
        bool lying = false;
        ItemKind item = ITEM_NONE;
        i32 gear = 0;
        f32 shift_time = 0.0f;
        glm::vec3 gun_at{0.0f};
        glm::vec3 gun_aim{0.0f};
        ghost::game::MechanismView gun;
        bool downed = false;
        bool active = false;
        bool visible = false;
    };

    std::array<Shown, kMaxSlots> shown_{};
    std::unique_ptr<ghost::game::BlobBody> blob_;
    bool blob_failed_ = false;
    GunPoints gun_points_;
};

}
