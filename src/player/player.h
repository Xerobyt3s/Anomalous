#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "player/movement.h"

namespace anom {

class PhysWorld;
class Vehicle;
struct Camera;
struct RigidBody;

inline constexpr f32 kPlayerRadius = 0.32f;
inline constexpr f32 kPlayerHeight = 1.56f;
inline constexpr f32 kPlayerEyeHeight = 1.44f;

enum class PlayerState : u32 {
    OnFoot,
    Entering,
    Driving,
    Exiting,
};

struct PlayerCommand {
    f32 move_x = 0.0f;
    f32 move_z = 0.0f;
    bool run = false;
    bool jump = false;
    bool interact = false;
    bool crouch = false;
    bool crawl = false;
};

class Player {
public:
    void init(Vec3 pos, f32 yaw);
    void teleport(Vec3 pos, f32 yaw);
    void tick(PhysWorld& phys, Vehicle* veh, const PlayerCommand& cmd, f32 dt);
    void look(f32 dx, f32 dy);
    void camera(PhysWorld& phys, const Vehicle* veh, f32 alpha, f32 dt, bool chase, Camera& out);

    bool driving() const { return state_ == PlayerState::Driving; }
    bool can_enter(PhysWorld& phys, const Vehicle* veh) const;
    bool can_exit(PhysWorld& phys, const Vehicle* veh) const;

    PlayerState state() const { return state_; }
    Vec3 pos() const;
    Vec3 prev_pos() const;
    Vec3 vel() const;
    Vec3 up() const;
    f32 yaw() const { return movement_.state().yaw; }
    f32 pitch() const { return movement_.state().pitch; }
    bool grounded() const { return movement_.state().grounded; }
    Stance stance() const { return movement_.state().stance; }
    f32 look_yaw() const { return look_yaw_; }
    f32 look_pitch() const { return look_pitch_; }
    f32 transition_t() const { return transition_t_; }
    f32 up_turn_rate() const;
    f32 field_presence() const;
    const Movement& movement() const { return movement_; }

    void set_speed_mul(f32 mul) { movement_.set_speed_mul(mul); }
    void set_exit_pref(i32 pref) { exit_pref_ = pref; }

private:
    void sync_jolt(PhysWorld& phys, const RigidBody* car);
    bool probe_exit(PhysWorld& phys, const Vehicle& veh, Vec3* out_foot) const;
    void update_car_frame(PhysWorld& phys, Vec3 body_pos, f32 dt);
    void chase_camera(PhysWorld& phys, const RigidBody& body, Vec3 body_pos, Quat body_rot,
                      f32 car_yaw, f32 dt, Camera& out);

    Movement movement_;
    PlayerState state_ = PlayerState::OnFoot;
    Vec3 seat_pos_{};
    Vec3 seat_prev_pos_{};
    Vec3 seat_vel_{};
    f32 transition_t_ = 0.0f;
    Vec3 transition_eye_{};
    Quat transition_frame_ = quat_identity();
    f32 transition_yaw_ = 0.0f;
    f32 transition_pitch_ = 0.0f;
    Vec3 exit_pos_{};
    Vec3 cockpit_eye_{};
    bool cockpit_eye_valid_ = false;
    f32 look_yaw_ = 0.0f;
    f32 look_pitch_ = 0.0f;
    f32 chase_yaw_ = 0.0f;
    f32 chase_prev_yaw_ = 0.0f;
    f32 look_idle_ = 0.0f;
    bool chase_active_ = false;
    f32 chase_dist_ = 0.0f;
    bool chase_valid_ = false;
    Quat car_frame_ = quat_identity();
    f32 car_turn_rate_ = 0.0f;
    f32 car_presence_ = 0.0f;
    i32 exit_pref_ = 0;
};

} // namespace anom
