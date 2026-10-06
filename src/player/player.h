#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "player/movement.h"

namespace anom {
class PhysWorld;
class Vehicle;
struct RigidBody;

inline constexpr f32 kPlayerRadius = 0.32f;
inline constexpr f32 kPlayerHeight = 1.56f;
inline constexpr f32 kPlayerEyeHeight = 1.44f;
inline constexpr f32 kLookSensitivity = 0.0022f;
inline constexpr f32 kLookYawLimit = 2.4f;
inline constexpr f32 kLookPitchLimit = 1.0f;

enum class PlayerState : u32 {
    OnFoot,
    Entering,
    Driving,
    Exiting,
};

inline constexpr u32 kCommandMaxChars = 32;

struct PlayerCommand {
    f32 move_x = 0.0f;
    f32 move_z = 0.0f;
    bool run = false;
    bool jump = false;
    bool interact = false;
    bool crouch = false;
    bool crawl = false;
    bool aim = false;
    bool holster = false;
    bool hands_busy = false;

    bool gameplay = false;
    f32 look_dx = 0.0f;
    f32 look_dy = 0.0f;
    Vec3 view_origin{};
    Vec3 view_dir{0.0f, 0.0f, -1.0f};
    bool use_down = false;
    bool use_pressed = false;
    bool face_view = false;

    f32 throttle = 0.0f;
    f32 reverse = 0.0f;
    f32 steer = 0.0f;
    bool handbrake = false;
    bool headlights_toggle = false;
    bool manual_toggle = false;
    i32 shift = 0;
    bool take_key = false;
    bool crank = false;
    bool recover = false;
    bool reset_car = false;
    bool handbrake_toggle = false;
    bool ignition_tap = false;
    bool wipers_cycle = false;
    bool horn = false;

    f32 throw_power = -1.0f;
    bool place_commit = false;
    Vec3 place_pos{};
    f32 place_yaw = 0.0f;
    bool stow_cable = false;

    bool terminal_leave = false;
    u32 terminal_keys = 0;
    char terminal_chars[kCommandMaxChars] = {};
    u32 terminal_char_count = 0;
    f32 terminal_orbit = 0.0f;
    f32 terminal_zoom = 0.0f;

    bool dummy_cycle = false;
    i32 dummy_script = -1;
    i32 spawn_ghost = -1;
    i32 scene = -1;

    bool trigger = false;
    bool cock = false;
    bool cylinder = false;
    bool close_cylinder = false;
    bool eject = false;
    bool speedload = false;
    i32 turn = 0;
    i32 load_element = -1;
    i32 quick_fill_element = -1;
    Vec3 muzzle{};
    Vec3 barrel_dir{0.0f, 0.0f, -1.0f};
    Vec3 wind{};
};

class Player {
public:
    void init(Vec3 pos, f32 yaw);
    void teleport(Vec3 pos, f32 yaw);
    void tick(PhysWorld& phys, Vehicle* veh, const PlayerCommand& cmd, f32 dt);
    void look(f32 dx, f32 dy);

    bool driving() const { return state_ == PlayerState::Driving; }
    bool can_enter(PhysWorld& phys, const Vehicle* veh, u32 seat = 0) const;
    u32 seat() const { return seat_; }
    void set_seat(u32 seat) { seat_ = seat; }
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
    Vec3 transition_eye() const { return transition_eye_; }
    Quat transition_frame() const { return transition_frame_; }
    f32 transition_yaw() const { return transition_yaw_; }
    f32 transition_pitch() const { return transition_pitch_; }
    Vec3 exit_pos() const { return exit_pos_; }
    Quat car_frame() const { return car_frame_; }
    f32 up_turn_rate() const;
    f32 field_presence() const;
    const Movement& movement() const { return movement_; }
    Movement& movement() { return movement_; }
    void set_character(u32 id) { movement_.set_character(id); }
    void adopt(const Player& other);
    void eject(PhysWorld& phys, const Vehicle& veh);
    u32 character() const { return movement_.character(); }

    void set_speed_mul(f32 mul) { movement_.set_speed_mul(mul); }
    void set_exit_pref(i32 pref) { exit_pref_ = pref; }

private:
    void sync_jolt(PhysWorld& phys);
    bool probe_exit(PhysWorld& phys, const Vehicle& veh, Vec3* out_foot) const;
    void update_car_frame(PhysWorld& phys, Vec3 body_pos, f32 dt);

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
    f32 look_yaw_ = 0.0f;
    f32 look_pitch_ = 0.0f;
    Quat car_frame_ = quat_identity();
    f32 car_turn_rate_ = 0.0f;
    f32 car_presence_ = 0.0f;
    i32 exit_pref_ = 0;
    u32 seat_ = 0;
};

}
