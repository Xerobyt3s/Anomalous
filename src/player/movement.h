#pragma once

#include "core/types.h"
#include "math/vmath.h"

namespace anom {

class JoltWorld;
class GravityField;

struct MoveCommand {
    Vec2 move{};
    bool sprint = false;
    bool jump = false;
    bool crouch = false;
    bool crawl = false;
};

struct MoveTuning {
    f32 walk_speed = 4.0f;
    f32 sprint_speed = 6.4f;
    f32 ground_accel = 22.0f;
    f32 ground_decel = 28.0f;
    f32 air_accel = 4.0f;
    f32 jump_speed = 4.2f;
    f32 radius = 0.32f;
    f32 height = 1.56f;
    f32 eye_height = 1.44f;
    f32 crouch_height = 1.2f;
    f32 crouch_eye_height = 1.05f;
    f32 slide_eye_height = 0.84f;
    f32 crouch_speed = 1.8f;
    f32 stance_rate = 12.0f;
    f32 slide_min_speed = 4.8f;
    f32 slide_boost = 1.1f;
    f32 slide_friction = 5.0f;
    f32 slide_steer = 6.0f;
    f32 slide_end_speed = 2.0f;
    f32 slide_cooldown = 0.4f;
    f32 crawl_height = 0.66f;
    f32 crawl_eye_height = 0.44f;
    f32 crawl_speed = 1.0f;
    f32 dive_speed = 6.5f;
    f32 dive_lift = 1.5f;
    f32 dive_eye_height = 0.6f;
    f32 belly_friction = 6.0f;
    f32 up_turn_ground = 3.2f;
    f32 up_turn_air = 1.7f;
};

enum class Stance : u32 {
    Stand,
    Crouch,
    Slide,
    Crawl,
    Dive,
};

struct MoveState {
    Vec3 pos{};
    Vec3 vel{};
    Quat frame = quat_identity();
    f32 yaw = 0.0f;
    f32 pitch = 0.0f;
    bool grounded = false;
    bool sprinting = false;
    Stance stance = Stance::Stand;
    f32 height = 1.56f;
    f32 eye_height = 1.44f;
    f32 slide_cooldown = 0.0f;
    bool crouch_held = false;
    bool dive_used = false;
    f32 tilt = 0.0f;
    f32 up_turn_rate = 0.0f;
    f32 field_presence = 0.0f;
    Vec3 ground_vel{};
    Vec3 ground_normal{0.0f, 1.0f, 0.0f};
};

Vec3 frame_up(Quat frame);
Vec3 frame_forward(Quat frame, f32 yaw);
Vec3 frame_right(Quat frame, f32 yaw);
Vec3 frame_view(Quat frame, f32 yaw, f32 pitch);
Quat frame_turn_up(Quat frame, Vec3 new_up);
void frame_view_angles(Quat frame, Vec3 dir, f32& yaw, f32& pitch);

class Movement {
public:
    void init(JoltWorld* jolt, Vec3 feet, Vec3 up, f32 yaw);
    void attach(JoltWorld* jolt);
    void teleport(Vec3 feet);
    void align_up(Vec3 up);
    void settle(const GravityField* field);
    void look(f32 dyaw, f32 dpitch);
    void face(Vec3 dir);
    void tick(const MoveCommand& cmd, const GravityField* field, f32 dt);

    const MoveState& state() const { return state_; }
    const MoveState& previous() const { return previous_; }
    MoveTuning& tuning() { return tuning_; }
    const MoveTuning& tuning() const { return tuning_; }
    void set_speed_mul(f32 mul) { speed_mul_ = mul; }

    Vec3 up() const { return frame_up(state_.frame); }
    Vec3 eye() const { return state_.pos + up() * state_.eye_height; }
    bool fits(Vec3 feet, f32 height) const;

private:
    void turn_up(Vec3 target, f32 dt);
    void apply_stance_height();

    JoltWorld* jolt_ = nullptr;
    MoveTuning tuning_;
    MoveState state_;
    MoveState previous_;
    f32 speed_mul_ = 1.0f;
};

} // namespace anom
