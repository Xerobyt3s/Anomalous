#pragma once

#include "core/types.h"
#include "math/vmath.h"

namespace anom {
class PhysWorld;
class Player;
class Vehicle;
struct Camera;
struct RigidBody;

class PlayerView {
public:
    void reset();
    void chase_look(f32 dx, f32 dy);
    void camera(const Player& player, PhysWorld& phys, const Vehicle* veh, f32 alpha, f32 dt, bool chase,
                f32 pending_dx, f32 pending_dy, Camera& out);

private:
    void chase_camera(const Player& player, PhysWorld& phys, const RigidBody& body, Vec3 body_pos,
                      Quat body_rot, f32 car_yaw, f32 dt, Camera& out);

    Vec3 cockpit_eye_{};
    bool cockpit_eye_valid_ = false;
    f32 chase_yaw_ = 0.0f;
    f32 chase_prev_yaw_ = 0.0f;
    f32 chase_look_yaw_ = 0.0f;
    f32 chase_look_pitch_ = 0.0f;
    f32 look_idle_ = 0.0f;
    f32 chase_dist_ = 0.0f;
    bool chase_valid_ = false;
};

}
