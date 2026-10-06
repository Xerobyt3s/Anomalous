#include "physics/body.h"

#include <cmath>

namespace anom {

Mat3 body_inv_inertia_world(const RigidBody& body)
{
    const Mat3 r = quat_to_mat3(body.rot);
    return r * (body.inv_inertia_local * transpose(r));
}

Vec3 body_velocity_at_point(const RigidBody& body, Vec3 point)
{
    return body.vel + cross(body.angular_vel, point - body.pos);
}

void body_wake(RigidBody& body)
{
    body.asleep = 0;
    body.wake_request = 1;
}

void body_apply_force_at_point(RigidBody& body, Vec3 force, Vec3 point)
{
    body_wake(body);
    body.force_accum += force;
    body.torque_accum += cross(point - body.pos, force);
}

void body_apply_torque(RigidBody& body, Vec3 torque)
{
    body_wake(body);
    body.torque_accum += torque;
}

void body_apply_impulse_at_point(RigidBody& body, Vec3 impulse, Vec3 point)
{
    body_wake(body);
    body.vel += impulse * body.inv_mass;
    body.angular_vel += body_inv_inertia_world(body) * cross(point - body.pos, impulse);
}

bool body_state_valid(const RigidBody& body)
{
    const f32 values[10] = {body.pos.x,         body.pos.y,         body.pos.z,
                            body.vel.x,         body.vel.y,         body.vel.z,
                            body.angular_vel.x, body.angular_vel.y, body.angular_vel.z,
                            body.rot.w};
    for (const f32 value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

} // namespace anom
