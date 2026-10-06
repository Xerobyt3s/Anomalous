#pragma once

#include "core/handle.h"
#include "core/types.h"
#include "math/vmath.h"

namespace anom {

inline constexpr u32 kNoJoltBody = 0xFFFFFFFFu;

struct RigidBody {
    Vec3 pos;
    Vec3 prev_pos;
    Quat rot;
    Quat prev_rot;
    Vec3 vel;
    Vec3 angular_vel;
    Vec3 force_accum;
    Vec3 torque_accum;
    f32 inv_mass;
    Mat3 inv_inertia_local;
    Vec3 half_extents;
    f32 restitution;
    f32 friction;
    Vec3 box_offset;
    b32 asleep;
    Vec3 gravity;
    u32 jolt_id;
    b32 wake_request;
    Vec3 read_pos;
    Quat read_rot;
    Vec3 read_vel;
    Vec3 read_angular_vel;
};

using BodyHandle = Handle<RigidBody>;

Mat3 body_inv_inertia_world(const RigidBody& body);
Vec3 body_velocity_at_point(const RigidBody& body, Vec3 point);
void body_apply_force_at_point(RigidBody& body, Vec3 force, Vec3 point);
void body_apply_torque(RigidBody& body, Vec3 torque);
void body_apply_impulse_at_point(RigidBody& body, Vec3 impulse, Vec3 point);
void body_wake(RigidBody& body);
bool body_state_valid(const RigidBody& body);

} // namespace anom
